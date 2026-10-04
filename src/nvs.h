/*
 * src/nvs.h
 *
 * Non-Volatile Storage (NVS) & Golden Master Seal Subsystem
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 *
 * Implements persistent, wear-leveled, CRC32-verified key-value storage in the
 * 448 KB NOR Flash partition at 0x00790000 (OTA_STORAGE_NVS_OFFSET) and provides
 * full-system production hardening and golden master integrity auditing.
 */

#ifndef IRONV_NVS_H
#define IRONV_NVS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return Status Codes */
typedef enum {
    NVS_OK                      =  0,
    NVS_ERR_NOT_FOUND           = -1,
    NVS_ERR_INVALID_PARAM       = -2,
    NVS_ERR_FLASH_IO            = -3,
    NVS_ERR_FULL                = -4,
    NVS_ERR_CORRUPT             = -5
} nvs_status_t;

/* Value Data Types */
typedef enum {
    NVS_TYPE_RAW                = 0,
    NVS_TYPE_U32                = 1,
    NVS_TYPE_STR                = 2,
    NVS_TYPE_BLOB               = 3
} nvs_type_t;

/* Geometry & Limits Constants */
#define NVS_FLASH_BASE_OFFSET   (0x00790000U)  /* 448 KB NVS partition origin */
#define NVS_FLASH_SECTOR_SIZE   (0x00001000U)  /* 4 KB sector size */
#define NVS_FLASH_TOTAL_SIZE    (0x00070000U)  /* 448 KB total partition size */
#define NVS_MAX_ENTRIES         (32U)          /* Max active key-value pairs */
#define NVS_KEY_MAX_LEN         (32U)          /* Max key length including null */
#define NVS_VAL_MAX_LEN         (64U)          /* Max value payload length */

#define NVS_MAGIC               (0x49524E56U)  /* "IRNV" - Iron NVS */
#define NVS_SECTOR_MAGIC        (0x4E565331U)  /* "NVS1" */
#define NVS_FLAG_VALID          (0x01U)
#define NVS_FLAG_DELETED        (0x00U)

/* Golden Master Seal Constants */
#define GOLDEN_MASTER_MAGIC     (0x5A5A5A5AU)  /* Golden Seal Verification Marker */

/* Packed Key-Value Entry Structure */
typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;                    /* NVS_MAGIC */
    uint8_t  key[NVS_KEY_MAX_LEN];     /* Key name string */
    uint16_t val_len;                  /* Byte length of value */
    uint8_t  type;                     /* nvs_type_t */
    uint8_t  flags;                    /* NVS_FLAG_VALID or NVS_FLAG_DELETED */
    uint8_t  val[NVS_VAL_MAX_LEN];     /* Payload */
    uint32_t crc32;                    /* IEEE 802.3 CRC32 over magic through val */
} nvs_entry_t;

/* Telemetry & Statistics */
typedef struct {
    uint32_t total_keys;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t reads_count;
    uint32_t writes_count;
    uint32_t erases_count;
} nvs_stats_t;

/* Golden Master System Health & Sealing Audit Report */
typedef struct {
    bool     memory_cartography_ok;
    bool     watchdogs_ok;
    bool     efuse_security_ok;
    bool     rf_coexistence_ok;
    bool     ota_partitions_ok;
    bool     nvs_storage_ok;
    uint32_t golden_seal_magic;        /* GOLDEN_MASTER_MAGIC (0x5A5A5A5A) */
    uint32_t total_assertions_passed;
} golden_master_report_t;

/* Public NVS Management APIs */
nvs_status_t nvs_init(void);
nvs_status_t nvs_set_u32(const char *key, uint32_t val);
nvs_status_t nvs_get_u32(const char *key, uint32_t *out_val);
nvs_status_t nvs_set_str(const char *key, const char *val);
nvs_status_t nvs_get_str(const char *key, char *out_val, size_t max_len);
nvs_status_t nvs_set_blob(const char *key, const void *val, size_t len);
nvs_status_t nvs_get_blob(const char *key, void *out_val, size_t max_len, size_t *out_len);
nvs_status_t nvs_erase_key(const char *key);
nvs_status_t nvs_erase_all(void);
nvs_status_t nvs_get_stats(nvs_stats_t *out_stats);

/* Whole-store snapshot. The self-test saves the store before tests that write
 * NVS and restores it afterwards; restore rewrites the sector only if the
 * contents differ, so an unchanged store costs no flash wear. */
typedef struct {
    nvs_entry_t entries[NVS_MAX_ENTRIES];
    uint32_t    count;
} nvs_snapshot_t;

nvs_status_t nvs_snapshot_save(nvs_snapshot_t *out_snap);
nvs_status_t nvs_snapshot_restore(const nvs_snapshot_t *snap, bool *out_rewritten);
/* true if the live store holds exactly the snapshot's entries */
bool         nvs_snapshot_matches(const nvs_snapshot_t *snap);

/* Flash XIP Diagnostic Visualizers */
void nvs_print_stats(void);
void nvs_print_keys(void);

/* Golden Master System Health & Sealing Audit */
bool golden_master_verify(golden_master_report_t *out_report);
void golden_master_print_report(void);

/* Host Mock Interfaces */
#if !defined(__riscv)
void nvs_mock_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IRONV_NVS_H */
