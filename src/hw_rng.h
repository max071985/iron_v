/*
 * Iron V - Hardware random numbers (REV-20)
 *
 * The one source for every value that must not be predictable: TCP ISNs and ephemeral ports,
 * DHCP transaction IDs, IPv4 identification, retry jitter, supplicant nonces and the blob's
 * os_random/get_random hooks. Ported from ESP-IDF esp_random() (66ab063a, hw_random.c, ESP32-C6 path):
 * - the RNG data register is a PRNG that gains 2 bits of entropy per APB cycle from a hardware noise
 *   source while the Wi-Fi/BT RF is on; reads are spaced so entropy is added faster than it is drained;
 * - each of the 4 result bytes is XORed with the low byte of the LP timer, an asynchronous clock.
 * Every Iron-V caller runs after the PHY is up (joins, DHCP, TCP over the radio).
 * Host builds link a controllable replacement from the test harness.
 */
#ifndef IRON_V_HW_RNG_H
#define IRON_V_HW_RNG_H

#include <stddef.h>
#include <stdint.h>

/* Minimum spacing between RNG reads: IDF waits 160 * 16 APB cycles on the C6 (62.5 kHz sampling) */
#define HW_RNG_READ_WAIT_APB_CYCLES   2560U
#define HW_RNG_LP_TIMER_BYTE_MASK     0xFFU
#define HW_RNG_BITS_PER_BYTE          8U
#define HW_RNG_BITS_PER_WORD          32U

/* Shell `rng [n]`: draw n words (default/max below), print the first few, report bit balance and timing */
#define HW_RNG_SHELL_DEFAULT_WORDS    64U
#define HW_RNG_SHELL_MAX_WORDS        4096U
#define HW_RNG_SHELL_PRINT_WORDS      8U
#define HW_RNG_PERMILLE               1000U

/* Enables the RNG clock; call once at boot before any other hw_rng_* call */
void     hw_rng_init(void);
/* One 32-bit random word (may wait up to HW_RNG_READ_WAIT_APB_CYCLES since the previous call) */
uint32_t hw_rng_u32(void);
/* Fills len bytes */
void     hw_rng_fill(void *buf, size_t len);

#endif /* IRON_V_HW_RNG_H */
