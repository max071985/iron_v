/*
 * Iron V - Wi-Fi Link Manager Policy (REV-12)
 *
 * Reconnect policy for the station, kept free of radio calls so it can be host-tested:
 * - exponential backoff with jitter and a cap (review 7.1 principle 4);
 * - cached BSSID/channel of the last successful join, tried first after a link loss
 *   (single-channel scan, seconds faster than a full scan); full scan after
 *   WIFI_LINK_FAST_ATTEMPTS failed fast attempts, until the next success.
 * The join state machine that applies it lives in provisioning_tick().
 */
#ifndef IRON_V_WIFI_LINK_H
#define IRON_V_WIFI_LINK_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_LINK_BSSID_LEN              6U
#define WIFI_LINK_RETRY_MIN_US           1000000ULL    /* first retry after a lost link or failed join */
#define WIFI_LINK_RETRY_MAX_US           300000000ULL  /* backoff cap (5 min) */
#define WIFI_LINK_RETRY_FACTOR           2U            /* backoff doubles per failed attempt */
#define WIFI_LINK_JITTER_PERCENT         20U           /* each wait is the backoff +/- this share */
#define WIFI_LINK_PERCENT                100U
#define WIFI_LINK_FAST_ATTEMPTS          2U            /* cached BSSID/channel attempts before a full scan */
/* Beacons the STA may miss before the blob drops the link (IDF default 6 s, minimum 3 s):
 * rides out short fades; an AP that is really gone is noticed within this time */
#define WIFI_LINK_STA_BEACON_TIMEOUT_S   10U

typedef struct {
    bool    valid;                              /* a join succeeded since the cache was cleared */
    uint8_t bssid[WIFI_LINK_BSSID_LEN];
    uint8_t channel;
    uint8_t fast_failures;                      /* failed fast attempts since the last success */
    uint32_t fast_attempts;                     /* telemetry: attempts made on the fast path */
    uint32_t full_attempts;                     /* telemetry: attempts made with a full scan */
} wifi_link_cache_t;

/* Next backoff after a failed attempt: doubled, capped at WIFI_LINK_RETRY_MAX_US */
uint64_t wifi_link_next_backoff_us(uint64_t delay_us);
/* The wait actually scheduled for a backoff: delay +/- WIFI_LINK_JITTER_PERCENT, picked by rand32 */
uint64_t wifi_link_jitter_us(uint64_t delay_us, uint32_t rand32);

/* Forgets the cached AP (new credentials, portal join) */
void wifi_link_forget(void);
/* A join succeeded on this BSSID/channel */
void wifi_link_on_connected(const uint8_t *bssid, uint8_t channel);
/* Target for the next STA-only attempt. Returns true (fast path) with the cached BSSID and channel,
 * or false with bssid untouched and channel 0 (full scan). Counts the attempt. */
bool wifi_link_next_target(uint8_t *out_bssid, uint8_t *out_channel);
/* The attempt last returned by wifi_link_next_target() failed */
void wifi_link_on_attempt_failed(bool was_fast);
const wifi_link_cache_t *wifi_link_get_cache(void);

#endif /* IRON_V_WIFI_LINK_H */
