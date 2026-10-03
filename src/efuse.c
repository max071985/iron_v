/*
 * src/efuse.c
 *
 * eFuse Memory Controller & Silicon Security Sealing Subsystem Implementation
 * Espressif ESP32-C6 RISC-V SoC (TRM Chapter 29 & SVD)
 */

#include "efuse.h"
#include "console.h"
#include "utils.h"
#include "string.h"

/* Operational Timeout Limit for Hardware Shadow Refresh */
#define EFUSE_POLL_TIMEOUT_CYCLES       (50000U)

/* Static Subsystem Execution State */
static uint32_t s_efuse_read_count = 0U;

#if !defined(__riscv)
/* ========================================================================= */
/* Freestanding Host Emulation Register Space                                */
/* ========================================================================= */

#define MOCK_EFUSE_WORDS (0x200U / 4U)
static uint32_t s_mock_regs[MOCK_EFUSE_WORDS];

void efuse_mock_reset(void)
{
    for (size_t i = 0; i < MOCK_EFUSE_WORDS; i++)
    {
        s_mock_regs[i] = 0U;
    }

    /* Bench Board Ground-Truth Values */
    s_mock_regs[0x2CU / 4U] = 0x00000000U; /* RD_WR_DIS */
    s_mock_regs[0x30U / 4U] = 0x00000000U; /* RD_REPEAT_DATA0 */
    s_mock_regs[0x34U / 4U] = 0x00000000U; /* RD_REPEAT_DATA1 */
    s_mock_regs[0x38U / 4U] = 0x00000000U; /* RD_REPEAT_DATA2 */
    s_mock_regs[0x3CU / 4U] = 0x00000000U; /* RD_REPEAT_DATA3 */
    s_mock_regs[0x40U / 4U] = 0x00000000U; /* RD_REPEAT_DATA4 */
    s_mock_regs[0x44U / 4U] = 0xCA451E14U; /* RD_MAC_SPI_SYS_0: 40:4c:ca:45:1e:14 */
    s_mock_regs[0x48U / 4U] = 0xFFFE404CU; /* RD_MAC_SPI_SYS_1: ext=fffe, mac=40:4c */
    s_mock_regs[0x50U / 4U] = (1U << 27);  /* RD_MAC_SPI_SYS_3: BLK_VERSION_MINOR=1 */
    s_mock_regs[0x5CU / 4U] = 0x68142BE6U; /* BLOCK2 UID Word 0 */
    s_mock_regs[0x60U / 4U] = 0xDCA210BAU; /* BLOCK2 UID Word 1 */
    s_mock_regs[0x64U / 4U] = 0x1FEE2DC6U; /* BLOCK2 UID Word 2 */
    s_mock_regs[0x68U / 4U] = 0x5168CF81U; /* BLOCK2 UID Word 3 */
    s_mock_regs[0x1D0U / 4U] = EFUSE_STATE_READ_DONE; /* STATUS */
    s_efuse_read_count = 0U;
}

void efuse_mock_set_reg(uint32_t offset, uint32_t val)
{
    if ((offset / 4U) < MOCK_EFUSE_WORDS)
    {
        s_mock_regs[offset / 4U] = val;
    }
}

static inline uint32_t efuse_read_mmio(uint32_t offset)
{
    if ((offset / 4U) < MOCK_EFUSE_WORDS)
    {
        return s_mock_regs[offset / 4U];
    }
    return 0U;
}

static inline void efuse_write_mmio(uint32_t offset, uint32_t val)
{
    if ((offset / 4U) < MOCK_EFUSE_WORDS)
    {
        s_mock_regs[offset / 4U] = val;
    }
}

#else

/* Physical Silicon MMIO Accessors */
static inline uint32_t efuse_read_mmio(uint32_t offset)
{
    return *((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + offset));
}

static inline void efuse_write_mmio(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + offset)) = val;
}

#endif /* !defined(__riscv) */

/* ========================================================================= */
/* Core Subsystem APIs                                                       */
/* ========================================================================= */

int efuse_refresh_shadow(void)
{
#if defined(__riscv)
    /*
     * The ESP32-C6 hardware ROM bootloader automatically latches all physical
     * eFuse OTP bits into the memory-mapped shadow registers (0x600B082C - 0x600B0978)
     * at system power-on and reset. We verify that the controller command register
     * is in an idle state (no active read or program command pending).
     */
    uint32_t timeout = EFUSE_POLL_TIMEOUT_CYCLES;
    while (timeout > 0U)
    {
        uint32_t cmd = efuse_read_mmio(0x1D4U);
        if ((cmd & (EFUSE_CMD_READ_CMD_M | EFUSE_CMD_PGM_CMD_M)) == 0U)
        {
            break;
        }
        timeout--;
    }

    if (timeout == 0U)
    {
        return EFUSE_ERR_TIMEOUT;
    }
#endif

    s_efuse_read_count++;
    return EFUSE_OK;
}

int efuse_init(void)
{
#if !defined(__riscv)
    efuse_mock_reset();
#endif

    int ret = efuse_refresh_shadow();
    if (ret != EFUSE_OK)
    {
        return ret;
    }

    return EFUSE_OK;
}

int efuse_get_mac(uint8_t out_mac[EFUSE_MAC_LEN])
{
    if (out_mac == NULL)
    {
        return EFUSE_ERR_INVALID_PARAM;
    }

    uint32_t mac0 = efuse_read_mmio(0x44U); /* EFUSE_RD_MAC_SPI_SYS_0_REG */
    uint32_t mac1 = efuse_read_mmio(0x48U); /* EFUSE_RD_MAC_SPI_SYS_1_REG */

    out_mac[0] = (uint8_t)((mac1 >> 8U) & 0xFFU);
    out_mac[1] = (uint8_t)(mac1 & 0xFFU);
    out_mac[2] = (uint8_t)((mac0 >> 24U) & 0xFFU);
    out_mac[3] = (uint8_t)((mac0 >> 16U) & 0xFFU);
    out_mac[4] = (uint8_t)((mac0 >> 8U) & 0xFFU);
    out_mac[5] = (uint8_t)(mac0 & 0xFFU);

    return EFUSE_OK;
}

int efuse_get_mac_ext(uint16_t *out_ext)
{
    if (out_ext == NULL)
    {
        return EFUSE_ERR_INVALID_PARAM;
    }

    uint32_t mac1 = efuse_read_mmio(0x48U);
    *out_ext = (uint16_t)((mac1 & EFUSE_RD_MAC_SPI_SYS_1_MAC_EXT_M) >> EFUSE_RD_MAC_SPI_SYS_1_MAC_EXT_S);
    return EFUSE_OK;
}

int efuse_get_unique_id(uint8_t out_uid[EFUSE_UNIQUE_ID_LEN])
{
    if (out_uid == NULL)
    {
        return EFUSE_ERR_INVALID_PARAM;
    }

    /* BLOCK2 Optional Unique ID spans 128 bits across data registers 0..3 */
    for (uint32_t w = 0U; w < 4U; w++)
    {
        uint32_t val = efuse_read_mmio(0x5CU + (w * 4U));
        out_uid[w * 4U + 0U] = (uint8_t)(val & 0xFFU);
        out_uid[w * 4U + 1U] = (uint8_t)((val >> 8U) & 0xFFU);
        out_uid[w * 4U + 2U] = (uint8_t)((val >> 16U) & 0xFFU);
        out_uid[w * 4U + 3U] = (uint8_t)((val >> 24U) & 0xFFU);
    }

    return EFUSE_OK;
}

int efuse_get_chip_version(uint32_t *out_major, uint32_t *out_minor)
{
    if (out_major == NULL || out_minor == NULL)
    {
        return EFUSE_ERR_INVALID_PARAM;
    }

    uint32_t sys3 = efuse_read_mmio(0x50U); /* EFUSE_RD_MAC_SPI_SYS_3_REG */
    *out_major = (sys3 & EFUSE_WAFER_VERSION_MAJOR_M) >> EFUSE_WAFER_VERSION_MAJOR_S;
    *out_minor = (sys3 & EFUSE_WAFER_VERSION_MINOR_M) >> EFUSE_WAFER_VERSION_MINOR_S;

    return EFUSE_OK;
}

uint32_t efuse_get_pkg_version(void)
{
    uint32_t sys3 = efuse_read_mmio(0x50U);
    return (sys3 & EFUSE_PKG_VERSION_M) >> EFUSE_PKG_VERSION_S;
}

bool efuse_is_secure_boot_enabled(void)
{
    uint32_t rpt2 = efuse_read_mmio(0x38U); /* EFUSE_RD_REPEAT_DATA2_REG */
    return (rpt2 & EFUSE_SECURE_BOOT_EN_BIT) != 0U;
}

bool efuse_is_flash_encryption_enabled(void)
{
    uint32_t rpt1 = efuse_read_mmio(0x34U); /* EFUSE_RD_REPEAT_DATA1_REG */
    uint32_t cnt = (rpt1 & EFUSE_SPI_BOOT_CRYPT_CNT_M) >> EFUSE_SPI_BOOT_CRYPT_CNT_S;

    /* Flash encryption is enabled when odd number of bits are set (1 or 3 bits) */
    uint32_t ones = (cnt & 1U) + ((cnt >> 1U) & 1U) + ((cnt >> 2U) & 1U);
    return (ones % 2U) != 0U;
}

bool efuse_is_jtag_disabled(void)
{
    uint32_t rpt0 = efuse_read_mmio(0x30U); /* EFUSE_RD_REPEAT_DATA0_REG */

    /* Hard permanent disable via PAD or USB switch */
    if ((rpt0 & EFUSE_DIS_PAD_JTAG_BIT) != 0U ||
        (rpt0 & EFUSE_DIS_USB_JTAG_BIT) != 0U)
    {
        return true;
    }

    /* Soft disable if SOFT_DIS_JTAG odd number */
    uint32_t soft = (rpt0 & EFUSE_SOFT_DIS_JTAG_M) >> EFUSE_SOFT_DIS_JTAG_S;
    if ((soft % 2U) != 0U)
    {
        return true;
    }

    return false;
}

bool efuse_is_download_mode_disabled(void)
{
    uint32_t rpt3 = efuse_read_mmio(0x3CU); /* EFUSE_RD_REPEAT_DATA3_REG */
    return (rpt3 & EFUSE_DIS_DOWNLOAD_MODE_BIT) != 0U;
}

uint32_t efuse_get_wr_dis(void)
{
    return efuse_read_mmio(0x2CU); /* EFUSE_RD_WR_DIS_REG */
}

uint32_t efuse_get_rd_dis(void)
{
    uint32_t rpt0 = efuse_read_mmio(0x30U);
    return rpt0 & EFUSE_RD_REPEAT_DATA0_RD_DIS_M;
}

int efuse_get_telemetry(efuse_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return EFUSE_ERR_INVALID_PARAM;
    }

    efuse_get_mac(out_telem->mac);
    efuse_get_mac_ext(&out_telem->mac_ext);
    efuse_get_unique_id(out_telem->unique_id);
    efuse_get_chip_version(&out_telem->wafer_version_major, &out_telem->wafer_version_minor);
    out_telem->pkg_version = efuse_get_pkg_version();

    uint32_t sys3 = efuse_read_mmio(0x50U);
    out_telem->blk_version_minor = (sys3 & EFUSE_BLK_VERSION_MINOR_M) >> EFUSE_BLK_VERSION_MINOR_S;
    out_telem->blk_version_major = (sys3 & EFUSE_BLK_VERSION_MAJOR_M) >> EFUSE_BLK_VERSION_MAJOR_S;

    out_telem->secure_boot_en = efuse_is_secure_boot_enabled();

    uint32_t rpt2 = efuse_read_mmio(0x38U);
    out_telem->secure_boot_aggressive_revoke = (rpt2 & EFUSE_SECURE_BOOT_AGG_REVOKE_BIT) != 0U;
    out_telem->sec_dpa_level = (rpt2 & EFUSE_SEC_DPA_LEVEL_M) >> EFUSE_SEC_DPA_LEVEL_S;
    out_telem->crypt_dpa_enabled = (rpt2 & EFUSE_CRYPT_DPA_ENABLE_BIT) != 0U;

    uint32_t rpt1 = efuse_read_mmio(0x34U);
    out_telem->flash_crypt_cnt = (rpt1 & EFUSE_SPI_BOOT_CRYPT_CNT_M) >> EFUSE_SPI_BOOT_CRYPT_CNT_S;
    out_telem->flash_encryption_en = efuse_is_flash_encryption_enabled();

    uint32_t rpt0 = efuse_read_mmio(0x30U);
    out_telem->jtag_pad_disabled = (rpt0 & EFUSE_DIS_PAD_JTAG_BIT) != 0U;
    out_telem->jtag_usb_disabled = (rpt0 & EFUSE_DIS_USB_JTAG_BIT) != 0U;
    uint32_t soft_jtag = (rpt0 & EFUSE_SOFT_DIS_JTAG_M) >> EFUSE_SOFT_DIS_JTAG_S;
    out_telem->jtag_soft_disabled = (soft_jtag % 2U) != 0U;

    uint32_t rpt3 = efuse_read_mmio(0x3CU);
    out_telem->download_mode_disabled = (rpt3 & EFUSE_DIS_DOWNLOAD_MODE_BIT) != 0U;
    out_telem->security_download_enabled = (rpt3 & EFUSE_ENABLE_SECURITY_DOWNLOAD_BIT) != 0U;

    out_telem->wr_dis = efuse_get_wr_dis();
    out_telem->rd_dis = efuse_get_rd_dis();
    out_telem->read_count = s_efuse_read_count;

    return EFUSE_OK;
}

/* ========================================================================= */
/* Interactive Console / Diagnostic Visualizers                              */
/* Placed in Flash XIP (.flash.text) to preserve critical IRAM / DRAM memory */
/* ========================================================================= */

__attribute__((section(".flash.text")))
void efuse_print_mac(void)
{
    uint8_t mac[EFUSE_MAC_LEN];
    uint16_t ext = 0U;
    efuse_get_mac(mac);
    efuse_get_mac_ext(&ext);

    console_puts("Factory MAC Address: ");
    const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < (int)EFUSE_MAC_LEN; i++)
    {
        console_putc(hex_chars[(mac[i] >> 4) & 0x0F]);
        console_putc(hex_chars[mac[i] & 0x0F]);
        if (i < 5) console_putc(':');
    }
    console_puts(" (Ext: 0x");
    put_hex(ext);
    console_puts(")\r\n");

    /* Calculate standard EUI-64: MAC[0:2] : EXT[0:1] : MAC[3:5] */
    console_puts("MAC EUI-64:          ");
    uint8_t ext_hi = (uint8_t)((ext >> 8U) & 0xFFU);
    uint8_t ext_lo = (uint8_t)(ext & 0xFFU);
    uint8_t eui64[8] = { mac[0], mac[1], mac[2], ext_hi, ext_lo, mac[3], mac[4], mac[5] };
    for (int i = 0; i < 8; i++)
    {
        console_putc(hex_chars[(eui64[i] >> 4) & 0x0F]);
        console_putc(hex_chars[eui64[i] & 0x0F]);
        if (i < 7) console_putc(':');
    }
    console_puts("\r\n");
}

__attribute__((section(".flash.text")))
void efuse_print_security(void)
{
    efuse_telemetry_t t;
    efuse_get_telemetry(&t);

    console_puts("================ Silicon Security Seals ================\r\n");
    console_puts("  Secure Boot V2:      ");
    console_puts(t.secure_boot_en ? "ENABLED (LOCKED)\r\n" : "DISABLED (DEV MODE)\r\n");

    console_puts("  Flash Encryption:    ");
    console_puts(t.flash_encryption_en ? "ENABLED (CRYPT ACTIVE)\r\n" : "DISABLED (PLAINTEXT)\r\n");
    console_puts("  Flash Crypt Count:   ");
    put_dec(t.flash_crypt_cnt);
    console_puts(" (mask=0x");
    put_hex(t.flash_crypt_cnt);
    console_puts(")\r\n");

    console_puts("  Hardware JTAG:       ");
    console_puts(t.jtag_pad_disabled ? "DISABLED (SEALED)\r\n" : "ENABLED\r\n");

    console_puts("  USB Serial JTAG:     ");
    console_puts(t.jtag_usb_disabled ? "DISABLED (SEALED)\r\n" : "ENABLED\r\n");

    console_puts("  Download Mode:       ");
    console_puts(t.download_mode_disabled ? "DISABLED (SEALED)\r\n" : "ENABLED\r\n");

    console_puts("  Security Download:   ");
    console_puts(t.security_download_enabled ? "ENABLED\r\n" : "DISABLED\r\n");

    console_puts("  Anti-DPA Protection: ");
    console_puts(t.crypt_dpa_enabled ? "ACTIVE (Level " : "INACTIVE (Level ");
    put_dec(t.sec_dpa_level);
    console_puts(")\r\n");

    console_puts("  Write Disable Mask:  0x");
    put_hex(t.wr_dis);
    console_puts("\r\n");

    console_puts("  Read Disable Mask:   0x");
    put_hex(t.rd_dis);
    console_puts("\r\n");
    console_puts("========================================================\r\n");
}

__attribute__((section(".flash.text")))
void efuse_print_summary(void)
{
    efuse_telemetry_t t;
    efuse_get_telemetry(&t);

    console_puts("================ ESP32-C6 eFuse Silicon Summary ================\r\n");
    efuse_print_mac();

    console_puts("Unique ID (128-bit): ");
    const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < (int)EFUSE_UNIQUE_ID_LEN; i++)
    {
        console_putc(hex_chars[(t.unique_id[i] >> 4) & 0x0F]);
        console_putc(hex_chars[t.unique_id[i] & 0x0F]);
        if (i < 15) console_putc(' ');
    }
    console_puts("\r\n");

    console_puts("Silicon Wafer Rev:   v");
    put_dec(t.wafer_version_major);
    console_putc('.');
    put_dec(t.wafer_version_minor);
    console_puts(" (Pkg: ");
    put_dec(t.pkg_version);
    console_puts(", Block2 Rev: ");
    put_dec(t.blk_version_major);
    console_putc('.');
    put_dec(t.blk_version_minor);
    console_puts(")\r\n");

    console_puts("Security Status:     SecureBoot=");
    console_puts(t.secure_boot_en ? "ON" : "OFF");
    console_puts(", FlashCrypt=");
    console_puts(t.flash_encryption_en ? "ON" : "OFF");
    console_puts(", JTAG=");
    console_puts((t.jtag_pad_disabled || t.jtag_usb_disabled) ? "DISABLED" : "ENABLED");
    console_puts("\r\n");

    console_puts("Shadow Refreshes:    ");
    put_dec(t.read_count);
    console_puts("\r\n");
    console_puts("================================================================\r\n");
}
