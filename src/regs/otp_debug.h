/*
 * OTP_DEBUG.h
 * OTP_DEBUG Peripheral
 * Base Address: 0x600B3C00
 */

#ifndef OTP_DEBUG_H
#define OTP_DEBUG_H

#include <stdint.h>

#define OTP_DEBUG_BASE 0x600B3C00

// Otp debuger block0 data register1.
#define OTP_DEBUG_WR_DIS_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x0))
#define OTP_DEBUG_WR_DIS_BLOCK0_WR_DIS_M (0xFFFFFFFFU)
#define OTP_DEBUG_WR_DIS_BLOCK0_WR_DIS_S (0)
#define OTP_DEBUG_WR_DIS_BLOCK0_WR_DIS_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register2.
#define OTP_DEBUG_BLK0_BACKUP1_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x4))
#define OTP_DEBUG_BLK0_BACKUP1_W1_OTP_BEBUG_BLOCK0_BACKUP1_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP1_W1_OTP_BEBUG_BLOCK0_BACKUP1_W1_S (0)
#define OTP_DEBUG_BLK0_BACKUP1_W1_OTP_BEBUG_BLOCK0_BACKUP1_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register3.
#define OTP_DEBUG_BLK0_BACKUP1_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x8))
#define OTP_DEBUG_BLK0_BACKUP1_W2_OTP_BEBUG_BLOCK0_BACKUP1_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP1_W2_OTP_BEBUG_BLOCK0_BACKUP1_W2_S (0)
#define OTP_DEBUG_BLK0_BACKUP1_W2_OTP_BEBUG_BLOCK0_BACKUP1_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register4.
#define OTP_DEBUG_BLK0_BACKUP1_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xC))
#define OTP_DEBUG_BLK0_BACKUP1_W3_OTP_BEBUG_BLOCK0_BACKUP1_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP1_W3_OTP_BEBUG_BLOCK0_BACKUP1_W3_S (0)
#define OTP_DEBUG_BLK0_BACKUP1_W3_OTP_BEBUG_BLOCK0_BACKUP1_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register5.
#define OTP_DEBUG_BLK0_BACKUP1_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x10))
#define OTP_DEBUG_BLK0_BACKUP1_W4_OTP_BEBUG_BLOCK0_BACKUP1_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP1_W4_OTP_BEBUG_BLOCK0_BACKUP1_W4_S (0)
#define OTP_DEBUG_BLK0_BACKUP1_W4_OTP_BEBUG_BLOCK0_BACKUP1_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register6.
#define OTP_DEBUG_BLK0_BACKUP1_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x14))
#define OTP_DEBUG_BLK0_BACKUP1_W5_OTP_BEBUG_BLOCK0_BACKUP1_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP1_W5_OTP_BEBUG_BLOCK0_BACKUP1_W5_S (0)
#define OTP_DEBUG_BLK0_BACKUP1_W5_OTP_BEBUG_BLOCK0_BACKUP1_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register7.
#define OTP_DEBUG_BLK0_BACKUP2_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x18))
#define OTP_DEBUG_BLK0_BACKUP2_W1_OTP_BEBUG_BLOCK0_BACKUP2_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP2_W1_OTP_BEBUG_BLOCK0_BACKUP2_W1_S (0)
#define OTP_DEBUG_BLK0_BACKUP2_W1_OTP_BEBUG_BLOCK0_BACKUP2_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register8.
#define OTP_DEBUG_BLK0_BACKUP2_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1C))
#define OTP_DEBUG_BLK0_BACKUP2_W2_OTP_BEBUG_BLOCK0_BACKUP2_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP2_W2_OTP_BEBUG_BLOCK0_BACKUP2_W2_S (0)
#define OTP_DEBUG_BLK0_BACKUP2_W2_OTP_BEBUG_BLOCK0_BACKUP2_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register9.
#define OTP_DEBUG_BLK0_BACKUP2_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x20))
#define OTP_DEBUG_BLK0_BACKUP2_W3_OTP_BEBUG_BLOCK0_BACKUP2_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP2_W3_OTP_BEBUG_BLOCK0_BACKUP2_W3_S (0)
#define OTP_DEBUG_BLK0_BACKUP2_W3_OTP_BEBUG_BLOCK0_BACKUP2_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register10.
#define OTP_DEBUG_BLK0_BACKUP2_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x24))
#define OTP_DEBUG_BLK0_BACKUP2_W4_OTP_BEBUG_BLOCK0_BACKUP2_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP2_W4_OTP_BEBUG_BLOCK0_BACKUP2_W4_S (0)
#define OTP_DEBUG_BLK0_BACKUP2_W4_OTP_BEBUG_BLOCK0_BACKUP2_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register11.
#define OTP_DEBUG_BLK0_BACKUP2_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x28))
#define OTP_DEBUG_BLK0_BACKUP2_W5_OTP_BEBUG_BLOCK0_BACKUP2_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP2_W5_OTP_BEBUG_BLOCK0_BACKUP2_W5_S (0)
#define OTP_DEBUG_BLK0_BACKUP2_W5_OTP_BEBUG_BLOCK0_BACKUP2_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register12.
#define OTP_DEBUG_BLK0_BACKUP3_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x2C))
#define OTP_DEBUG_BLK0_BACKUP3_W1_OTP_BEBUG_BLOCK0_BACKUP3_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP3_W1_OTP_BEBUG_BLOCK0_BACKUP3_W1_S (0)
#define OTP_DEBUG_BLK0_BACKUP3_W1_OTP_BEBUG_BLOCK0_BACKUP3_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register13.
#define OTP_DEBUG_BLK0_BACKUP3_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x30))
#define OTP_DEBUG_BLK0_BACKUP3_W2_OTP_BEBUG_BLOCK0_BACKUP3_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP3_W2_OTP_BEBUG_BLOCK0_BACKUP3_W2_S (0)
#define OTP_DEBUG_BLK0_BACKUP3_W2_OTP_BEBUG_BLOCK0_BACKUP3_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register14.
#define OTP_DEBUG_BLK0_BACKUP3_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x34))
#define OTP_DEBUG_BLK0_BACKUP3_W3_OTP_BEBUG_BLOCK0_BACKUP3_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP3_W3_OTP_BEBUG_BLOCK0_BACKUP3_W3_S (0)
#define OTP_DEBUG_BLK0_BACKUP3_W3_OTP_BEBUG_BLOCK0_BACKUP3_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register15.
#define OTP_DEBUG_BLK0_BACKUP3_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x38))
#define OTP_DEBUG_BLK0_BACKUP3_W4_OTP_BEBUG_BLOCK0_BACKUP3_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP3_W4_OTP_BEBUG_BLOCK0_BACKUP3_W4_S (0)
#define OTP_DEBUG_BLK0_BACKUP3_W4_OTP_BEBUG_BLOCK0_BACKUP3_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register16.
#define OTP_DEBUG_BLK0_BACKUP3_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x3C))
#define OTP_DEBUG_BLK0_BACKUP3_W5_OTP_BEBUG_BLOCK0_BACKUP3_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP3_W5_OTP_BEBUG_BLOCK0_BACKUP3_W5_S (0)
#define OTP_DEBUG_BLK0_BACKUP3_W5_OTP_BEBUG_BLOCK0_BACKUP3_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register17.
#define OTP_DEBUG_BLK0_BACKUP4_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x40))
#define OTP_DEBUG_BLK0_BACKUP4_W1_OTP_BEBUG_BLOCK0_BACKUP4_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP4_W1_OTP_BEBUG_BLOCK0_BACKUP4_W1_S (0)
#define OTP_DEBUG_BLK0_BACKUP4_W1_OTP_BEBUG_BLOCK0_BACKUP4_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register18.
#define OTP_DEBUG_BLK0_BACKUP4_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x44))
#define OTP_DEBUG_BLK0_BACKUP4_W2_OTP_BEBUG_BLOCK0_BACKUP4_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP4_W2_OTP_BEBUG_BLOCK0_BACKUP4_W2_S (0)
#define OTP_DEBUG_BLK0_BACKUP4_W2_OTP_BEBUG_BLOCK0_BACKUP4_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register19.
#define OTP_DEBUG_BLK0_BACKUP4_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x48))
#define OTP_DEBUG_BLK0_BACKUP4_W3_OTP_BEBUG_BLOCK0_BACKUP4_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP4_W3_OTP_BEBUG_BLOCK0_BACKUP4_W3_S (0)
#define OTP_DEBUG_BLK0_BACKUP4_W3_OTP_BEBUG_BLOCK0_BACKUP4_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register20.
#define OTP_DEBUG_BLK0_BACKUP4_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x4C))
#define OTP_DEBUG_BLK0_BACKUP4_W4_OTP_BEBUG_BLOCK0_BACKUP4_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP4_W4_OTP_BEBUG_BLOCK0_BACKUP4_W4_S (0)
#define OTP_DEBUG_BLK0_BACKUP4_W4_OTP_BEBUG_BLOCK0_BACKUP4_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block0 data register21.
#define OTP_DEBUG_BLK0_BACKUP4_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x50))
#define OTP_DEBUG_BLK0_BACKUP4_W5_OTP_BEBUG_BLOCK0_BACKUP4_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK0_BACKUP4_W5_OTP_BEBUG_BLOCK0_BACKUP4_W5_S (0)
#define OTP_DEBUG_BLK0_BACKUP4_W5_OTP_BEBUG_BLOCK0_BACKUP4_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register1.
#define OTP_DEBUG_BLK1_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x54))
#define OTP_DEBUG_BLK1_W1_BLOCK1_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W1_BLOCK1_W1_S (0)
#define OTP_DEBUG_BLK1_W1_BLOCK1_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register2.
#define OTP_DEBUG_BLK1_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x58))
#define OTP_DEBUG_BLK1_W2_BLOCK1_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W2_BLOCK1_W2_S (0)
#define OTP_DEBUG_BLK1_W2_BLOCK1_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register3.
#define OTP_DEBUG_BLK1_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x5C))
#define OTP_DEBUG_BLK1_W3_BLOCK1_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W3_BLOCK1_W3_S (0)
#define OTP_DEBUG_BLK1_W3_BLOCK1_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register4.
#define OTP_DEBUG_BLK1_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x60))
#define OTP_DEBUG_BLK1_W4_BLOCK1_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W4_BLOCK1_W4_S (0)
#define OTP_DEBUG_BLK1_W4_BLOCK1_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register5.
#define OTP_DEBUG_BLK1_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x64))
#define OTP_DEBUG_BLK1_W5_BLOCK1_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W5_BLOCK1_W5_S (0)
#define OTP_DEBUG_BLK1_W5_BLOCK1_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register6.
#define OTP_DEBUG_BLK1_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x68))
#define OTP_DEBUG_BLK1_W6_BLOCK1_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W6_BLOCK1_W6_S (0)
#define OTP_DEBUG_BLK1_W6_BLOCK1_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register7.
#define OTP_DEBUG_BLK1_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x6C))
#define OTP_DEBUG_BLK1_W7_BLOCK1_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W7_BLOCK1_W7_S (0)
#define OTP_DEBUG_BLK1_W7_BLOCK1_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register8.
#define OTP_DEBUG_BLK1_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x70))
#define OTP_DEBUG_BLK1_W8_BLOCK1_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W8_BLOCK1_W8_S (0)
#define OTP_DEBUG_BLK1_W8_BLOCK1_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block1 data register9.
#define OTP_DEBUG_BLK1_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x74))
#define OTP_DEBUG_BLK1_W9_BLOCK1_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK1_W9_BLOCK1_W9_S (0)
#define OTP_DEBUG_BLK1_W9_BLOCK1_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register1.
#define OTP_DEBUG_BLK2_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x78))
#define OTP_DEBUG_BLK2_W1_BLOCK2_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W1_BLOCK2_W1_S (0)
#define OTP_DEBUG_BLK2_W1_BLOCK2_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register2.
#define OTP_DEBUG_BLK2_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x7C))
#define OTP_DEBUG_BLK2_W2_BLOCK2_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W2_BLOCK2_W2_S (0)
#define OTP_DEBUG_BLK2_W2_BLOCK2_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register3.
#define OTP_DEBUG_BLK2_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x80))
#define OTP_DEBUG_BLK2_W3_BLOCK2_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W3_BLOCK2_W3_S (0)
#define OTP_DEBUG_BLK2_W3_BLOCK2_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register4.
#define OTP_DEBUG_BLK2_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x84))
#define OTP_DEBUG_BLK2_W4_BLOCK2_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W4_BLOCK2_W4_S (0)
#define OTP_DEBUG_BLK2_W4_BLOCK2_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register5.
#define OTP_DEBUG_BLK2_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x88))
#define OTP_DEBUG_BLK2_W5_BLOCK2_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W5_BLOCK2_W5_S (0)
#define OTP_DEBUG_BLK2_W5_BLOCK2_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register6.
#define OTP_DEBUG_BLK2_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x8C))
#define OTP_DEBUG_BLK2_W6_BLOCK2_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W6_BLOCK2_W6_S (0)
#define OTP_DEBUG_BLK2_W6_BLOCK2_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register7.
#define OTP_DEBUG_BLK2_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x90))
#define OTP_DEBUG_BLK2_W7_BLOCK2_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W7_BLOCK2_W7_S (0)
#define OTP_DEBUG_BLK2_W7_BLOCK2_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register8.
#define OTP_DEBUG_BLK2_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x94))
#define OTP_DEBUG_BLK2_W8_BLOCK2_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W8_BLOCK2_W8_S (0)
#define OTP_DEBUG_BLK2_W8_BLOCK2_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register9.
#define OTP_DEBUG_BLK2_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x98))
#define OTP_DEBUG_BLK2_W9_BLOCK2_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W9_BLOCK2_W9_S (0)
#define OTP_DEBUG_BLK2_W9_BLOCK2_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register10.
#define OTP_DEBUG_BLK2_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x9C))
#define OTP_DEBUG_BLK2_W10_BLOCK2_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W10_BLOCK2_W10_S (0)
#define OTP_DEBUG_BLK2_W10_BLOCK2_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block2 data register11.
#define OTP_DEBUG_BLK2_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xA0))
#define OTP_DEBUG_BLK2_W11_BLOCK2_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK2_W11_BLOCK2_W11_S (0)
#define OTP_DEBUG_BLK2_W11_BLOCK2_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register1.
#define OTP_DEBUG_BLK3_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xA4))
#define OTP_DEBUG_BLK3_W1_BLOCK3_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W1_BLOCK3_W1_S (0)
#define OTP_DEBUG_BLK3_W1_BLOCK3_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register2.
#define OTP_DEBUG_BLK3_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xA8))
#define OTP_DEBUG_BLK3_W2_BLOCK3_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W2_BLOCK3_W2_S (0)
#define OTP_DEBUG_BLK3_W2_BLOCK3_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register3.
#define OTP_DEBUG_BLK3_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xAC))
#define OTP_DEBUG_BLK3_W3_BLOCK3_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W3_BLOCK3_W3_S (0)
#define OTP_DEBUG_BLK3_W3_BLOCK3_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register4.
#define OTP_DEBUG_BLK3_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xB0))
#define OTP_DEBUG_BLK3_W4_BLOCK3_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W4_BLOCK3_W4_S (0)
#define OTP_DEBUG_BLK3_W4_BLOCK3_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register5.
#define OTP_DEBUG_BLK3_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xB4))
#define OTP_DEBUG_BLK3_W5_BLOCK3_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W5_BLOCK3_W5_S (0)
#define OTP_DEBUG_BLK3_W5_BLOCK3_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register6.
#define OTP_DEBUG_BLK3_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xB8))
#define OTP_DEBUG_BLK3_W6_BLOCK3_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W6_BLOCK3_W6_S (0)
#define OTP_DEBUG_BLK3_W6_BLOCK3_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register7.
#define OTP_DEBUG_BLK3_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xBC))
#define OTP_DEBUG_BLK3_W7_BLOCK3_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W7_BLOCK3_W7_S (0)
#define OTP_DEBUG_BLK3_W7_BLOCK3_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register8.
#define OTP_DEBUG_BLK3_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xC0))
#define OTP_DEBUG_BLK3_W8_BLOCK3_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W8_BLOCK3_W8_S (0)
#define OTP_DEBUG_BLK3_W8_BLOCK3_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register9.
#define OTP_DEBUG_BLK3_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xC4))
#define OTP_DEBUG_BLK3_W9_BLOCK3_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W9_BLOCK3_W9_S (0)
#define OTP_DEBUG_BLK3_W9_BLOCK3_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register10.
#define OTP_DEBUG_BLK3_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xC8))
#define OTP_DEBUG_BLK3_W10_BLOCK3_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W10_BLOCK3_W10_S (0)
#define OTP_DEBUG_BLK3_W10_BLOCK3_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block3 data register11.
#define OTP_DEBUG_BLK3_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xCC))
#define OTP_DEBUG_BLK3_W11_BLOCK3_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK3_W11_BLOCK3_W11_S (0)
#define OTP_DEBUG_BLK3_W11_BLOCK3_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register1.
#define OTP_DEBUG_BLK4_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xD0))
#define OTP_DEBUG_BLK4_W1_BLOCK4_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W1_BLOCK4_W1_S (0)
#define OTP_DEBUG_BLK4_W1_BLOCK4_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register2.
#define OTP_DEBUG_BLK4_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xD4))
#define OTP_DEBUG_BLK4_W2_BLOCK4_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W2_BLOCK4_W2_S (0)
#define OTP_DEBUG_BLK4_W2_BLOCK4_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register3.
#define OTP_DEBUG_BLK4_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xD8))
#define OTP_DEBUG_BLK4_W3_BLOCK4_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W3_BLOCK4_W3_S (0)
#define OTP_DEBUG_BLK4_W3_BLOCK4_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register4.
#define OTP_DEBUG_BLK4_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xDC))
#define OTP_DEBUG_BLK4_W4_BLOCK4_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W4_BLOCK4_W4_S (0)
#define OTP_DEBUG_BLK4_W4_BLOCK4_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register5.
#define OTP_DEBUG_BLK4_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xE0))
#define OTP_DEBUG_BLK4_W5_BLOCK4_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W5_BLOCK4_W5_S (0)
#define OTP_DEBUG_BLK4_W5_BLOCK4_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register6.
#define OTP_DEBUG_BLK4_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xE4))
#define OTP_DEBUG_BLK4_W6_BLOCK4_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W6_BLOCK4_W6_S (0)
#define OTP_DEBUG_BLK4_W6_BLOCK4_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register7.
#define OTP_DEBUG_BLK4_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xE8))
#define OTP_DEBUG_BLK4_W7_BLOCK4_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W7_BLOCK4_W7_S (0)
#define OTP_DEBUG_BLK4_W7_BLOCK4_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register8.
#define OTP_DEBUG_BLK4_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xEC))
#define OTP_DEBUG_BLK4_W8_BLOCK4_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W8_BLOCK4_W8_S (0)
#define OTP_DEBUG_BLK4_W8_BLOCK4_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register9.
#define OTP_DEBUG_BLK4_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xF0))
#define OTP_DEBUG_BLK4_W9_BLOCK4_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W9_BLOCK4_W9_S (0)
#define OTP_DEBUG_BLK4_W9_BLOCK4_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data registe10.
#define OTP_DEBUG_BLK4_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xF4))
#define OTP_DEBUG_BLK4_W10_BLOCK4_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W10_BLOCK4_W10_S (0)
#define OTP_DEBUG_BLK4_W10_BLOCK4_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block4 data register11.
#define OTP_DEBUG_BLK4_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xF8))
#define OTP_DEBUG_BLK4_W11_BLOCK4_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK4_W11_BLOCK4_W11_S (0)
#define OTP_DEBUG_BLK4_W11_BLOCK4_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register1.
#define OTP_DEBUG_BLK5_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0xFC))
#define OTP_DEBUG_BLK5_W1_BLOCK5_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W1_BLOCK5_W1_S (0)
#define OTP_DEBUG_BLK5_W1_BLOCK5_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register2.
#define OTP_DEBUG_BLK5_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x100))
#define OTP_DEBUG_BLK5_W2_BLOCK5_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W2_BLOCK5_W2_S (0)
#define OTP_DEBUG_BLK5_W2_BLOCK5_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register3.
#define OTP_DEBUG_BLK5_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x104))
#define OTP_DEBUG_BLK5_W3_BLOCK5_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W3_BLOCK5_W3_S (0)
#define OTP_DEBUG_BLK5_W3_BLOCK5_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register4.
#define OTP_DEBUG_BLK5_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x108))
#define OTP_DEBUG_BLK5_W4_BLOCK5_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W4_BLOCK5_W4_S (0)
#define OTP_DEBUG_BLK5_W4_BLOCK5_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register5.
#define OTP_DEBUG_BLK5_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x10C))
#define OTP_DEBUG_BLK5_W5_BLOCK5_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W5_BLOCK5_W5_S (0)
#define OTP_DEBUG_BLK5_W5_BLOCK5_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register6.
#define OTP_DEBUG_BLK5_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x110))
#define OTP_DEBUG_BLK5_W6_BLOCK5_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W6_BLOCK5_W6_S (0)
#define OTP_DEBUG_BLK5_W6_BLOCK5_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register7.
#define OTP_DEBUG_BLK5_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x114))
#define OTP_DEBUG_BLK5_W7_BLOCK5_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W7_BLOCK5_W7_S (0)
#define OTP_DEBUG_BLK5_W7_BLOCK5_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register8.
#define OTP_DEBUG_BLK5_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x118))
#define OTP_DEBUG_BLK5_W8_BLOCK5_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W8_BLOCK5_W8_S (0)
#define OTP_DEBUG_BLK5_W8_BLOCK5_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register9.
#define OTP_DEBUG_BLK5_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x11C))
#define OTP_DEBUG_BLK5_W9_BLOCK5_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W9_BLOCK5_W9_S (0)
#define OTP_DEBUG_BLK5_W9_BLOCK5_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register10.
#define OTP_DEBUG_BLK5_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x120))
#define OTP_DEBUG_BLK5_W10_BLOCK5_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W10_BLOCK5_W10_S (0)
#define OTP_DEBUG_BLK5_W10_BLOCK5_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block5 data register11.
#define OTP_DEBUG_BLK5_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x124))
#define OTP_DEBUG_BLK5_W11_BLOCK5_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK5_W11_BLOCK5_W11_S (0)
#define OTP_DEBUG_BLK5_W11_BLOCK5_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register1.
#define OTP_DEBUG_BLK6_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x128))
#define OTP_DEBUG_BLK6_W1_BLOCK6_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W1_BLOCK6_W1_S (0)
#define OTP_DEBUG_BLK6_W1_BLOCK6_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register2.
#define OTP_DEBUG_BLK6_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x12C))
#define OTP_DEBUG_BLK6_W2_BLOCK6_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W2_BLOCK6_W2_S (0)
#define OTP_DEBUG_BLK6_W2_BLOCK6_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register3.
#define OTP_DEBUG_BLK6_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x130))
#define OTP_DEBUG_BLK6_W3_BLOCK6_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W3_BLOCK6_W3_S (0)
#define OTP_DEBUG_BLK6_W3_BLOCK6_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register4.
#define OTP_DEBUG_BLK6_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x134))
#define OTP_DEBUG_BLK6_W4_BLOCK6_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W4_BLOCK6_W4_S (0)
#define OTP_DEBUG_BLK6_W4_BLOCK6_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register5.
#define OTP_DEBUG_BLK6_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x138))
#define OTP_DEBUG_BLK6_W5_BLOCK6_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W5_BLOCK6_W5_S (0)
#define OTP_DEBUG_BLK6_W5_BLOCK6_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register6.
#define OTP_DEBUG_BLK6_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x13C))
#define OTP_DEBUG_BLK6_W6_BLOCK6_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W6_BLOCK6_W6_S (0)
#define OTP_DEBUG_BLK6_W6_BLOCK6_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register7.
#define OTP_DEBUG_BLK6_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x140))
#define OTP_DEBUG_BLK6_W7_BLOCK6_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W7_BLOCK6_W7_S (0)
#define OTP_DEBUG_BLK6_W7_BLOCK6_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register8.
#define OTP_DEBUG_BLK6_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x144))
#define OTP_DEBUG_BLK6_W8_BLOCK6_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W8_BLOCK6_W8_S (0)
#define OTP_DEBUG_BLK6_W8_BLOCK6_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register9.
#define OTP_DEBUG_BLK6_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x148))
#define OTP_DEBUG_BLK6_W9_BLOCK6_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W9_BLOCK6_W9_S (0)
#define OTP_DEBUG_BLK6_W9_BLOCK6_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register10.
#define OTP_DEBUG_BLK6_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x14C))
#define OTP_DEBUG_BLK6_W10_BLOCK6_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W10_BLOCK6_W10_S (0)
#define OTP_DEBUG_BLK6_W10_BLOCK6_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block6 data register11.
#define OTP_DEBUG_BLK6_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x150))
#define OTP_DEBUG_BLK6_W11_BLOCK6_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK6_W11_BLOCK6_W11_S (0)
#define OTP_DEBUG_BLK6_W11_BLOCK6_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register1.
#define OTP_DEBUG_BLK7_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x154))
#define OTP_DEBUG_BLK7_W1_BLOCK7_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W1_BLOCK7_W1_S (0)
#define OTP_DEBUG_BLK7_W1_BLOCK7_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register2.
#define OTP_DEBUG_BLK7_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x158))
#define OTP_DEBUG_BLK7_W2_BLOCK7_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W2_BLOCK7_W2_S (0)
#define OTP_DEBUG_BLK7_W2_BLOCK7_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register3.
#define OTP_DEBUG_BLK7_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x15C))
#define OTP_DEBUG_BLK7_W3_BLOCK7_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W3_BLOCK7_W3_S (0)
#define OTP_DEBUG_BLK7_W3_BLOCK7_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register4.
#define OTP_DEBUG_BLK7_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x160))
#define OTP_DEBUG_BLK7_W4_BLOCK7_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W4_BLOCK7_W4_S (0)
#define OTP_DEBUG_BLK7_W4_BLOCK7_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register5.
#define OTP_DEBUG_BLK7_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x164))
#define OTP_DEBUG_BLK7_W5_BLOCK7_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W5_BLOCK7_W5_S (0)
#define OTP_DEBUG_BLK7_W5_BLOCK7_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register6.
#define OTP_DEBUG_BLK7_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x168))
#define OTP_DEBUG_BLK7_W6_BLOCK7_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W6_BLOCK7_W6_S (0)
#define OTP_DEBUG_BLK7_W6_BLOCK7_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register7.
#define OTP_DEBUG_BLK7_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x16C))
#define OTP_DEBUG_BLK7_W7_BLOCK7_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W7_BLOCK7_W7_S (0)
#define OTP_DEBUG_BLK7_W7_BLOCK7_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register8.
#define OTP_DEBUG_BLK7_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x170))
#define OTP_DEBUG_BLK7_W8_BLOCK7_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W8_BLOCK7_W8_S (0)
#define OTP_DEBUG_BLK7_W8_BLOCK7_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register9.
#define OTP_DEBUG_BLK7_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x174))
#define OTP_DEBUG_BLK7_W9_BLOCK7_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W9_BLOCK7_W9_S (0)
#define OTP_DEBUG_BLK7_W9_BLOCK7_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register10.
#define OTP_DEBUG_BLK7_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x178))
#define OTP_DEBUG_BLK7_W10_BLOCK7_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W10_BLOCK7_W10_S (0)
#define OTP_DEBUG_BLK7_W10_BLOCK7_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block7 data register11.
#define OTP_DEBUG_BLK7_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x17C))
#define OTP_DEBUG_BLK7_W11_BLOCK7_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK7_W11_BLOCK7_W11_S (0)
#define OTP_DEBUG_BLK7_W11_BLOCK7_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register1.
#define OTP_DEBUG_BLK8_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x180))
#define OTP_DEBUG_BLK8_W1_BLOCK8_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W1_BLOCK8_W1_S (0)
#define OTP_DEBUG_BLK8_W1_BLOCK8_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register2.
#define OTP_DEBUG_BLK8_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x184))
#define OTP_DEBUG_BLK8_W2_BLOCK8_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W2_BLOCK8_W2_S (0)
#define OTP_DEBUG_BLK8_W2_BLOCK8_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register3.
#define OTP_DEBUG_BLK8_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x188))
#define OTP_DEBUG_BLK8_W3_BLOCK8_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W3_BLOCK8_W3_S (0)
#define OTP_DEBUG_BLK8_W3_BLOCK8_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register4.
#define OTP_DEBUG_BLK8_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x18C))
#define OTP_DEBUG_BLK8_W4_BLOCK8_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W4_BLOCK8_W4_S (0)
#define OTP_DEBUG_BLK8_W4_BLOCK8_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register5.
#define OTP_DEBUG_BLK8_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x190))
#define OTP_DEBUG_BLK8_W5_BLOCK8_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W5_BLOCK8_W5_S (0)
#define OTP_DEBUG_BLK8_W5_BLOCK8_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register6.
#define OTP_DEBUG_BLK8_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x194))
#define OTP_DEBUG_BLK8_W6_BLOCK8_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W6_BLOCK8_W6_S (0)
#define OTP_DEBUG_BLK8_W6_BLOCK8_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register7.
#define OTP_DEBUG_BLK8_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x198))
#define OTP_DEBUG_BLK8_W7_BLOCK8_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W7_BLOCK8_W7_S (0)
#define OTP_DEBUG_BLK8_W7_BLOCK8_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register8.
#define OTP_DEBUG_BLK8_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x19C))
#define OTP_DEBUG_BLK8_W8_BLOCK8_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W8_BLOCK8_W8_S (0)
#define OTP_DEBUG_BLK8_W8_BLOCK8_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register9.
#define OTP_DEBUG_BLK8_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1A0))
#define OTP_DEBUG_BLK8_W9_BLOCK8_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W9_BLOCK8_W9_S (0)
#define OTP_DEBUG_BLK8_W9_BLOCK8_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register10.
#define OTP_DEBUG_BLK8_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1A4))
#define OTP_DEBUG_BLK8_W10_BLOCK8_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W10_BLOCK8_W10_S (0)
#define OTP_DEBUG_BLK8_W10_BLOCK8_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block8 data register11.
#define OTP_DEBUG_BLK8_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1A8))
#define OTP_DEBUG_BLK8_W11_BLOCK8_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK8_W11_BLOCK8_W11_S (0)
#define OTP_DEBUG_BLK8_W11_BLOCK8_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register1.
#define OTP_DEBUG_BLK9_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1AC))
#define OTP_DEBUG_BLK9_W1_BLOCK9_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W1_BLOCK9_W1_S (0)
#define OTP_DEBUG_BLK9_W1_BLOCK9_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register2.
#define OTP_DEBUG_BLK9_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1B0))
#define OTP_DEBUG_BLK9_W2_BLOCK9_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W2_BLOCK9_W2_S (0)
#define OTP_DEBUG_BLK9_W2_BLOCK9_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register3.
#define OTP_DEBUG_BLK9_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1B4))
#define OTP_DEBUG_BLK9_W3_BLOCK9_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W3_BLOCK9_W3_S (0)
#define OTP_DEBUG_BLK9_W3_BLOCK9_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register4.
#define OTP_DEBUG_BLK9_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1B8))
#define OTP_DEBUG_BLK9_W4_BLOCK9_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W4_BLOCK9_W4_S (0)
#define OTP_DEBUG_BLK9_W4_BLOCK9_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register5.
#define OTP_DEBUG_BLK9_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1BC))
#define OTP_DEBUG_BLK9_W5_BLOCK9_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W5_BLOCK9_W5_S (0)
#define OTP_DEBUG_BLK9_W5_BLOCK9_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register6.
#define OTP_DEBUG_BLK9_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1C0))
#define OTP_DEBUG_BLK9_W6_BLOCK9_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W6_BLOCK9_W6_S (0)
#define OTP_DEBUG_BLK9_W6_BLOCK9_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register7.
#define OTP_DEBUG_BLK9_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1C4))
#define OTP_DEBUG_BLK9_W7_BLOCK9_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W7_BLOCK9_W7_S (0)
#define OTP_DEBUG_BLK9_W7_BLOCK9_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register8.
#define OTP_DEBUG_BLK9_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1C8))
#define OTP_DEBUG_BLK9_W8_BLOCK9_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W8_BLOCK9_W8_S (0)
#define OTP_DEBUG_BLK9_W8_BLOCK9_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register9.
#define OTP_DEBUG_BLK9_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1CC))
#define OTP_DEBUG_BLK9_W9_BLOCK9_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W9_BLOCK9_W9_S (0)
#define OTP_DEBUG_BLK9_W9_BLOCK9_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register10.
#define OTP_DEBUG_BLK9_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1D0))
#define OTP_DEBUG_BLK9_W10_BLOCK9_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W10_BLOCK9_W10_S (0)
#define OTP_DEBUG_BLK9_W10_BLOCK9_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block9 data register11.
#define OTP_DEBUG_BLK9_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1D4))
#define OTP_DEBUG_BLK9_W11_BLOCK9_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK9_W11_BLOCK9_W11_S (0)
#define OTP_DEBUG_BLK9_W11_BLOCK9_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register1.
#define OTP_DEBUG_BLK10_W1_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1D8))
#define OTP_DEBUG_BLK10_W1_BLOCK10_W1_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W1_BLOCK10_W1_S (0)
#define OTP_DEBUG_BLK10_W1_BLOCK10_W1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register2.
#define OTP_DEBUG_BLK10_W2_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1DC))
#define OTP_DEBUG_BLK10_W2_BLOCK10_W2_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W2_BLOCK10_W2_S (0)
#define OTP_DEBUG_BLK10_W2_BLOCK10_W2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register3.
#define OTP_DEBUG_BLK10_W3_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1E0))
#define OTP_DEBUG_BLK10_W3_BLOCK10_W3_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W3_BLOCK10_W3_S (0)
#define OTP_DEBUG_BLK10_W3_BLOCK10_W3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register4.
#define OTP_DEBUG_BLK10_W4_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1E4))
#define OTP_DEBUG_BLK10_W4_BLOCK10_W4_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W4_BLOCK10_W4_S (0)
#define OTP_DEBUG_BLK10_W4_BLOCK10_W4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register5.
#define OTP_DEBUG_BLK10_W5_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1E8))
#define OTP_DEBUG_BLK10_W5_BLOCK10_W5_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W5_BLOCK10_W5_S (0)
#define OTP_DEBUG_BLK10_W5_BLOCK10_W5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register6.
#define OTP_DEBUG_BLK10_W6_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1EC))
#define OTP_DEBUG_BLK10_W6_BLOCK10_W6_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W6_BLOCK10_W6_S (0)
#define OTP_DEBUG_BLK10_W6_BLOCK10_W6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register7.
#define OTP_DEBUG_BLK10_W7_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1F0))
#define OTP_DEBUG_BLK10_W7_BLOCK10_W7_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W7_BLOCK10_W7_S (0)
#define OTP_DEBUG_BLK10_W7_BLOCK10_W7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register8.
#define OTP_DEBUG_BLK10_W8_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1F4))
#define OTP_DEBUG_BLK10_W8_BLOCK10_W8_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W8_BLOCK10_W8_S (0)
#define OTP_DEBUG_BLK10_W8_BLOCK10_W8_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register9.
#define OTP_DEBUG_BLK10_W9_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1F8))
#define OTP_DEBUG_BLK10_W9_BLOCK10_W9_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W9_BLOCK10_W9_S (0)
#define OTP_DEBUG_BLK10_W9_BLOCK10_W9_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register10.
#define OTP_DEBUG_BLK10_W10_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x1FC))
#define OTP_DEBUG_BLK10_W10_BLOCK19_W10_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W10_BLOCK19_W10_S (0)
#define OTP_DEBUG_BLK10_W10_BLOCK19_W10_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger block10 data register11.
#define OTP_DEBUG_BLK10_W11_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x200))
#define OTP_DEBUG_BLK10_W11_BLOCK10_W11_M (0xFFFFFFFFU)
#define OTP_DEBUG_BLK10_W11_BLOCK10_W11_S (0)
#define OTP_DEBUG_BLK10_W11_BLOCK10_W11_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Otp debuger clk_en configuration register.
#define OTP_DEBUG_CLK_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x204))
#define OTP_DEBUG_CLK_EN_M (0x00000001U)
#define OTP_DEBUG_CLK_EN_S (0)
#define OTP_DEBUG_CLK_EN_V(v) (((v) << 0) & 0x00000001U)

// Otp_debuger apb2otp enable configuration register.
#define OTP_DEBUG_APB2OTP_EN_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x208))
#define OTP_DEBUG_APB2OTP_EN_APB2OTP_EN_M (0x00000001U)
#define OTP_DEBUG_APB2OTP_EN_APB2OTP_EN_S (0)
#define OTP_DEBUG_APB2OTP_EN_APB2OTP_EN_V(v) (((v) << 0) & 0x00000001U)

// eFuse version register.
#define OTP_DEBUG_DATE_REG ((volatile uint32_t *)(OTP_DEBUG_BASE + 0x20C))
#define OTP_DEBUG_DATE_DATE_M (0x0FFFFFFFU)
#define OTP_DEBUG_DATE_DATE_S (0)
#define OTP_DEBUG_DATE_DATE_V(v) (((v) << 0) & 0x0FFFFFFFU)

#endif // OTP_DEBUG_H
