/*
 * src/efuse.h
 *
 * eFuse Memory Controller & Silicon Security Sealing Subsystem
 * Espressif ESP32-C6 RISC-V SoC (TRM Chapter 29 & SVD)
 */

#ifndef IRONV_EFUSE_H
#define IRONV_EFUSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "regs/efuse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return Status Codes */
typedef enum {
    EFUSE_OK                  = 0,
    EFUSE_ERR_INVALID_PARAM   = -1,
    EFUSE_ERR_TIMEOUT         = -2,
    EFUSE_ERR_NOT_READY       = -3,
    EFUSE_ERR_CORRUPT         = -4
} efuse_status_t;

/* eFuse Subsystem Telemetry Snapshot */
typedef struct {
    uint8_t  mac[EFUSE_MAC_LEN];
    uint16_t mac_ext;
    uint8_t  unique_id[EFUSE_UNIQUE_ID_LEN];
    uint32_t wafer_version_major;
    uint32_t wafer_version_minor;
    uint32_t pkg_version;
    uint32_t blk_version_major;
    uint32_t blk_version_minor;
    bool     secure_boot_en;
    bool     secure_boot_aggressive_revoke;
    bool     flash_encryption_en;
    uint32_t flash_crypt_cnt;
    bool     jtag_pad_disabled;
    bool     jtag_usb_disabled;
    bool     jtag_soft_disabled;
    bool     download_mode_disabled;
    bool     security_download_enabled;
    uint32_t sec_dpa_level;
    bool     crypt_dpa_enabled;
    uint32_t wr_dis;
    uint32_t rd_dis;
    uint32_t read_count;
} efuse_telemetry_t;

/* Core Subsystem Lifecycle & Query APIs */
int      efuse_init(void);
int      efuse_refresh_shadow(void);
int      efuse_get_mac(uint8_t out_mac[EFUSE_MAC_LEN]);
int      efuse_get_mac_ext(uint16_t *out_ext);
int      efuse_get_unique_id(uint8_t out_uid[EFUSE_UNIQUE_ID_LEN]);
int      efuse_get_chip_version(uint32_t *out_major, uint32_t *out_minor);
uint32_t efuse_get_pkg_version(void);
bool     efuse_is_secure_boot_enabled(void);
bool     efuse_is_flash_encryption_enabled(void);
bool     efuse_is_jtag_disabled(void);
bool     efuse_is_download_mode_disabled(void);
uint32_t efuse_get_wr_dis(void);
uint32_t efuse_get_rd_dis(void);
int      efuse_get_telemetry(efuse_telemetry_t *out_telem);

/* Interactive Console / Diagnostic Visualizers */
void     efuse_print_summary(void);
void     efuse_print_security(void);
void     efuse_print_mac(void);

#if !defined(__riscv)
/* Freestanding Host Emulation Mocks */
void     efuse_mock_reset(void);
void     efuse_mock_set_reg(uint32_t offset, uint32_t val);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IRONV_EFUSE_H */
