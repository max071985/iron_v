/*
 * src/ota.h
 *
 * Dual-Slot Flash OTA Firmware Upgrade & Rollback Subsystem
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 *
 * Implements fail-safe A/B dual-slot firmware partitioning, non-destructive
 * boot header validation, rollback state machine, and MSPI Flash MMU partition swapping.
 */

#ifndef IRONV_OTA_H
#define IRONV_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return Status Codes */
typedef enum {
    OTA_OK                      =  0,
    OTA_ERR_INVALID_PARAM       = -1,
    OTA_ERR_INVALID_IMAGE       = -2,
    OTA_ERR_FLASH_IO            = -3,
    OTA_ERR_CORRUPT             = -4,
    OTA_ERR_BUSY                = -5,
    OTA_ERR_NOT_PERMITTED       = -6,
    OTA_ERR_ALREADY_ACTIVE      = -7
} ota_status_t;

/* Dual-Slot Partition Identifiers */
typedef enum {
    OTA_SLOT_0 = 0,
    OTA_SLOT_1 = 1,
    OTA_SLOT_COUNT = 2,
    OTA_SLOT_INVALID = -1
} ota_slot_t;

/* Slot Lifecycle States */
typedef enum {
    OTA_STATE_UNINITIALIZED     = 0xFFFFFFFFU,
    OTA_STATE_NEW               = 0x00000000U,
    OTA_STATE_TESTING           = 0x55AA55AAU,
    OTA_STATE_VALID             = 0x5A5A5A5AU,
    OTA_STATE_ABORTED           = 0xDEADDEADU
} ota_state_t;

/* 8 MB Physical Flash Partition Geometry Constants (Zero Magic Numbers) */
#define OTA_FLASH_TOTAL_SIZE            (0x00800000U)  /* 8 MB total NOR flash */
#define OTA_FLASH_SECTOR_SIZE           (0x00001000U)  /* 4 KB sector erase granularity */
#define OTA_FLASH_BLOCK_SIZE            (0x00010000U)  /* 64 KB block erase / MMU page */
#define OTA_FLASH_PAGE_SIZE             (0x00000100U)  /* 256 bytes SPI page program */

/* Slot 0 (Primary / Factory Application Partition) */
#define OTA_SLOT_0_OFFSET               (0x00000000U)  /* 0 KB offset (boots monolithic) */
#define OTA_SLOT_0_SIZE                 (0x003C0000U)  /* 3.75 MB capacity (60 pages) */

/* Slot 1 (Secondary / Upgrade Application Partition) */
#define OTA_SLOT_1_OFFSET               (0x003C0000U)  /* 3.75 MB offset (page 60) */
#define OTA_SLOT_1_SIZE                 (0x003C0000U)  /* 3.75 MB capacity (60 pages) */

/* OTA Selection Data Partition (Redundant Fail-Safe Sectors) */
#define OTA_DATA_SECTOR_0_OFFSET        (0x00780000U)  /* 7.5 MB (Sector 1920) */
#define OTA_DATA_SECTOR_1_OFFSET        (0x00781000U)  /* 7.5 MB + 4 KB (Sector 1921) */
#define OTA_DATA_SECTOR_SIZE            (0x00001000U)  /* 4 KB */

/* Storage / NVS Configuration Partition */
#define OTA_STORAGE_NVS_OFFSET          (0x00790000U)  /* 7.5625 MB */
#define OTA_STORAGE_NVS_SIZE            (0x00070000U)  /* 448 KB */

/* Selection Record Header Constants */
#define OTA_SELECT_MAGIC                (0x4F544153U)  /* "OTAS" */
#define OTA_MAX_TEST_BOOTS              (3U)

/* ESP32-C6 Standard Image Header Constants */
#define ESP_IMAGE_HEADER_MAGIC          (0xE9U)
#define ESP_IMAGE_HEADER_SIZE           (24U)
#define ESP_IMAGE_SEG_HEADER_SIZE       (8U)
#define ESP_IMAGE_MAX_SEGMENTS          (16U)
#define ESP_IMAGE_CHIP_ID_ESP32C6       (13U)
#define ESP_IMAGE_DEFAULT_ENTRY_ADDR    (0x40800000U)

/* Standard ESP32 Image Header Structure */
typedef struct __attribute__((packed)) {
    uint8_t  magic;              /* 0xE9 */
    uint8_t  segment_count;      /* Count of binary segments */
    uint8_t  spi_mode;           /* 0: QIO, 1: QOUT, 2: DIO, 3: DOUT */
    uint8_t  spi_speed_size;     /* Low 4b: speed (80M=0), High 4b: size (8MB=3) */
    uint32_t entry_addr;         /* Execution entrypoint (0x40800000) */
    uint8_t  wp_pin;             /* 0xEE: disabled */
    uint8_t  spi_pin_drv[3];     /* Pin drive strengths */
    uint16_t chip_id;            /* 0x000D: ESP32-C6 */
    uint8_t  min_chip_rev;       /* Minimal chip revision */
    uint16_t min_chip_rev_full;  /* Full min revision */
    uint16_t max_chip_rev_full;  /* Full max revision */
    uint8_t  reserved[4];        /* Reserved padding */
    uint8_t  hash_appended;      /* 1 if SHA-256 appended */
} esp_image_header_t;

/* ESP32 Image Segment Header */
typedef struct __attribute__((packed)) {
    uint32_t load_addr;          /* Load address in SRAM or Flash XIP */
    uint32_t data_len;           /* Segment length in bytes */
} esp_image_segment_header_t;

/* Redundant Fail-Safe OTA Selection Sector Record */
typedef struct __attribute__((packed)) {
    uint32_t magic;              /* 0x4F544153 ("OTAS") */
    uint32_t seq;                /* Monotonically increasing sequence number */
    uint32_t slot;               /* Active slot (OTA_SLOT_0 or OTA_SLOT_1) */
    uint32_t state;              /* ota_state_t */
    uint32_t image_size;         /* Firmware size in bytes */
    uint32_t image_crc32;        /* Firmware image CRC32 */
    uint32_t version_major;      /* Major version */
    uint32_t version_minor;      /* Minor version */
    uint32_t version_patch;      /* Patch version */
    uint32_t boot_count;         /* Boots attempted in testing state */
    uint32_t reserved[5];        /* Alignment padding */
    uint32_t crc32;              /* CRC32 of fields above */
} ota_select_t;

/* Partition Geometry Query Structure */
typedef struct {
    ota_slot_t slot;
    uint32_t   phys_offset;
    uint32_t   size_bytes;
    bool       is_active;
    ota_state_t state;
    uint32_t   seq;
} ota_partition_t;

/* Subsystem Telemetry & Status Report */
typedef struct {
    ota_slot_t  active_slot;
    ota_slot_t  inactive_slot;
    ota_state_t active_state;
    uint32_t    active_seq;
    uint32_t    boot_attempts;
    uint32_t    total_switches;
    uint32_t    total_rollbacks;
    uint32_t    verified_images;
    uint32_t    flash_reads;
    uint32_t    flash_writes;
    uint32_t    flash_erases;
    bool        rollback_possible;
} ota_status_report_t;

/* Public API Functions */
ota_status_t ota_init(void);
ota_slot_t   ota_get_active_slot(void);
ota_slot_t   ota_get_inactive_slot(void);
ota_state_t  ota_get_slot_state(ota_slot_t slot);
ota_status_t ota_get_partition_info(ota_slot_t slot, ota_partition_t *out_part);
ota_status_t ota_parse_image_header(const uint8_t *data, size_t len, esp_image_header_t *out_hdr);
ota_status_t ota_verify_image(ota_slot_t slot, esp_image_header_t *out_hdr);
ota_status_t ota_switch_slot(ota_slot_t slot);
ota_status_t ota_mark_valid(void);
ota_status_t ota_rollback(void);
ota_status_t ota_get_status(ota_status_report_t *out_report);
ota_status_t ota_read_slot(ota_slot_t slot, uint32_t offset, void *dest, size_t len);
ota_status_t ota_write_chunk(ota_slot_t slot, uint32_t offset, const void *data, size_t len);
ota_status_t ota_erase_slot(ota_slot_t slot);

/* Flash XIP Diagnostic Visualizers */
void ota_print_status(void);
void ota_print_partitions(void);

/* Host Test Mock Interfaces */
#if !defined(__riscv)
void ota_mock_reset(void);
void ota_mock_set_active_slot(ota_slot_t slot, uint32_t seq, ota_state_t state);
void ota_mock_corrupt_select(uint32_t sector_idx);
uint8_t *ota_mock_get_flash_ptr(uint32_t offset);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IRONV_OTA_H */
