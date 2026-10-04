/*
 * Host-test stand-in for the generated LP core image (build/gen/lp_firmware_image.h).
 * Lets the host tests build without the RISC-V cross toolchain. The payload is a
 * single "j ." instruction; host tests only check the header fields, not the code.
 */
#ifndef LP_FIRMWARE_IMAGE_H
#define LP_FIRMWARE_IMAGE_H
#include <stdint.h>
#include <stddef.h>
static const uint8_t g_lp_firmware_bin[] __attribute__((aligned(4))) = {0x6FU, 0x00U, 0x00U, 0x00U};
static const size_t g_lp_firmware_bin_len = sizeof(g_lp_firmware_bin);
#endif
