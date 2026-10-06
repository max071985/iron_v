/*
 * Iron V - Wi-Fi Link Manager Policy (REV-12). See wifi_link.h.
 */
#include "wifi_link.h"
#include "string.h"

static wifi_link_cache_t s_link_cache;

uint64_t wifi_link_next_backoff_us(uint64_t delay_us)
{
    if (delay_us < WIFI_LINK_RETRY_MIN_US)
    {
        return WIFI_LINK_RETRY_MIN_US;
    }
    if (delay_us >= WIFI_LINK_RETRY_MAX_US / WIFI_LINK_RETRY_FACTOR)
    {
        return WIFI_LINK_RETRY_MAX_US;
    }
    return delay_us * WIFI_LINK_RETRY_FACTOR;
}

uint64_t wifi_link_jitter_us(uint64_t delay_us, uint32_t rand32)
{
    /* Uniform offset in [-J, +J] percent of the delay */
    uint64_t span = (delay_us * WIFI_LINK_JITTER_PERCENT) / WIFI_LINK_PERCENT;
    uint64_t pick = (span == 0U) ? 0U : ((uint64_t)rand32 % (span * 2U + 1U));
    return delay_us - span + pick;
}

void wifi_link_forget(void)
{
    memset(&s_link_cache, 0, sizeof(s_link_cache));
}

void wifi_link_on_connected(const uint8_t *bssid, uint8_t channel)
{
    if (bssid == NULL || channel == 0U)
    {
        return;
    }
    memcpy(s_link_cache.bssid, bssid, WIFI_LINK_BSSID_LEN);
    s_link_cache.channel = channel;
    s_link_cache.fast_failures = 0U;
    s_link_cache.valid = true;
}

bool wifi_link_next_target(uint8_t *out_bssid, uint8_t *out_channel)
{
    bool fast = s_link_cache.valid && s_link_cache.fast_failures < WIFI_LINK_FAST_ATTEMPTS;
    if (fast)
    {
        if (out_bssid != NULL)
        {
            memcpy(out_bssid, s_link_cache.bssid, WIFI_LINK_BSSID_LEN);
        }
        s_link_cache.fast_attempts++;
    }
    else
    {
        s_link_cache.full_attempts++;
    }
    if (out_channel != NULL)
    {
        *out_channel = fast ? s_link_cache.channel : 0U;
    }
    return fast;
}

void wifi_link_on_attempt_failed(bool was_fast)
{
    if (was_fast && s_link_cache.fast_failures < WIFI_LINK_FAST_ATTEMPTS)
    {
        s_link_cache.fast_failures++;
    }
}

const wifi_link_cache_t *wifi_link_get_cache(void)
{
    return &s_link_cache;
}
