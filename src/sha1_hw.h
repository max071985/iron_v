/*
 * Iron V - SHA-1 block compression on the ESP32-C6 SHA accelerator (REV-16)
 *
 * Used by PBKDF2 (WPA2 PMK derivation): with the HMAC inner/outer key states precomputed, every
 * PBKDF2 iteration is two single-block compressions from a saved state. The accelerator runs in
 * "typical" (CPU-fed) mode: message words go to M memory, the state sits in H memory, START hashes
 * from the SHA-1 initial value and CONTINUE from whatever H holds, so writing H first resumes from
 * any saved state (ESP-IDF sha_ll_write_digest does the same).
 *
 * Byte layout: H and M memory hold the digest/message bytes in memory order, so a state is kept as
 * the 20 digest bytes (sha1_state_t words are loaded/stored as raw memory, not as host integers).
 * Not reentrant; callers run from the main loop and never yield in between.
 */
#ifndef IRON_V_SHA1_HW_H
#define IRON_V_SHA1_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHA1_BLOCK_WORDS        16U
#define SHA1_BLOCK_BYTES        64U
#define SHA1_STATE_WORDS        5U
#define SHA1_DIGEST_BYTES       20U

/* SHA_MODE_REG values (TRM: SHA accelerator) */
#define SHA_HW_MODE_SHA1        0U
#define SHA_HW_TRIGGER          1U          /* write to SHA_START_REG / SHA_CONTINUE_REG */
#define SHA_HW_BUSY_POLL_MAX    10000U      /* one block takes ~80 SHA clocks */

/* SHA-1 state as digest bytes in memory order (see header comment) */
typedef struct {
    uint32_t w[SHA1_STATE_WORDS];
} sha1_state_t;

/* Message block as bytes in memory order */
typedef struct {
    uint32_t w[SHA1_BLOCK_WORDS];
} sha1_block_t;

void sha1_hw_init(void);
/* out = compress(in, block); in == NULL starts from the SHA-1 initial value. False on timeout. */
bool sha1_hw_compress(const sha1_state_t *in, const sha1_block_t *block, sha1_state_t *out);

#endif /* IRON_V_SHA1_HW_H */
