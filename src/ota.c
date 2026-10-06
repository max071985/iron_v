/*
 * src/ota.c
 *
 * Dual-Slot Flash OTA Firmware Upgrade & Rollback Subsystem Implementation
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 */

#include "ota.h"
#include "section.h"
#include "flash_rom.h"
#include "string.h"
#include "console.h"
#include "utils.h"

#if defined(__riscv)
#include "interrupt.h"

/* ROM SPI Flash C API Prototypes */
typedef int esp_rom_spiflash_result_t;
extern esp_rom_spiflash_result_t esp_rom_spiflash_unlock(void);
extern void spi_flash_attach(uint32_t ishspi, bool legacy);

typedef struct {
    uint32_t device_id;
    uint32_t chip_size;       /* 8 MB = 0x00800000 */
    uint32_t block_size;      /* 64 KB = 0x00010000 */
    uint32_t sector_size;     /* 4 KB = 0x00001000 */
    uint32_t page_size;       /* 256 B = 0x00000100 */
    uint32_t status_mask;     /* 0xFFFF */
} esp_rom_spiflash_chip_t;

static esp_rom_spiflash_chip_t s_rom_flashchip = {
    .device_id   = 0x00000000U,
    .chip_size   = OTA_FLASH_TOTAL_SIZE,
    .block_size  = OTA_FLASH_BLOCK_SIZE,
    .sector_size = OTA_FLASH_SECTOR_SIZE,
    .page_size   = OTA_FLASH_PAGE_SIZE,
    .status_mask = 0xFFFFU
};
#endif

/* Standard IEEE 802.3 32-bit CRC Polynomial */
#define CRC32_POLYNOMIAL                (0xEDB88320U)

/* Static Subsystem State */
static ota_select_t        s_ota_records[2];
static bool                s_record_valid[2];
static ota_slot_t          s_active_slot = OTA_SLOT_0;
static ota_slot_t          s_fallback_slot = OTA_SLOT_0;
static ota_state_t         s_active_state = OTA_STATE_VALID;
static uint32_t            s_active_seq = 1U;
static uint32_t            s_active_sector_idx = 0U;
static ota_status_report_t s_telemetry;

#if !defined(__riscv)
/* In-Memory Mock Flash Simulation for Host Verification */
#define MOCK_FLASH_SECTOR_COUNT         (8U)
#define MOCK_FLASH_DATA_SEC0            (0U)
#define MOCK_FLASH_DATA_SEC1            (1U)
#define MOCK_FLASH_SLOT0_HDR            (2U)
#define MOCK_FLASH_SLOT1_HDR            (3U)

static uint8_t s_mock_flash[MOCK_FLASH_SECTOR_COUNT][OTA_FLASH_SECTOR_SIZE];
static bool s_mock_initialized = false;

static void mock_flash_ensure_init(void)
{
    if (!s_mock_initialized)
    {
        memset(s_mock_flash, 0xFF, sizeof(s_mock_flash));

        /* Populate mock valid ESP32-C6 image header in Slot 0 */
        esp_image_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = ESP_IMAGE_HEADER_MAGIC;
        hdr.segment_count = 4U;
        hdr.spi_mode = 2U; /* DIO */
        hdr.spi_speed_size = 0x30U; /* 80MHz, 8MB */
        hdr.entry_addr = ESP_IMAGE_DEFAULT_ENTRY_ADDR;
        hdr.wp_pin = 0xEEU;
        hdr.chip_id = ESP_IMAGE_CHIP_ID_ESP32C6;
        hdr.hash_appended = 1U;
        memcpy(&s_mock_flash[MOCK_FLASH_SLOT0_HDR][0], &hdr, sizeof(hdr));

        s_mock_initialized = true;
    }
}
#endif

/* Deterministic IEEE 802.3 CRC32 Calculation */
static uint32_t calc_crc32(const void *data, size_t len)
{
    if (data == NULL || len == 0U)
    {
        return 0U;
    }

    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0U; i < len; i++)
    {
        crc ^= (uint32_t)p[i];
        for (uint32_t b = 0U; b < 8U; b++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

/* Low-Level Flash Operations Abstraction */
static IRAM_ATTR ota_status_t flash_read(uint32_t offset, void *dest, size_t len)
{
    if (dest == NULL || len == 0U)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    s_telemetry.flash_reads++;

#if defined(__riscv)
    return (flash_rom_read(offset, dest, len) == FLASH_ROM_OK) ? OTA_OK : OTA_ERR_FLASH_IO;
#else
    mock_flash_ensure_init();
    uint32_t sec_idx = 0U;
    uint32_t sec_offset = 0U;

    if (offset >= OTA_DATA_SECTOR_0_OFFSET && offset < OTA_DATA_SECTOR_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_DATA_SEC0;
        sec_offset = offset - OTA_DATA_SECTOR_0_OFFSET;
    }
    else if (offset >= OTA_DATA_SECTOR_1_OFFSET && offset < OTA_DATA_SECTOR_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_DATA_SEC1;
        sec_offset = offset - OTA_DATA_SECTOR_1_OFFSET;
    }
    else if (offset < OTA_SLOT_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_SLOT0_HDR;
        sec_offset = offset - OTA_SLOT_0_OFFSET;
    }
    else if (offset >= OTA_SLOT_1_OFFSET && offset < OTA_SLOT_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_SLOT1_HDR;
        sec_offset = offset - OTA_SLOT_1_OFFSET;
    }
    else
    {
        memset(dest, 0xFF, len);
        return OTA_OK;
    }

    if (sec_offset + len > OTA_FLASH_SECTOR_SIZE)
    {
        return OTA_ERR_FLASH_IO;
    }

    memcpy(dest, &s_mock_flash[sec_idx][sec_offset], len);
    return OTA_OK;
#endif
}

static IRAM_ATTR ota_status_t flash_write(uint32_t offset, const void *src, size_t len)
{
    if (src == NULL || len == 0U)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    s_telemetry.flash_writes++;

#if defined(__riscv)
    return (flash_rom_write(offset, src, len) == FLASH_ROM_OK) ? OTA_OK : OTA_ERR_FLASH_IO;
#else
    mock_flash_ensure_init();
    uint32_t sec_idx = 0U;
    uint32_t sec_offset = 0U;

    if (offset >= OTA_DATA_SECTOR_0_OFFSET && offset < OTA_DATA_SECTOR_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_DATA_SEC0;
        sec_offset = offset - OTA_DATA_SECTOR_0_OFFSET;
    }
    else if (offset >= OTA_DATA_SECTOR_1_OFFSET && offset < OTA_DATA_SECTOR_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_DATA_SEC1;
        sec_offset = offset - OTA_DATA_SECTOR_1_OFFSET;
    }
    else if (offset < OTA_SLOT_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_SLOT0_HDR;
        sec_offset = offset - OTA_SLOT_0_OFFSET;
    }
    else if (offset >= OTA_SLOT_1_OFFSET && offset < OTA_SLOT_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_SLOT1_HDR;
        sec_offset = offset - OTA_SLOT_1_OFFSET;
    }
    else
    {
        return OTA_OK;
    }

    if (sec_offset + len > OTA_FLASH_SECTOR_SIZE)
    {
        return OTA_ERR_FLASH_IO;
    }

    const uint8_t *in = (const uint8_t *)src;
    for (size_t i = 0U; i < len; i++)
    {
        s_mock_flash[sec_idx][sec_offset + i] &= in[i];
    }
    return OTA_OK;
#endif
}

static IRAM_ATTR ota_status_t flash_erase_sector(uint32_t offset)
{
    s_telemetry.flash_erases++;

#if defined(__riscv)
    return (flash_rom_erase_sector(offset) == FLASH_ROM_OK) ? OTA_OK : OTA_ERR_FLASH_IO;
#else
    mock_flash_ensure_init();
    uint32_t sec_idx = 0U;

    if (offset >= OTA_DATA_SECTOR_0_OFFSET && offset < OTA_DATA_SECTOR_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_DATA_SEC0;
    }
    else if (offset >= OTA_DATA_SECTOR_1_OFFSET && offset < OTA_DATA_SECTOR_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_DATA_SEC1;
    }
    else if (offset < OTA_SLOT_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_SLOT0_HDR;
    }
    else if (offset >= OTA_SLOT_1_OFFSET && offset < OTA_SLOT_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
    {
        sec_idx = MOCK_FLASH_SLOT1_HDR;
    }
    else
    {
        return OTA_OK;
    }

    memset(&s_mock_flash[sec_idx][0], 0xFF, OTA_FLASH_SECTOR_SIZE);
    return OTA_OK;
#endif
}

/* Validate Selection Record Integrity */
static bool is_record_valid(const ota_select_t *rec)
{
    if (rec == NULL)
    {
        return false;
    }

    if (rec->magic != OTA_SELECT_MAGIC)
    {
        return false;
    }

    if (rec->slot != (uint32_t)OTA_SLOT_0 && rec->slot != (uint32_t)OTA_SLOT_1)
    {
        return false;
    }

    /* Compute CRC32 over the first 60 bytes of the structure */
    uint32_t expected_crc = calc_crc32(rec, offsetof(ota_select_t, crc32));
    return (rec->crc32 == expected_crc);
}

/* Commit Selection Record to Flash Sector */
static ota_status_t write_select_record(uint32_t sector_idx, const ota_select_t *rec)
{
    if (sector_idx > 1U || rec == NULL)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    s_ota_records[sector_idx] = *rec;
    s_record_valid[sector_idx] = true;

    uint32_t offset = (sector_idx == 0U) ? OTA_DATA_SECTOR_0_OFFSET : OTA_DATA_SECTOR_1_OFFSET;

    ota_status_t res = flash_erase_sector(offset);
    if (res == OTA_OK)
    {
        (void)flash_write(offset, rec, sizeof(ota_select_t));
    }

    return OTA_OK;
}

/* Derive the active slot/state from the two selection records (a blank flash
 * gets a factory record for Slot 0) */
static void ota_select_active_record(void)
{
    if (!s_record_valid[0] && !s_record_valid[1])
    {
        /* Virgin Flash: default to Slot 0 as Factory Slot */
        ota_select_t initial_rec;
        memset(&initial_rec, 0, sizeof(initial_rec));
        initial_rec.magic = OTA_SELECT_MAGIC;
        initial_rec.seq = 1U;
        initial_rec.slot = (uint32_t)OTA_SLOT_0;
        initial_rec.state = (uint32_t)OTA_STATE_VALID;
        initial_rec.version_major = 1U;
        initial_rec.version_minor = 0U;
        initial_rec.version_patch = 0U;
        initial_rec.crc32 = calc_crc32(&initial_rec, offsetof(ota_select_t, crc32));

        write_select_record(0U, &initial_rec);

        s_active_slot = OTA_SLOT_0;
        s_fallback_slot = OTA_SLOT_0;
        s_active_state = OTA_STATE_VALID;
        s_active_seq = 1U;
        s_active_sector_idx = 0U;
    }
    else if (s_record_valid[0] && !s_record_valid[1])
    {
        s_active_slot = (ota_slot_t)s_ota_records[0].slot;
        s_fallback_slot = s_active_slot;
        s_active_state = (ota_state_t)s_ota_records[0].state;
        s_active_seq = s_ota_records[0].seq;
        s_active_sector_idx = 0U;
    }
    else if (!s_record_valid[0] && s_record_valid[1])
    {
        s_active_slot = (ota_slot_t)s_ota_records[1].slot;
        s_fallback_slot = s_active_slot;
        s_active_state = (ota_state_t)s_ota_records[1].state;
        s_active_seq = s_ota_records[1].seq;
        s_active_sector_idx = 1U;
    }
    else
    {
        /* Both records valid: choose the higher sequence number */
        if (s_ota_records[0].seq >= s_ota_records[1].seq)
        {
            s_active_slot = (ota_slot_t)s_ota_records[0].slot;
            s_fallback_slot = (ota_slot_t)s_ota_records[1].slot;
            s_active_state = (ota_state_t)s_ota_records[0].state;
            s_active_seq = s_ota_records[0].seq;
            s_active_sector_idx = 0U;
        }
        else
        {
            s_active_slot = (ota_slot_t)s_ota_records[1].slot;
            s_fallback_slot = (ota_slot_t)s_ota_records[0].slot;
            s_active_state = (ota_state_t)s_ota_records[1].state;
            s_active_seq = s_ota_records[1].seq;
            s_active_sector_idx = 1U;
        }
    }
}

/* Public Subsystem Initialization */
ota_status_t ota_init(void)
{
#if defined(__riscv)
    *(esp_rom_spiflash_chip_t **)0x4087ffec = &s_rom_flashchip;
    spi_flash_attach(0, false);
    esp_rom_spiflash_unlock();
#endif

    memset(&s_telemetry, 0, sizeof(s_telemetry));
    memset(s_ota_records, 0, sizeof(s_ota_records));
    s_record_valid[0] = false;
    s_record_valid[1] = false;

    /* Read both OTA Selection Sectors */
    flash_read(OTA_DATA_SECTOR_0_OFFSET, &s_ota_records[0], sizeof(ota_select_t));
    flash_read(OTA_DATA_SECTOR_1_OFFSET, &s_ota_records[1], sizeof(ota_select_t));

    s_record_valid[0] = is_record_valid(&s_ota_records[0]);
    s_record_valid[1] = is_record_valid(&s_ota_records[1]);

    ota_select_active_record();

    /* Auto-Rollback check if active state is TESTING */
    if (s_active_state == OTA_STATE_TESTING)
    {
        s_telemetry.boot_attempts++;
        if (s_telemetry.boot_attempts > OTA_MAX_TEST_BOOTS)
        {
            ota_rollback();
        }
    }

    return OTA_OK;
}

ota_status_t ota_snapshot_save(ota_snapshot_t *out_snap)
{
    if (out_snap == NULL)
    {
        return OTA_ERR_INVALID_PARAM;
    }
    for (uint32_t i = 0U; i < 2U; i++)
    {
        out_snap->records[i] = s_ota_records[i];
        out_snap->valid[i] = s_record_valid[i];
    }
    return OTA_OK;
}

static bool ota_record_matches(const ota_snapshot_t *snap, uint32_t idx)
{
    if (snap->valid[idx] != s_record_valid[idx])
    {
        return false;
    }
    return !snap->valid[idx] ||
           (memcmp(&snap->records[idx], &s_ota_records[idx], sizeof(ota_select_t)) == 0);
}

bool ota_snapshot_matches(const ota_snapshot_t *snap)
{
    return (snap != NULL) && ota_record_matches(snap, 0U) && ota_record_matches(snap, 1U);
}

ota_status_t ota_snapshot_restore(const ota_snapshot_t *snap, bool *out_rewritten)
{
    if (out_rewritten != NULL)
    {
        *out_rewritten = false;
    }
    if (snap == NULL)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    bool rewritten = false;
    for (uint32_t i = 0U; i < 2U; i++)
    {
        if (ota_record_matches(snap, i))
        {
            continue;
        }
        rewritten = true;
        if (snap->valid[i])
        {
            write_select_record(i, &snap->records[i]);
        }
        else
        {
            uint32_t offset = (i == 0U) ? OTA_DATA_SECTOR_0_OFFSET : OTA_DATA_SECTOR_1_OFFSET;
            (void)flash_erase_sector(offset);
            s_ota_records[i] = snap->records[i];
            s_record_valid[i] = false;
        }
    }

    if (rewritten)
    {
        ota_select_active_record();
    }
    if (out_rewritten != NULL)
    {
        *out_rewritten = rewritten;
    }
    return OTA_OK;
}

ota_slot_t ota_get_active_slot(void)
{
    return s_active_slot;
}

ota_slot_t ota_get_inactive_slot(void)
{
    return (s_active_slot == OTA_SLOT_0) ? OTA_SLOT_1 : OTA_SLOT_0;
}

ota_state_t ota_get_slot_state(ota_slot_t slot)
{
    if (slot == s_active_slot)
    {
        return s_active_state;
    }
    ota_slot_t inact = ota_get_inactive_slot();
    if (slot == inact)
    {
        uint32_t inact_idx = 1U - s_active_sector_idx;
        if (s_record_valid[inact_idx] && (ota_slot_t)s_ota_records[inact_idx].slot == slot)
        {
            return (ota_state_t)s_ota_records[inact_idx].state;
        }
    }
    return OTA_STATE_UNINITIALIZED;
}

ota_status_t ota_get_partition_info(ota_slot_t slot, ota_partition_t *out_part)
{
    if (out_part == NULL || slot == OTA_SLOT_INVALID || slot >= OTA_SLOT_COUNT)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    out_part->slot = slot;
    out_part->phys_offset = (slot == OTA_SLOT_0) ? OTA_SLOT_0_OFFSET : OTA_SLOT_1_OFFSET;
    out_part->size_bytes = (slot == OTA_SLOT_0) ? OTA_SLOT_0_SIZE : OTA_SLOT_1_SIZE;
    out_part->is_active = (slot == s_active_slot);
    out_part->state = ota_get_slot_state(slot);
    out_part->seq = (slot == s_active_slot) ? s_active_seq : 0U;

    return OTA_OK;
}

ota_status_t ota_parse_image_header(const uint8_t *data, size_t len, esp_image_header_t *out_hdr)
{
    if (data == NULL || out_hdr == NULL || len < ESP_IMAGE_HEADER_SIZE)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    if (data[0] != ESP_IMAGE_HEADER_MAGIC)
    {
        return OTA_ERR_INVALID_IMAGE;
    }

    const esp_image_header_t *img = (const esp_image_header_t *)data;

    if (img->segment_count == 0U || img->segment_count > ESP_IMAGE_MAX_SEGMENTS)
    {
        return OTA_ERR_INVALID_IMAGE;
    }

    if (img->chip_id != ESP_IMAGE_CHIP_ID_ESP32C6)
    {
        return OTA_ERR_INVALID_IMAGE;
    }

    memcpy(out_hdr, img, sizeof(esp_image_header_t));
    return OTA_OK;
}

ota_status_t ota_verify_image(ota_slot_t slot, esp_image_header_t *out_hdr)
{
    if (slot == OTA_SLOT_INVALID || slot >= OTA_SLOT_COUNT)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    uint32_t offset = (slot == OTA_SLOT_0) ? OTA_SLOT_0_OFFSET : OTA_SLOT_1_OFFSET;
    uint8_t hdr_buf[ESP_IMAGE_HEADER_SIZE];

    ota_status_t res = flash_read(offset, hdr_buf, sizeof(hdr_buf));
    if (res != OTA_OK)
    {
        return res;
    }

    esp_image_header_t temp_hdr;
    res = ota_parse_image_header(hdr_buf, sizeof(hdr_buf), &temp_hdr);
    if (res != OTA_OK)
    {
        return res;
    }

    if (out_hdr != NULL)
    {
        *out_hdr = temp_hdr;
    }

    s_telemetry.verified_images++;
    return OTA_OK;
}

ota_status_t ota_switch_slot(ota_slot_t slot)
{
    if (slot == OTA_SLOT_INVALID || slot >= OTA_SLOT_COUNT)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    if (slot == s_active_slot)
    {
        return OTA_ERR_ALREADY_ACTIVE;
    }

    uint32_t next_sector_idx = 1U - s_active_sector_idx;
    ota_select_t new_rec;
    memset(&new_rec, 0, sizeof(new_rec));
    new_rec.magic = OTA_SELECT_MAGIC;
    new_rec.seq = s_active_seq + 1U;
    new_rec.slot = (uint32_t)slot;
    new_rec.state = (uint32_t)OTA_STATE_TESTING;
    new_rec.version_major = 1U;
    new_rec.version_minor = 1U;
    new_rec.version_patch = 0U;
    new_rec.boot_count = 0U;
    new_rec.crc32 = calc_crc32(&new_rec, offsetof(ota_select_t, crc32));

    ota_status_t res = write_select_record(next_sector_idx, &new_rec);
    if (res != OTA_OK)
    {
        return res;
    }

    s_fallback_slot = s_active_slot;
    s_active_slot = slot;
    s_active_state = OTA_STATE_TESTING;
    s_active_seq = new_rec.seq;
    s_active_sector_idx = next_sector_idx;
    s_telemetry.total_switches++;

    return OTA_OK;
}

ota_status_t ota_mark_valid(void)
{
    if (s_active_state == OTA_STATE_VALID)
    {
        return OTA_OK;
    }

    uint32_t next_sector_idx = 1U - s_active_sector_idx;
    ota_select_t valid_rec;
    memset(&valid_rec, 0, sizeof(valid_rec));
    valid_rec.magic = OTA_SELECT_MAGIC;
    valid_rec.seq = s_active_seq + 1U;
    valid_rec.slot = (uint32_t)s_active_slot;
    valid_rec.state = (uint32_t)OTA_STATE_VALID;
    valid_rec.version_major = 1U;
    valid_rec.version_minor = 0U;
    valid_rec.version_patch = 0U;
    valid_rec.crc32 = calc_crc32(&valid_rec, offsetof(ota_select_t, crc32));

    ota_status_t res = write_select_record(next_sector_idx, &valid_rec);
    if (res != OTA_OK)
    {
        return res;
    }

    s_active_state = OTA_STATE_VALID;
    s_active_seq = valid_rec.seq;
    s_active_sector_idx = next_sector_idx;
    return OTA_OK;
}

ota_status_t ota_rollback(void)
{
    ota_slot_t target = (s_active_slot == OTA_SLOT_0) ? OTA_SLOT_1 : OTA_SLOT_0;
    uint32_t next_sector_idx = 1U - s_active_sector_idx;

    ota_select_t rollback_rec;
    memset(&rollback_rec, 0, sizeof(rollback_rec));
    rollback_rec.magic = OTA_SELECT_MAGIC;
    rollback_rec.seq = s_active_seq + 1U;
    rollback_rec.slot = (uint32_t)target;
    rollback_rec.state = (uint32_t)OTA_STATE_VALID;
    rollback_rec.version_major = 1U;
    rollback_rec.version_minor = 0U;
    rollback_rec.version_patch = 0U;
    rollback_rec.crc32 = calc_crc32(&rollback_rec, offsetof(ota_select_t, crc32));

    ota_status_t res = write_select_record(next_sector_idx, &rollback_rec);
    if (res != OTA_OK)
    {
        return res;
    }

    s_fallback_slot = s_active_slot;
    s_active_slot = target;
    s_active_state = OTA_STATE_VALID;
    s_active_seq = rollback_rec.seq;
    s_active_sector_idx = next_sector_idx;
    s_telemetry.total_rollbacks++;

    return OTA_OK;
}

ota_status_t ota_get_status(ota_status_report_t *out_report)
{
    if (out_report == NULL)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    s_telemetry.active_slot = s_active_slot;
    s_telemetry.inactive_slot = ota_get_inactive_slot();
    s_telemetry.active_state = s_active_state;
    s_telemetry.active_seq = s_active_seq;
    s_telemetry.rollback_possible = (s_active_slot != s_fallback_slot || s_active_state == OTA_STATE_TESTING);

    *out_report = s_telemetry;
    return OTA_OK;
}

ota_status_t ota_read_slot(ota_slot_t slot, uint32_t offset, void *dest, size_t len)
{
    if (slot == OTA_SLOT_INVALID || slot >= OTA_SLOT_COUNT || dest == NULL || len == 0U)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    uint32_t base = (slot == OTA_SLOT_0) ? OTA_SLOT_0_OFFSET : OTA_SLOT_1_OFFSET;
    uint32_t max_size = (slot == OTA_SLOT_0) ? OTA_SLOT_0_SIZE : OTA_SLOT_1_SIZE;

    if (offset + len > max_size)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    return flash_read(base + offset, dest, len);
}

ota_status_t ota_write_chunk(ota_slot_t slot, uint32_t offset, const void *data, size_t len)
{
    if (slot == OTA_SLOT_INVALID || slot >= OTA_SLOT_COUNT || data == NULL || len == 0U)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    /* Never write into active running slot! */
    if (slot == s_active_slot)
    {
        return OTA_ERR_NOT_PERMITTED;
    }

    uint32_t base = (slot == OTA_SLOT_0) ? OTA_SLOT_0_OFFSET : OTA_SLOT_1_OFFSET;
    uint32_t max_size = (slot == OTA_SLOT_0) ? OTA_SLOT_0_SIZE : OTA_SLOT_1_SIZE;

    if (offset + len > max_size)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    return flash_write(base + offset, data, len);
}

ota_status_t ota_erase_slot(ota_slot_t slot)
{
    if (slot == OTA_SLOT_INVALID || slot >= OTA_SLOT_COUNT)
    {
        return OTA_ERR_INVALID_PARAM;
    }

    /* Guard active running slot from accidental erasure */
    if (slot == s_active_slot)
    {
        return OTA_ERR_NOT_PERMITTED;
    }

    uint32_t base = (slot == OTA_SLOT_0) ? OTA_SLOT_0_OFFSET : OTA_SLOT_1_OFFSET;
    return flash_erase_sector(base);
}

/* Flash XIP Diagnostic Visualizers */
__attribute__((section(".flash.text")))
void ota_print_status(void)
{
    ota_status_report_t rep;
    ota_get_status(&rep);

    console_puts("================ Dual-Slot Flash OTA Telemetry ================\r\n");
    console_puts("  Active Slot:        Slot ");
    put_dec(rep.active_slot);
    console_puts(" (Seq: ");
    put_dec(rep.active_seq);
    console_puts(", State: ");
    if (rep.active_state == OTA_STATE_VALID) console_puts("VALID)\r\n");
    else if (rep.active_state == OTA_STATE_TESTING) console_puts("TESTING)\r\n");
    else if (rep.active_state == OTA_STATE_ABORTED) console_puts("ABORTED)\r\n");
    else console_puts("UNKNOWN)\r\n");

    console_puts("  Inactive Slot:      Slot ");
    put_dec(rep.inactive_slot);
    console_puts("\r\n");

    console_puts("  Total Switches:     ");
    put_dec(rep.total_switches);
    console_puts(" (Rollbacks: ");
    put_dec(rep.total_rollbacks);
    console_puts(")\r\n");

    console_puts("  Verified Images:    ");
    put_dec(rep.verified_images);
    console_puts("\r\n");

    console_puts("  Flash I/O Stats:    Reads=");
    put_dec(rep.flash_reads);
    console_puts(", Writes=");
    put_dec(rep.flash_writes);
    console_puts(", Erases=");
    put_dec(rep.flash_erases);
    console_puts("\r\n");

    console_puts("  Rollback Ready:     ");
    console_puts(rep.rollback_possible ? "YES\r\n" : "NO\r\n");
    console_puts("===============================================================\r\n");
}

__attribute__((section(".flash.text")))
void ota_print_partitions(void)
{
    console_puts("================ Flash Partition Geometry (8 MB) ================\r\n");
    console_puts("  [0x000000 - 0x3C0000] (3.75 MB) Slot 0: Primary/Factory App ");
    console_puts((s_active_slot == OTA_SLOT_0) ? "[ACTIVE]\r\n" : "[INACTIVE]\r\n");
    console_puts("  [0x3C0000 - 0x780000] (3.75 MB) Slot 1: Secondary/Upgrade App ");
    console_puts((s_active_slot == OTA_SLOT_1) ? "[ACTIVE]\r\n" : "[INACTIVE]\r\n");
    console_puts("  [0x780000 - 0x790000] ( 64 KB)  ota_data: Redundant Control Sectors\r\n");
    console_puts("  [0x790000 - 0x800000] (448 KB)  storage:  NVS Key-Value & Config\r\n");
    console_puts("=================================================================\r\n");
}

/* Host Testing Mocks */
#if !defined(__riscv)
void ota_mock_reset(void)
{
    s_mock_initialized = false;
    mock_flash_ensure_init();
    ota_init();
}

void ota_mock_set_active_slot(ota_slot_t slot, uint32_t seq, ota_state_t state)
{
    s_active_slot = slot;
    s_active_seq = seq;
    s_active_state = state;
}

void ota_mock_corrupt_select(uint32_t sector_idx)
{
    if (sector_idx < 2U)
    {
        uint32_t sec = (sector_idx == 0U) ? MOCK_FLASH_DATA_SEC0 : MOCK_FLASH_DATA_SEC1;
        memset(&s_mock_flash[sec][0], 0xAA, sizeof(ota_select_t));
    }
}

uint8_t *ota_mock_get_flash_ptr(uint32_t offset)
{
    mock_flash_ensure_init();
    if (offset >= OTA_DATA_SECTOR_0_OFFSET && offset < OTA_DATA_SECTOR_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
        return &s_mock_flash[MOCK_FLASH_DATA_SEC0][offset - OTA_DATA_SECTOR_0_OFFSET];
    if (offset >= OTA_DATA_SECTOR_1_OFFSET && offset < OTA_DATA_SECTOR_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
        return &s_mock_flash[MOCK_FLASH_DATA_SEC1][offset - OTA_DATA_SECTOR_1_OFFSET];
    if (offset < OTA_SLOT_0_OFFSET + OTA_FLASH_SECTOR_SIZE)
        return &s_mock_flash[MOCK_FLASH_SLOT0_HDR][offset - OTA_SLOT_0_OFFSET];
    if (offset >= OTA_SLOT_1_OFFSET && offset < OTA_SLOT_1_OFFSET + OTA_FLASH_SECTOR_SIZE)
        return &s_mock_flash[MOCK_FLASH_SLOT1_HDR][offset - OTA_SLOT_1_OFFSET];
    return NULL;
}
#endif
