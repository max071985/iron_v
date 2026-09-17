/*
 * src/wifi.c
 *
 * ESP32-C6 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Circular Packet Ring
 * TRM Chapter 4 (GDMA Controller) & Chapter 8 (Reset and Clock / MODEM_SYSCON)
 *
 * Implements static DRAM net_packet_t descriptor rings, GDMA Channel 1 binding,
 * authentic eFuse MAC reading, zero-copy frame dispatch, and ring integrity queries.
 */

#include "wifi.h"
#include "gdma.h"
#include "modem.h"
#include "string.h"
#include "regs/wifi_mac.h"
#include "regs/modem_rf.h"

#if defined(__riscv)
#include "esp_wifi.h"
#include "esp_wifi_he_types.h"
#include "esp_private/wifi.h"
#include "wifi_os_adapter.h"
#include "systimer.h"
#include "task.h"
#include "wdt.h"
#include "console.h"
#include "utils.h"
#include "interrupt.h"
#include "io_constants.h"

static bool s_vendor_wifi_inited = false;
static int32_t s_vendor_init_err = -999;
static volatile bool s_wifi_scan_done = false;
static uint8_t s_wifi_channel = 1U;
static int8_t s_wifi_rssi = 0;
#endif

/* Baseband DMA linkage & RF timing telemetry tracking (Task 3) */
static uint32_t s_rf_dma_linkage_addr = 0U;
static uint32_t s_bb_tx_on_delay = 0U;
static uint32_t s_tx_ramp_delay = 0U;
static uint32_t s_tx_cca_start_ts = 0U;

/* ========================================================================= */
/* Static Storage: Pre-Allocated Packet Descriptor Rings in HP SRAM DRAM    */
/* Zero dynamic heap memory calls permitted (AGENTS.md execution standard)   */
/* ========================================================================= */
static net_packet_t s_rx_packet_ring[PACKET_RING_COUNT] __attribute__((aligned(4)));
static net_packet_t s_tx_packet_ring[WIFI_TX_RING_COUNT] __attribute__((aligned(4)));

static wifi_telemetry_t s_wifi_telemetry = {
    .state             = WIFI_STATE_OFF,
    .mac_addr          = {0x40U, 0x4CU, 0xCAU, 0x45U, 0x1EU, 0x14U}, /* Default fallback */
    .rx_ring_capacity  = PACKET_RING_COUNT,
    .tx_ring_capacity  = WIFI_TX_RING_COUNT,
    .rx_ring_head      = 0U,
    .rx_ring_tail      = 0U,
    .rx_packets        = 0U,
    .tx_packets        = 0U,
    .rx_bytes          = 0U,
    .tx_bytes          = 0U,
    .ring_full_drops   = 0U,
    .dma_err_count     = 0U
};

static bool s_wifi_initialized = false;

/* ========================================================================= */
/* Memory & Hardware Synchronization Barrier                                 */
/* ========================================================================= */
static inline void wifi_fence(void)
{
#if defined(__riscv)
    asm volatile("fence rw, rw" ::: "memory");
#else
    __sync_synchronize();
#endif
}

/* ========================================================================= */
/* Authentic Silicon MAC Address Extraction (eFuse)                          */
/* ========================================================================= */
static void wifi_read_hardware_mac(uint8_t *out_addr)
{
#if defined(__riscv)
    uint32_t mac0 = *EFUSE_MAC_SYS_0_REG;
    uint32_t mac1 = *EFUSE_MAC_SYS_1_REG;

    out_addr[0] = (uint8_t)((mac1 >> 8U) & 0xFFU);
    out_addr[1] = (uint8_t)(mac1 & 0xFFU);
    out_addr[2] = (uint8_t)((mac0 >> 24U) & 0xFFU);
    out_addr[3] = (uint8_t)((mac0 >> 16U) & 0xFFU);
    out_addr[4] = (uint8_t)((mac0 >> 8U) & 0xFFU);
    out_addr[5] = (uint8_t)(mac0 & 0xFFU);
#else
    out_addr[0] = 0x40U;
    out_addr[1] = 0x4CU;
    out_addr[2] = 0xCAU;
    out_addr[3] = 0x45U;
    out_addr[4] = 0x1EU;
    out_addr[5] = 0x14U;
#endif
}

/* ========================================================================= */
/* Circular Packet Ring Initialization                                       */
/* ========================================================================= */

wifi_status_t wifi_rx_ring_init(void)
{
    for (uint32_t i = 0U; i < PACKET_RING_COUNT; i++)
    {
        uint32_t next_idx = (i + 1U) % PACKET_RING_COUNT;

        s_rx_packet_ring[i].dma_desc.dw0             = 0U;
        s_rx_packet_ring[i].dma_desc.size            = PACKET_BUFFER_SIZE;
        s_rx_packet_ring[i].dma_desc.length          = 0U;
        s_rx_packet_ring[i].dma_desc.err_eof         = 0U;
        s_rx_packet_ring[i].dma_desc.suc_eof         = 0U;
        s_rx_packet_ring[i].dma_desc.owner           = DMA_OWNER_DMA;
        s_rx_packet_ring[i].dma_desc.buffer_addr     = (uint32_t)(uintptr_t)s_rx_packet_ring[i].payload;
        s_rx_packet_ring[i].dma_desc.next_descriptor = &s_rx_packet_ring[next_idx].dma_desc;
    }

    s_wifi_telemetry.rx_ring_head = 0U;
    s_wifi_telemetry.rx_ring_tail = 0U;
    wifi_fence();

    return WIFI_OK;
}

wifi_status_t wifi_tx_ring_init(void)
{
    for (uint32_t i = 0U; i < WIFI_TX_RING_COUNT; i++)
    {
        uint32_t next_idx = (i + 1U) % WIFI_TX_RING_COUNT;

        s_tx_packet_ring[i].dma_desc.dw0             = 0U;
        s_tx_packet_ring[i].dma_desc.size            = PACKET_BUFFER_SIZE;
        s_tx_packet_ring[i].dma_desc.length          = 0U;
        s_tx_packet_ring[i].dma_desc.err_eof         = 0U;
        s_tx_packet_ring[i].dma_desc.suc_eof         = 0U;
        s_tx_packet_ring[i].dma_desc.owner           = DMA_OWNER_CPU;
        s_tx_packet_ring[i].dma_desc.buffer_addr     = (uint32_t)(uintptr_t)s_tx_packet_ring[i].payload;
        s_tx_packet_ring[i].dma_desc.next_descriptor = &s_tx_packet_ring[next_idx].dma_desc;
    }

    wifi_fence();
    return WIFI_OK;
}

/* ========================================================================= */
/* Circular Ring Traversal & Boundary Verification (TEST 31)                 */
/* ========================================================================= */

wifi_status_t wifi_verify_rx_ring(uint32_t *out_visited_count)
{
    if (out_visited_count == NULL)
    {
        return WIFI_ERR_INVALID_ARG;
    }

    dma_descriptor_t *start = &s_rx_packet_ring[0].dma_desc;
    dma_descriptor_t *curr = start;
    uint32_t visited = 0U;

    for (uint32_t i = 0U; i <= PACKET_RING_COUNT; i++)
    {
        if (curr == NULL)
        {
            return WIFI_ERR_DMA_FAULT;
        }

        /* 1. Verify 4-byte descriptor alignment */
        if (((uintptr_t)curr & DMA_DESC_ALIGN_MASK) != 0U)
        {
            return WIFI_ERR_UNALIGNED;
        }

#if defined(__riscv)
        /* 2. Verify descriptor DRAM boundaries [0x40820000, 0x40880000) */
        uintptr_t desc_vma = (uintptr_t)curr;
        if (desc_vma < WIFI_DRAM_START_ADDR || desc_vma >= WIFI_DRAM_END_ADDR)
        {
            return WIFI_ERR_OUT_OF_BOUNDS;
        }

        /* 3. Verify buffer pointer DRAM boundaries */
        uintptr_t buf_vma = (uintptr_t)curr->buffer_addr;
        if (buf_vma < WIFI_DRAM_START_ADDR || buf_vma >= WIFI_DRAM_END_ADDR)
        {
            return WIFI_ERR_OUT_OF_BOUNDS;
        }
#endif

        /* 4. Verify buffer capacity and ownership bit */
        if (curr->size != PACKET_BUFFER_SIZE)
        {
            return WIFI_ERR_DMA_FAULT;
        }
        if (curr->owner != DMA_OWNER_DMA)
        {
            return WIFI_ERR_DMA_FAULT;
        }

        visited++;
        curr = curr->next_descriptor;
        if (curr == start)
        {
            break;
        }
    }

    *out_visited_count = visited;
    return (visited == PACKET_RING_COUNT) ? WIFI_OK : WIFI_ERR_DMA_FAULT;
}

/* ========================================================================= */
/* Vendor Wi-Fi Callbacks & Lifecycle Initialization                         */
/* ========================================================================= */

#if defined(__riscv)
static esp_err_t wifi_vendor_rx_callback(void *buffer, uint16_t len, void *eb)
{
    if (buffer != NULL && len > 0U && len <= PACKET_BUFFER_SIZE)
    {
        uint32_t tail = s_wifi_telemetry.rx_ring_tail;
        uint32_t next_tail = (tail + 1U) % PACKET_RING_COUNT;

        if (s_rx_packet_ring[tail].dma_desc.owner == DMA_OWNER_DMA)
        {
            memcpy(s_rx_packet_ring[tail].payload, buffer, len);
            s_rx_packet_ring[tail].dma_desc.length = len;
            s_rx_packet_ring[tail].dma_desc.suc_eof = 1U;
            wifi_fence();
            s_rx_packet_ring[tail].dma_desc.owner = DMA_OWNER_CPU;
            wifi_fence();
            s_wifi_telemetry.rx_ring_tail = next_tail;
            s_wifi_telemetry.rx_bytes += len;
        }
        else
        {
            s_wifi_telemetry.ring_full_drops++;
        }
    }

    if (eb != NULL)
    {
        esp_wifi_internal_free_rx_buffer(eb);
    }
    return 0;
}
#endif

void wifi_handle_vendor_event(int32_t event_id, void *event_data)
{
#if defined(__riscv)
    console_puts("[Wi-Fi Event] id=");
    put_dec((uint32_t)event_id);
    console_puts("\r\n");

    if (event_id == WIFI_EVENT_STA_START)
    {
        console_puts("[Wi-Fi] Event: STA_START\r\n");
        s_wifi_telemetry.state = WIFI_STATE_ACTIVE;
    }
    else if (event_id == WIFI_EVENT_SCAN_DONE)
    {
        console_puts("[Wi-Fi] Event: SCAN_DONE\r\n");
        s_wifi_scan_done = true;
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
        console_puts("[Wi-Fi] Event: STA_CONNECTED\r\n");
        s_wifi_telemetry.state = WIFI_STATE_CONNECTED;
        if (event_data != NULL)
        {
            const wifi_event_sta_connected_t *conn = (const wifi_event_sta_connected_t *)event_data;
            s_wifi_channel = conn->channel;
            console_puts("[Wi-Fi] Associated to AP on channel ");
            put_dec((uint32_t)conn->channel);
            console_puts("\r\n");
        }
        esp_wifi_internal_set_sta_ip();
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        console_puts("[Wi-Fi] Event: STA_DISCONNECTED");
        if (event_data != NULL)
        {
            const wifi_event_sta_disconnected_t *disconn = (const wifi_event_sta_disconnected_t *)event_data;
            console_puts(", reason=");
            put_dec((uint32_t)disconn->reason);
        }
        console_puts("\r\n");
        s_wifi_telemetry.state = WIFI_STATE_DISCONNECTED;
        s_wifi_rssi = 0;
    }
#else
    (void)event_id;
    (void)event_data;
#endif
}

wifi_status_t wifi_init(void)
{
#if defined(__riscv)
    if (s_wifi_initialized && s_vendor_wifi_inited)
    {
        return WIFI_OK;
    }
#else
    if (s_wifi_initialized)
    {
        return WIFI_OK;
    }
#endif

    /* 1. Enable Wi-Fi APB & MAC clocks via MODEM_SYSCON */
    modem_enable_wifi_clocks();

    /* 2. Read authentic silicon MAC address from eFuse */
    wifi_read_hardware_mac(s_wifi_telemetry.mac_addr);

    /* 3. Initialize circular packet descriptor rings */
    wifi_rx_ring_init();
    wifi_tx_ring_init();

    /* 4. Initialize and reset GDMA Channel 1 */
    gdma_channel_init(WIFI_GDMA_CHANNEL);
    gdma_channel_reset(WIFI_GDMA_CHANNEL);

    /* 5. Bind GDMA Channel 1 Inlink to circular RX descriptor ring */
    gdma_inlink_set(WIFI_GDMA_CHANNEL, &s_rx_packet_ring[0].dma_desc);
    gdma_inlink_start(WIFI_GDMA_CHANNEL);

    /* 6. Bind GDMA Channel 1 Outlink to TX descriptor ring */
    gdma_outlink_set(WIFI_GDMA_CHANNEL, &s_tx_packet_ring[0].dma_desc);

    /* 7. Link GDMA descriptor physical base address to Modem SYSCON / RF DMA linkage registers (Task 3) */
    s_rf_dma_linkage_addr = (uint32_t)(uintptr_t)&s_rx_packet_ring[0].dma_desc;
#if defined(__riscv)
    *MODEM_DATA_RF_DMA_DESC_ADDR_REG = s_rf_dma_linkage_addr;
    *MODEM_DATA_TX_DMA_DESC_ADDR_REG = (uint32_t)(uintptr_t)&s_tx_packet_ring[0].dma_desc;
    *MODEM_RF_DMA_BUF_REG            = MODEM_DATA_BASE_ADDR;
    wifi_fence();
#endif

    /* 8. Initialize baseband front-end TX/RX timing coordination delays (Task 3) */
    s_bb_tx_on_delay  = WIFI_MAC_DEFAULT_BB_TX_ON_DELAY_US;
    s_tx_ramp_delay   = WIFI_MAC_DEFAULT_TX_RAMP_DELAY_US;
    s_tx_cca_start_ts = WIFI_MAC_DEFAULT_TX_CCA_START_TS_US;
#if defined(__riscv)
    *WIFI_MAC_BB_TX_ON_DELAY_REG  = s_bb_tx_on_delay;
    *WIFI_MAC_TX_RAMP_DELAY_REG   = s_tx_ramp_delay;
    *WIFI_MAC_TX_CCA_START_TS_REG = s_tx_cca_start_ts;
    *WIFI_MAC_TX_CCA_END_TS_REG   = WIFI_MAC_DEFAULT_TX_CCA_END_TS_US;
    wifi_fence();
#endif

    s_wifi_telemetry.state = WIFI_STATE_IDLE;
    s_wifi_initialized = true;

#if defined(__riscv)
    if (!s_vendor_wifi_inited)
    {
        wifi_os_adapter_init();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        cfg.feature_caps &= ~(CONFIG_FEATURE_FTM_INITIATOR_BIT);
        console_puts("[wifi] calling esp_wifi_init_internal...\r\n");
        lp_wdt_feed();
        lp_wdt_disable();
        wdt_disable();
        esp_err_t err = esp_wifi_init_internal(&cfg);
        wdt_enable();
        lp_wdt_enable();
        lp_wdt_feed();
        console_puts("[wifi] esp_wifi_init_internal err=");
        put_dec((uint32_t)err);
        console_puts("\r\n");
        s_vendor_init_err = (int32_t)err;
        if (err == 0)
        {
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_set_storage(WIFI_STORAGE_RAM);
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX);

            wifi_country_t country = {
                .cc = "01",
                .schan = 1,
                .nchan = 14,
                .max_tx_power = 20,
                .policy = WIFI_COUNTRY_POLICY_MANUAL
            };
            esp_wifi_set_country(&country);
            esp_wifi_set_max_tx_power(80);

            wifi_config_t sta_cfg;
            memset(&sta_cfg, 0, sizeof(sta_cfg));
            sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
            sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            sta_cfg.sta.threshold.rssi = WIFI_DEFAULT_SCAN_RSSI_THRESHOLD;
            sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
            esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
            modem_force_rx_agc();

            esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_vendor_rx_callback);
            wifi_os_adapter_register_wpa_stubs();
            s_vendor_wifi_inited = true;
        }
    }
#endif

    return WIFI_OK;
}


/* ========================================================================= */
/* Zero-Copy Packet Reception & Transmission                                 */
/* ========================================================================= */

wifi_status_t wifi_rx_poll(net_packet_t **out_packet, uint16_t *out_len)
{
    if (out_packet == NULL || out_len == NULL)
    {
        return WIFI_ERR_INVALID_ARG;
    }

    if (!s_wifi_initialized)
    {
        return WIFI_ERR_NOT_INITIALIZED;
    }

    uint32_t head = s_wifi_telemetry.rx_ring_head;
    net_packet_t *pkt = &s_rx_packet_ring[head];

    /* In hardware, GDMA clears owner to DMA_OWNER_CPU upon finishing packet write */
    if (pkt->dma_desc.owner == DMA_OWNER_DMA)
    {
        return WIFI_ERR_RING_EMPTY;
    }

    *out_packet = pkt;
    *out_len = (uint16_t)pkt->dma_desc.length;
    return WIFI_OK;
}

wifi_status_t wifi_rx_release(net_packet_t *packet)
{
    if (packet == NULL)
    {
        return WIFI_ERR_INVALID_ARG;
    }

    /* Reset descriptor metadata and restore ownership to DMA */
    packet->dma_desc.length = 0U;
    packet->dma_desc.suc_eof = 0U;
    packet->dma_desc.err_eof = 0U;
    wifi_fence();
    packet->dma_desc.owner = DMA_OWNER_DMA;
    wifi_fence();

    s_wifi_telemetry.rx_ring_head = (s_wifi_telemetry.rx_ring_head + 1U) % PACKET_RING_COUNT;
    s_wifi_telemetry.rx_packets++;
    return WIFI_OK;
}

wifi_status_t wifi_tx_packet(const uint8_t *payload, uint16_t len)
{
    if (payload == NULL || len == 0U || len > PACKET_BUFFER_SIZE)
    {
        return WIFI_ERR_INVALID_ARG;
    }

    if (!s_wifi_initialized)
    {
        wifi_init();
    }

    uint32_t tail = s_wifi_telemetry.rx_ring_tail;
    net_packet_t *tx_pkt = &s_tx_packet_ring[tail % WIFI_TX_RING_COUNT];

    /* Guard against buffer collision */
    if (tx_pkt->dma_desc.owner == DMA_OWNER_DMA)
    {
        s_wifi_telemetry.ring_full_drops++;
        return WIFI_ERR_RING_FULL;
    }

    memcpy(tx_pkt->payload, payload, len);
    tx_pkt->dma_desc.length = (uint32_t)len;
    tx_pkt->dma_desc.suc_eof = 1U;
    wifi_fence();
    tx_pkt->dma_desc.owner = DMA_OWNER_DMA;
    wifi_fence();

    /* Trigger GDMA Channel 1 Outlink */
    gdma_outlink_restart(WIFI_GDMA_CHANNEL);

    s_wifi_telemetry.tx_packets++;
    s_wifi_telemetry.tx_bytes += (uint32_t)len;
    s_wifi_telemetry.rx_ring_tail = (tail + 1U) % WIFI_TX_RING_COUNT;

    return WIFI_OK;
}

/* ========================================================================= */
/* Accessors & Telemetry Queries                                             */
/* ========================================================================= */

wifi_state_t wifi_get_state(void)
{
    return s_wifi_telemetry.state;
}

wifi_status_t wifi_get_mac_addr(uint8_t *out_mac)
{
    if (out_mac == NULL)
    {
        return WIFI_ERR_INVALID_ARG;
    }

    memcpy(out_mac, s_wifi_telemetry.mac_addr, WIFI_MAC_ADDR_LEN);
    return WIFI_OK;
}

wifi_status_t wifi_get_telemetry(wifi_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL)
    {
        return WIFI_ERR_INVALID_ARG;
    }

    *out_telemetry = s_wifi_telemetry;
    return WIFI_OK;
}

const net_packet_t *wifi_get_rx_packet(uint32_t index)
{
    if (index >= PACKET_RING_COUNT)
    {
        return NULL;
    }

    return &s_rx_packet_ring[index];
}

/* ========================================================================= */
/* Active / Passive Scanning Engine                                          */
/* ========================================================================= */

wifi_status_t wifi_scan(const char *ssid, uint8_t channel, bool passive, uint32_t duration_ms)
{
    if (!s_wifi_initialized)
    {
        wifi_init();
    }

#if defined(__riscv)
    /* Ensure Wi-Fi, Baseband, and Front-End clocks are fully active */
    modem_enable_wifi_clocks();

    if (duration_ms == 0U)
    {
        duration_ms = (channel > 0U) ? WIFI_SCAN_DEFAULT_DURATION_MS : WIFI_SCAN_CHANNEL_DWELL_MS;
    }

    if (s_wifi_telemetry.state != WIFI_STATE_ACTIVE && s_wifi_telemetry.state != WIFI_STATE_CONNECTED)
    {
#if defined(__riscv)
        console_puts("[Wi-Fi] Status: inited=");
        put_dec(s_vendor_wifi_inited);
        console_puts(", init_err=");
        put_dec((uint32_t)s_vendor_init_err);
        console_puts("\r\n");
#endif
        console_puts("[Wi-Fi] Starting station before scan...\r\n");
        wdt_feed();
        lp_wdt_feed();
        console_puts("[Wi-Fi] calling esp_wifi_start()...\r\n");
        esp_err_t start_err = esp_wifi_start();
        console_puts("[Wi-Fi] esp_wifi_start returned err=");
        put_dec((uint32_t)start_err);
        console_puts("\r\n");
        uint64_t start_wait = systimer_get_us();
        while (s_wifi_telemetry.state != WIFI_STATE_ACTIVE &&
               s_wifi_telemetry.state != WIFI_STATE_CONNECTED &&
               (systimer_get_us() - start_wait) < WIFI_STA_START_TIMEOUT_US)
        {
            wdt_feed();
            lp_wdt_feed();
            wifi_os_adapter_poll();
            wdt_supervisor_tick();
            if (task_get_count() > 1U)
            {
                task_yield();
            }
        }
    }

    wifi_scan_config_t scan_cfg;
    memset(&scan_cfg, 0, sizeof(scan_cfg));
    if (ssid != NULL && ssid[0] != '\0')
    {
        scan_cfg.ssid = (uint8_t *)ssid;
    }
    if (channel > 0U && channel <= 14U)
    {
        scan_cfg.channel = channel;
        scan_cfg.channel_bitmap.ghz_2_channels = (uint16_t)(1U << channel);
    }
    else
    {
        scan_cfg.channel = 0U;
        scan_cfg.channel_bitmap.ghz_2_channels = WIFI_SCAN_ALL_2G_CHANNELS_MASK;
    }
    scan_cfg.channel_bitmap.ghz_5_channels = WIFI_SCAN_BYPASS_5G_MASK;
    if (passive)
    {
        scan_cfg.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        scan_cfg.scan_time.passive = duration_ms;
    }
    else
    {
        scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scan_cfg.scan_time.active.min = (duration_ms > 500U) ? 120U : (duration_ms / 2U);
        scan_cfg.scan_time.active.max = (duration_ms > 500U) ? 250U : duration_ms;
    }
    scan_cfg.show_hidden = true;

    console_puts("[Wi-Fi] Starting ");
    console_puts(passive ? "passive" : "active");
    console_puts(" scan (dwell ");
    put_dec(duration_ms);
    console_puts(" ms)");
    if (channel > 0U)
    {
        console_puts(" on channel ");
        put_dec((uint32_t)channel);
    }
    else
    {
        console_puts(" on all channels");
    }
    if (ssid != NULL && ssid[0] != '\0')
    {
        console_puts(" for SSID '");
        console_puts(ssid);
        console_puts("'");
    }
    console_puts("...\r\n");

    uint64_t total_start_us = systimer_get_us();
    uint64_t total_target_us = (channel > 0U) ? ((uint64_t)duration_ms * 1000ULL) : ((uint64_t)duration_ms * 14ULL * 1000ULL);
    uint32_t last_s = 0;
    uint16_t ap_num = 0U;

    modem_force_rx_agc();

    while (1)
    {
        s_wifi_scan_done = false;
        console_puts("[Wi-Fi] ISR1 count before scan: ");
        put_dec(interrupt_get_count(1));
        console_puts("\r\n");
        esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
        if (err != 0)
        {
            console_puts("[Wi-Fi] esp_wifi_scan_start err=");
            put_dec((uint32_t)err);
            console_puts("\r\n");
            return WIFI_ERR_DMA_FAULT;
        }

        uint64_t pass_expected_ms = (channel > 0U) ? 2000ULL : ((uint64_t)duration_ms * 14ULL + 2000ULL);
        uint64_t pass_timeout_us = (pass_expected_ms + 5000ULL) * 1000ULL;
        uint64_t pass_start_us = systimer_get_us();

        while ((systimer_get_us() - pass_start_us) < pass_timeout_us)
        {
            wifi_os_adapter_poll();
            wdt_supervisor_tick();
            uint32_t elapsed_s = (uint32_t)((systimer_get_us() - total_start_us) / 1000000ULL);
            if (elapsed_s != last_s)
            {
                last_s = elapsed_s;
                console_puts(".");
                console_flush();
            }
            if (s_wifi_scan_done)
            {
                break;
            }
            if (task_get_count() > 1U)
            {
                task_yield();
            }
        }

        if (!s_wifi_scan_done)
        {
            console_puts("[Wi-Fi] Scan pass timed out! Stopping...\r\n");
            esp_wifi_scan_stop();
        }

        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num > 0U)
        {
            break;
        }

        if ((systimer_get_us() - total_start_us) >= total_target_us)
        {
            break;
        }
    }
    console_puts("\r\n");

    esp_wifi_scan_get_ap_num(&ap_num);
    console_puts("[Wi-Fi] Scan finished. Found ");
    put_dec((uint32_t)ap_num);
    console_puts(" Access Points:\r\n");

    if (ap_num > 0U)
    {
        wifi_ap_record_t recs[16];
        uint16_t fetch_count = (ap_num > 16U) ? 16U : ap_num;
        esp_wifi_scan_get_ap_records(&fetch_count, recs);
        for (uint16_t i = 0U; i < fetch_count; i++)
        {
            console_puts("  #");
            put_dec((uint32_t)(i + 1U));
            console_puts(": '");
            console_puts((char *)recs[i].ssid);
            console_puts("' Chan=");
            put_dec((uint32_t)recs[i].primary);
            console_puts(" RSSI=");
            if (recs[i].rssi < 0) {
                console_puts("-");
                put_dec((uint32_t)-recs[i].rssi);
            } else {
                put_dec((uint32_t)recs[i].rssi);
            }
            console_puts(" dBm Auth=");
            put_dec((uint32_t)recs[i].authmode);
            console_puts("\r\n");
        }
    }
    else
    {
        esp_wifi_clear_ap_list();
    }
#else
    (void)ssid;
    (void)channel;
    (void)passive;
    (void)duration_ms;
#endif
    return WIFI_OK;
}

#if defined(__riscv)
static volatile uint32_t s_sniffer_total_packets = 0U;
static volatile uint32_t s_sniffer_mgmt_packets = 0U;
static volatile uint32_t s_sniffer_beacons = 0U;

#define FRAME_CTRL_SUBTYPE_MASK        0xFCU
#define FRAME_CTRL_SUBTYPE_BEACON      0x80U
#define FRAME_CTRL_SUBTYPE_PROBE_RESP  0x50U
#define TAG_PARAM_SSID_ID              0x00U
#define IEEE80211_MIN_MGMT_HEADER_LEN  24U
#define IEEE80211_TAGGED_PARAM_OFFSET  36U

typedef struct {
    esp_wifi_rxctrl_t rx_ctrl;
    uint8_t payload[0];
} wifi_promiscuous_pkt_t;

static void wifi_promiscuous_rx_callback(void *buf, wifi_promiscuous_pkt_type_t type)
{
    s_sniffer_total_packets++;
    if (buf == NULL)
    {
        return;
    }
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    int8_t rssi = pkt->rx_ctrl.rssi;
    uint16_t len = pkt->rx_ctrl.sig_len;
    uint8_t chan = pkt->rx_ctrl.channel;

    if (type == WIFI_PKT_MGMT)
    {
        s_sniffer_mgmt_packets++;
        if (len >= IEEE80211_MIN_MGMT_HEADER_LEN)
        {
            uint8_t fc0 = pkt->payload[0];
            if ((fc0 & FRAME_CTRL_SUBTYPE_MASK) == FRAME_CTRL_SUBTYPE_BEACON ||
                (fc0 & FRAME_CTRL_SUBTYPE_MASK) == FRAME_CTRL_SUBTYPE_PROBE_RESP)
            {
                s_sniffer_beacons++;
                if (s_sniffer_beacons <= WIFI_SNIFFER_MAX_BEACONS_LOGGED)
                {
                    char ssid_buf[WIFI_MAX_SSID_LEN + 1U] = {0};
                    if (len >= (IEEE80211_TAGGED_PARAM_OFFSET + 2U))
                    {
                        uint8_t tag_num = pkt->payload[IEEE80211_TAGGED_PARAM_OFFSET];
                        uint8_t tag_len = pkt->payload[IEEE80211_TAGGED_PARAM_OFFSET + 1U];
                        if (tag_num == TAG_PARAM_SSID_ID && tag_len > 0U && tag_len <= WIFI_MAX_SSID_LEN &&
                            (IEEE80211_TAGGED_PARAM_OFFSET + 2U + tag_len) <= len)
                        {
                            memcpy(ssid_buf, &pkt->payload[IEEE80211_TAGGED_PARAM_OFFSET + 2U], tag_len);
                            ssid_buf[tag_len] = '\0';
                        }
                    }
                    console_puts("[sniffer] ");
                    console_puts(((fc0 & FRAME_CTRL_SUBTYPE_MASK) == FRAME_CTRL_SUBTYPE_BEACON) ? "BEACON" : "PROBE_RESP");
                    console_puts(" chan=");
                    put_dec((uint32_t)chan);
                    console_puts(" rssi=");
                    if (rssi < 0)
                    {
                        console_puts("-");
                        put_dec((uint32_t)-rssi);
                    }
                    else
                    {
                        put_dec((uint32_t)rssi);
                    }
                    console_puts(" dBm len=");
                    put_dec((uint32_t)len);
                    if (ssid_buf[0] != '\0')
                    {
                        console_puts(" SSID='");
                        console_puts(ssid_buf);
                        console_puts("'");
                    }
                    console_puts("\r\n");
                    console_flush();
                }
            }
        }
    }
}
#endif

wifi_status_t wifi_sniffer(uint8_t channel, uint32_t duration_sec)
{
    if (!s_wifi_initialized)
    {
        wifi_init();
    }

#if defined(__riscv)
    if (channel == 0U)
    {
        channel = 1U;
    }
    if (duration_sec == 0U)
    {
        duration_sec = WIFI_SNIFFER_DEFAULT_DURATION_SEC;
    }

    modem_enable_wifi_clocks();

    if (s_wifi_telemetry.state != WIFI_STATE_ACTIVE && s_wifi_telemetry.state != WIFI_STATE_CONNECTED)
    {
        console_puts("[Wi-Fi] Starting station before sniffer...\r\n");
        wdt_feed();
        lp_wdt_feed();
        esp_wifi_start();
        uint64_t start_wait = systimer_get_us();
        while (s_wifi_telemetry.state != WIFI_STATE_ACTIVE &&
               s_wifi_telemetry.state != WIFI_STATE_CONNECTED &&
               (systimer_get_us() - start_wait) < WIFI_STA_START_TIMEOUT_US)
        {
            wdt_feed();
            lp_wdt_feed();
            wifi_os_adapter_poll();
            wdt_supervisor_tick();
            if (task_get_count() > 1U)
            {
                task_yield();
            }
        }
    }

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    modem_force_rx_agc();

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_err_t filter_err = esp_wifi_set_promiscuous_filter(&filter);
    esp_err_t cb_err = esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_callback);
    s_sniffer_total_packets = 0U;
    s_sniffer_mgmt_packets = 0U;
    s_sniffer_beacons = 0U;

    esp_err_t prom_err = esp_wifi_set_promiscuous(true);
    if (prom_err != 0 || filter_err != 0 || cb_err != 0)
    {
        console_puts("[Wi-Fi] Promiscuous config err=");
        put_dec((uint32_t)prom_err);
        console_puts(" f_err=");
        put_dec((uint32_t)filter_err);
        console_puts(" cb_err=");
        put_dec((uint32_t)cb_err);
        console_puts("\r\n");
    }

    console_puts("[Wi-Fi] Sniffer active on channel ");
    put_dec((uint32_t)channel);
    console_puts(" for ");
    put_dec(duration_sec);
    console_puts(" seconds (ISR1 count: ");
    put_dec(interrupt_get_count(1));
    console_puts(")...\r\n");

    uint64_t start_us = systimer_get_us();
    uint64_t target_us = (uint64_t)duration_sec * 1000000ULL;
    uint32_t last_s = 0;

    while ((systimer_get_us() - start_us) < target_us)
    {
        wifi_os_adapter_poll();
        wdt_supervisor_tick();
        uint32_t elapsed_s = (uint32_t)((systimer_get_us() - start_us) / 1000000ULL);
        if (elapsed_s != last_s)
        {
            last_s = elapsed_s;
            console_puts(".");
            console_flush();
        }
        if (task_get_count() > 1U)
        {
            task_yield();
        }
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);

    console_puts("\r\n[Wi-Fi] Sniffer finished. Packets: total=");
    put_dec(s_sniffer_total_packets);
    console_puts(" mgmt=");
    put_dec(s_sniffer_mgmt_packets);
    console_puts(" beacons=");
    put_dec(s_sniffer_beacons);
    console_puts(" ISR1 count=");
    put_dec(interrupt_get_count(1));
    console_puts("\r\n");
#else
    (void)channel;
    (void)duration_sec;
#endif
    return WIFI_OK;
}

/* ========================================================================= */
/* Baseband DMA Linkage & RF Timing Telemetry Getters (Task 3)               */
/* ========================================================================= */
uint32_t wifi_get_rf_dma_linkage_reg(void)
{
    return s_rf_dma_linkage_addr;
}

uint32_t wifi_get_bb_tx_on_delay(void)
{
    return s_bb_tx_on_delay;
}

uint32_t wifi_get_tx_ramp_delay(void)
{
    return s_tx_ramp_delay;
}

uint32_t wifi_get_tx_cca_start_ts(void)
{
    return s_tx_cca_start_ts;
}


