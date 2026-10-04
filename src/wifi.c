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
#include "net.h"
#include "dhcp.h"
#include "regs/wifi_mac.h"
#include "regs/modem_rf.h"
#include "wpa2_client.h"

#if defined(__riscv)
#include "wifi_vendor_types.h"
#include "wifi_regulatory.h"
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
extern uint8_t *g_wifi_nvs;
#endif

/* RF timing telemetry tracking (Task 3) */
static uint32_t s_bb_tx_on_delay = 0U;
static uint32_t s_tx_ramp_delay = 0U;
static uint32_t s_tx_cca_start_ts = 0U;

/* SoftAP Broadcast Tracking (Task 5.7.2) */
static bool s_wifi_ap_running = false;
static char s_wifi_ap_ssid[WIFI_MAX_SSID_LEN + 1U] = {0};
static uint8_t s_wifi_ap_channel = WIFI_DEFAULT_AP_CHANNEL;
static bool s_wifi_cca_enabled = true;

/* Station Subsystem Tracking (Task 8.2) */
static bool s_wifi_sta_connected = false;
static uint8_t s_wifi_sta_bssid[WIFI_MAC_ADDR_LEN] = {0};

/* ========================================================================= */
/* Static Storage: software RX queue filled by the blob's RX callback       */
/* ========================================================================= */
static net_packet_t s_rx_packet_ring[PACKET_RING_COUNT] __attribute__((aligned(4)));

static wifi_telemetry_t s_wifi_telemetry = {
    .state             = WIFI_STATE_OFF,
    .mac_addr          = {0x40U, 0x4CU, 0xCAU, 0x45U, 0x1EU, 0x14U}, /* Default fallback */
    .rx_ring_capacity  = PACKET_RING_COUNT,
    .rx_ring_head      = 0U,
    .rx_ring_tail      = 0U,
    .rx_packets        = 0U,
    .tx_packets        = 0U,
    .rx_bytes          = 0U,
    .tx_bytes          = 0U,
    .ring_full_drops   = 0U,
    .tx_errors         = 0U
};

static bool s_wifi_initialized = false;

#if !defined(__riscv)
static uint8_t s_host_last_tx[PACKET_BUFFER_SIZE];
static uint16_t s_host_last_tx_len = 0U;
static wifi_tx_if_t s_host_last_tx_if = WIFI_TX_IF_AP;
#endif

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

#if defined(__riscv)
static void wifi_print_mac(const uint8_t *mac)
{
    const char hex_chars[] = "0123456789abcdef";
    for (uint32_t i = 0U; i < WIFI_MAC_ADDR_LEN; i++)
    {
        console_putc(hex_chars[(mac[i] >> 4U) & 0x0FU]);
        console_putc(hex_chars[mac[i] & 0x0FU]);
        if (i < (WIFI_MAC_ADDR_LEN - 1U))
        {
            console_putc(':');
        }
    }
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
    else if (event_id == WIFI_EVENT_STA_STOP)
    {
        console_puts("[Wi-Fi] Event: STA_STOP\r\n");
        s_wifi_telemetry.state = WIFI_STATE_IDLE;
        s_wifi_sta_connected = false;
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
        s_wifi_sta_connected = true;
        if (event_data != NULL)
        {
            const wifi_event_sta_connected_t *conn = (const wifi_event_sta_connected_t *)event_data;
            s_wifi_channel = conn->channel;
            memcpy(s_wifi_sta_bssid, conn->bssid, WIFI_MAC_ADDR_LEN);
            console_puts("[Wi-Fi] Associated to AP on channel ");
            put_dec((uint32_t)conn->channel);
            console_puts("\r\n");
            wpa2_client_on_connected(conn->bssid);
        }
        esp_wifi_internal_set_sta_ip();
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        console_puts("[Wi-Fi] Event: STA_DISCONNECTED");
        s_wifi_sta_connected = false;
        if (event_data != NULL)
        {
            const wifi_event_sta_disconnected_t *disconn = (const wifi_event_sta_disconnected_t *)event_data;
            console_puts(", reason=");
            put_dec((uint32_t)disconn->reason);
            if (disconn->reason == 201)
            {
                console_puts(" (NO_AP_FOUND)");
            }
            else if (disconn->reason == 202 || disconn->reason == 15 || disconn->reason == 2)
            {
                console_puts(" (AUTH_FAILED / WRONG_PASSWORD)");
            }
            else if (disconn->reason == 204)
            {
                console_puts(" (HANDSHAKE_TIMEOUT)");
            }
            wpa2_client_on_disconnected(disconn->reason);
        }
        else
        {
            wpa2_client_on_disconnected(0);
        }
        console_puts("\r\n");
        s_wifi_telemetry.state = WIFI_STATE_ACTIVE;
        s_wifi_rssi = 0;
    }
    else if (event_id == WIFI_EVENT_AP_START)
    {
        console_puts("[Wi-Fi] Event: AP_START\r\n");
        s_wifi_telemetry.state = WIFI_STATE_AP_ACTIVE;
        s_wifi_ap_running = true;
    }
    else if (event_id == WIFI_EVENT_AP_STOP)
    {
        console_puts("[Wi-Fi] Event: AP_STOP\r\n");
        s_wifi_telemetry.state = WIFI_STATE_IDLE;
        s_wifi_ap_running = false;
    }
    else if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        console_puts("[Wi-Fi] Event: AP_STACONNECTED");
        if (event_data != NULL)
        {
            const wifi_event_ap_staconnected_t *staconn = (const wifi_event_ap_staconnected_t *)event_data;
            console_puts(" MAC: ");
            wifi_print_mac(staconn->mac);
            console_puts(" AID=");
            put_dec((uint32_t)staconn->aid);
        }
        console_puts("\r\n");
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        console_puts("[Wi-Fi] Event: AP_STADISCONNECTED");
        if (event_data != NULL)
        {
            const wifi_event_ap_stadisconnected_t *stadisconn = (const wifi_event_ap_stadisconnected_t *)event_data;
            console_puts(" MAC: ");
            wifi_print_mac(stadisconn->mac);
            console_puts(" AID=");
            put_dec((uint32_t)stadisconn->aid);
            console_puts(" reason=");
            put_dec((uint32_t)stadisconn->reason);
            dhcp_release_lease(stadisconn->mac);
        }
        console_puts("\r\n");
    }
#else
    if (event_id == WIFI_VENDOR_EVENT_AP_START)
    {
        s_wifi_telemetry.state = WIFI_STATE_AP_ACTIVE;
        s_wifi_ap_running = true;
    }
    else if (event_id == WIFI_VENDOR_EVENT_AP_STOP)
    {
        s_wifi_telemetry.state = WIFI_STATE_IDLE;
        s_wifi_ap_running = false;
    }
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

    /* 3. Initialize the software RX queue (the MAC does not use GDMA; frames
     *    arrive through the blob's RX callback) */
    wifi_rx_ring_init();

    /* 4. Store timing parameters.
     * NOTE: Physical silicon tracing proved that 0x600AD000/0x600AD004 are the MAC's
     * hardware 64-bit microsecond TSF timer, and 0x600A4010-0x600A401C are hardware
     * BSSID filter registers. They must NEVER be overwritten with DRAM pointers or
     * delay constants, as doing so destroys 802.11 TBTT timing and filters. */
    s_bb_tx_on_delay  = WIFI_MAC_DEFAULT_BB_TX_ON_DELAY_US;
    s_tx_ramp_delay   = WIFI_MAC_DEFAULT_TX_RAMP_DELAY_US;
    s_tx_cca_start_ts = WIFI_MAC_DEFAULT_TX_CCA_START_TS_US;

#if defined(__riscv)
    /* Ensure CCA is enabled so normal carrier sense and TBTT can proceed */
    wifi_set_cca_enabled(true);
#endif

    s_wifi_telemetry.state = WIFI_STATE_IDLE;
    s_wifi_initialized = true;

#if defined(__riscv)
    if (!s_vendor_wifi_inited)
    {
        wifi_os_adapter_init();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        cfg.nvs_enable = 1;
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
            esp_wifi_set_ps(WIFI_PS_NONE);
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

            esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_vendor_rx_callback);
            esp_wifi_internal_reg_rxcb(WIFI_IF_AP, wifi_vendor_rx_callback);
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

wifi_tx_if_t wifi_get_ip_tx_if(void)
{
    return s_wifi_sta_connected ? WIFI_TX_IF_STA : WIFI_TX_IF_AP;
}

/* Hands one Ethernet II frame to the blob on the given interface (no copy, no GDMA) */
wifi_status_t wifi_tx_packet(wifi_tx_if_t ifx, const uint8_t *payload, uint16_t len)
{
    if (payload == NULL || len == 0U || len > PACKET_BUFFER_SIZE ||
        (ifx != WIFI_TX_IF_STA && ifx != WIFI_TX_IF_AP))
    {
        return WIFI_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    bool if_up = (ifx == WIFI_TX_IF_AP) ? s_wifi_ap_running
                                        : (s_wifi_telemetry.state == WIFI_STATE_CONNECTED ||
                                           s_wifi_telemetry.state == WIFI_STATE_ACTIVE);
    if (!s_vendor_wifi_inited || !if_up)
    {
        s_wifi_telemetry.tx_errors++;
        return WIFI_ERR_IF_DOWN;
    }

    esp_err_t tx_err = esp_wifi_internal_tx((wifi_interface_t)ifx, (void *)payload, len);
    if (tx_err != 0)
    {
        s_wifi_telemetry.tx_errors++;
        console_puts("[Wi-Fi] TX err=");
        put_dec((uint32_t)tx_err);
        console_puts(" if=");
        put_dec((uint32_t)ifx);
        console_puts(" len=");
        put_dec((uint32_t)len);
        console_puts("\r\n");
        return WIFI_ERR_TX_FAILED;
    }
#else
    memcpy(s_host_last_tx, payload, len);
    s_host_last_tx_len = len;
    s_host_last_tx_if = ifx;
#endif

    s_wifi_telemetry.tx_packets++;
    s_wifi_telemetry.tx_bytes += (uint32_t)len;
    return WIFI_OK;
}

#if !defined(__riscv)
const uint8_t *wifi_host_last_tx(uint16_t *out_len, wifi_tx_if_t *out_ifx)
{
    if (out_len != NULL) *out_len = s_host_last_tx_len;
    if (out_ifx != NULL) *out_ifx = s_host_last_tx_if;
    return s_host_last_tx;
}
#endif

void wifi_poll_rx_traffic(void)
{
    net_packet_t *pkt = NULL;
    uint16_t len = 0U;
    while (wifi_rx_poll(&pkt, &len) == WIFI_OK)
    {
        if (pkt != NULL && len > 0U)
        {
            net_input(pkt->payload, len);
        }
        wifi_rx_release(pkt);
    }
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

wifi_status_t wifi_get_ap_mac_addr(uint8_t *out_mac)
{
    if (out_mac == NULL)
    {
        return WIFI_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    if (s_vendor_wifi_inited)
    {
        if (esp_wifi_get_mac(WIFI_IF_AP, out_mac) == 0)
        {
            return WIFI_OK;
        }
    }
#endif

    memcpy(out_mac, s_wifi_telemetry.mac_addr, WIFI_MAC_ADDR_LEN);
    out_mac[5] = (uint8_t)(out_mac[5] + 1U);
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

WIFI_FLASH_TEXT
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
        if (s_wifi_ap_running)
        {
            esp_wifi_set_mode(WIFI_MODE_APSTA);
        }
        else
        {
            esp_wifi_set_mode(WIFI_MODE_STA);
        }
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

    /* Prevent modem sleep, assert hardware RF enable, initialize analog registers, and enable CCA */
    esp_wifi_set_ps(WIFI_PS_NONE);
    *MODEM_RF_ENABLE_REG |= MODEM_RF_ENABLE_MASTER_BIT;
    modem_rf_analog_init();
    wifi_set_cca_enabled(true);
    esp_wifi_set_max_tx_power(WIFI_DEFAULT_MAX_TX_POWER_INDEX);
    wifi_fence();

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
    scan_cfg.channel_bitmap.ghz_5_channels = 0;
    if (passive)
    {
        scan_cfg.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        scan_cfg.scan_time.passive = duration_ms;
    }
    else
    {
        scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scan_cfg.scan_time.active.min = 120U;
        scan_cfg.scan_time.active.max = 250U;
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

WIFI_FLASH_TEXT
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
        if (s_wifi_ap_running)
        {
            esp_wifi_set_mode(WIFI_MODE_APSTA);
        }
        else
        {
            esp_wifi_set_mode(WIFI_MODE_STA);
        }
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

    /* Prevent modem sleep, assert hardware RF enable, initialize analog registers, and enable CCA */
    esp_wifi_set_ps(WIFI_PS_NONE);
    *MODEM_RF_ENABLE_REG |= MODEM_RF_ENABLE_MASTER_BIT;
    modem_rf_analog_init();
    wifi_set_cca_enabled(true);
    wifi_fence();

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

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

/* ========================================================================= */
/* SoftAP Broadcasting Subsystem (Task 5.7.2)                                */
/* ========================================================================= */

wifi_status_t wifi_start_ap(const char *ssid, const char *password, uint8_t channel)
{
    if (!s_wifi_initialized)
    {
        wifi_init();
    }

    modem_enable_wifi_clocks();

    const char *target_ssid = (ssid != NULL && ssid[0] != '\0') ? ssid : WIFI_DEFAULT_AP_SSID;
    size_t ssid_len = strlen(target_ssid);
    if (ssid_len > WIFI_MAX_SSID_LEN)
    {
        ssid_len = WIFI_MAX_SSID_LEN;
    }
    memcpy(s_wifi_ap_ssid, target_ssid, ssid_len);
    s_wifi_ap_ssid[ssid_len] = '\0';
    s_wifi_ap_channel = (channel >= WIFI_MIN_CHANNEL && channel <= WIFI_MAX_CHANNEL) ? channel : WIFI_DEFAULT_AP_CHANNEL;

#if defined(__riscv)
    if (s_vendor_wifi_inited)
    {
        if (s_wifi_telemetry.state == WIFI_STATE_ACTIVE ||
            s_wifi_telemetry.state == WIFI_STATE_CONNECTED ||
            s_wifi_telemetry.state == WIFI_STATE_AP_ACTIVE ||
            s_wifi_ap_running)
        {
            esp_wifi_stop();
        }

        esp_err_t err_mode = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err_mode != 0)
        {
            console_puts("[Wi-Fi] WARN: set_mode AP err=");
            put_dec((uint32_t)err_mode);
            console_puts("\r\n");
        }
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);

        wifi_config_t ap_cfg;
        memset(&ap_cfg, 0, sizeof(ap_cfg));
        memcpy(ap_cfg.ap.ssid, s_wifi_ap_ssid, ssid_len);
        ap_cfg.ap.ssid[ssid_len] = '\0';
        ap_cfg.ap.ssid_len = (uint8_t)ssid_len;
        ap_cfg.ap.channel = s_wifi_ap_channel;

        if (password != NULL && strlen(password) >= WIFI_AP_MIN_PASSWORD_LEN)
        {
            size_t pass_len = strlen(password);
            if (pass_len > WIFI_MAX_PASSPHRASE_LEN)
            {
                pass_len = WIFI_MAX_PASSPHRASE_LEN;
            }
            memcpy(ap_cfg.ap.password, password, pass_len);
            if (pass_len < sizeof(ap_cfg.ap.password))
            {
                ap_cfg.ap.password[pass_len] = '\0';
            }
            ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        }
        else
        {
            ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        }

        ap_cfg.ap.ssid_hidden = 0U;
        ap_cfg.ap.max_connection = WIFI_DEFAULT_AP_MAX_CONN;
        ap_cfg.ap.beacon_interval = WIFI_DEFAULT_AP_BEACON_INTERVAL_TU;
        ap_cfg.ap.dtim_period = WIFI_DEFAULT_AP_DTIM_PERIOD;
        ap_cfg.ap.csa_count = WIFI_DEFAULT_AP_CSA_COUNT;

        wifi_country_t country = {
            .cc = "01",
            .schan = WIFI_MIN_CHANNEL,
            .nchan = WIFI_MAX_CHANNEL,
            .max_tx_power = WIFI_DEFAULT_COUNTRY_MAX_TX_PWR,
            .policy = WIFI_COUNTRY_POLICY_MANUAL
        };
        esp_wifi_set_country(&country);

        esp_err_t err_cfg = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        if (err_cfg != 0)
        {
            console_puts("[Wi-Fi] WARN: set_config AP err=");
            put_dec((uint32_t)err_cfg);
            console_puts("\r\n");
        }

        console_puts("[Wi-Fi] Starting AP stack...\r\n");
        esp_wifi_config_11b_rate(WIFI_IF_AP, false);
        esp_wifi_internal_reg_rxcb(WIFI_IF_AP, wifi_vendor_rx_callback);
        esp_err_t err_start = esp_wifi_start();
        if (err_start != 0)
        {
            console_puts("[Wi-Fi] WARN: esp_wifi_start err=");
            put_dec((uint32_t)err_start);
            console_puts("\r\n");
        }
        esp_wifi_set_ps(WIFI_PS_NONE);

        /* Assert Hardware Master RF Enable, initialize antenna switch telemetry, and enable CCA */
        *MODEM_RF_ENABLE_REG |= MODEM_RF_ENABLE_MASTER_BIT;
        modem_rf_analog_init();
        wifi_set_cca_enabled(true);

        esp_err_t err_pwr = esp_wifi_set_max_tx_power(WIFI_DEFAULT_MAX_TX_POWER_INDEX);
        if (err_pwr != 0)
        {
            console_puts("[Wi-Fi] WARN: set_max_tx_power err=");
            put_dec((uint32_t)err_pwr);
            console_puts("\r\n");
        }

        wifi_fence();

        uint64_t start_wait = systimer_get_us();
        while (!s_wifi_ap_running &&
               s_wifi_telemetry.state != WIFI_STATE_AP_ACTIVE &&
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
#else
    (void)password;
#endif

    s_wifi_ap_running = true;
    s_wifi_telemetry.state = WIFI_STATE_AP_ACTIVE;

    uint8_t ap_mac[WIFI_MAC_ADDR_LEN];
    if (wifi_get_ap_mac_addr(ap_mac) == WIFI_OK)
    {
        net_set_mac(ap_mac);
    }

    return WIFI_OK;
}

wifi_status_t wifi_stop_ap(void)
{
#if defined(__riscv)
    if (s_vendor_wifi_inited)
    {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
    }
#endif
    s_wifi_ap_running = false;
    s_wifi_telemetry.state = WIFI_STATE_IDLE;

    uint8_t sta_mac[WIFI_MAC_ADDR_LEN];
    if (wifi_get_mac_addr(sta_mac) == WIFI_OK)
    {
        net_set_mac(sta_mac);
    }
    return WIFI_OK;
}

bool wifi_is_ap_active(void)
{
    return s_wifi_ap_running || (s_wifi_telemetry.state == WIFI_STATE_AP_ACTIVE);
}

const char *wifi_get_ap_ssid(void)
{
    return (s_wifi_ap_ssid[0] != '\0') ? s_wifi_ap_ssid : WIFI_DEFAULT_AP_SSID;
}

uint8_t wifi_get_ap_channel(void)
{
    return s_wifi_ap_channel;
}

/* ========================================================================= */
/* PHY CCA Control APIs (Task 5.7.3)                                         */
/* ========================================================================= */

wifi_status_t wifi_set_cca_enabled(bool enabled)
{
    s_wifi_cca_enabled = enabled;
#if defined(__riscv)
    if (!enabled)
    {
        /* Force hardware baseband to ignore CCA / carrier sense busy hold-off */
        *WIFI_MAC_DBG_CTRL_REG |= WIFI_MAC_DBG_TB_IGNORE_CCA_ENABLE_BIT;
        *WIFI_MAC_PHY_CCA_CTRL_REG &= ~WIFI_MAC_PHY_CCA_BUSY_FORCE_MASK;
    }
    else
    {
        /* Restore standard CCA carrier sense operation */
        *WIFI_MAC_DBG_CTRL_REG &= ~WIFI_MAC_DBG_TB_IGNORE_CCA_ENABLE_BIT;
        *WIFI_MAC_PHY_CCA_CTRL_REG &= ~WIFI_MAC_PHY_CCA_BUSY_FORCE_MASK;
    }
    wifi_fence();
#endif
    return WIFI_OK;
}

bool wifi_is_cca_enabled(void)
{
#if defined(__riscv)
    return ((*WIFI_MAC_DBG_CTRL_REG & WIFI_MAC_DBG_TB_IGNORE_CCA_ENABLE_BIT) == 0U) &&
           ((*WIFI_MAC_PHY_CCA_CTRL_REG & WIFI_MAC_PHY_CCA_BUSY_FORCE_MASK) == 0U);
#else
    return s_wifi_cca_enabled;
#endif
}

/* ========================================================================= */
/* Wi-Fi Station (STA) Subsystem (Task 8.2)                                  */
/* ========================================================================= */

WIFI_FLASH_TEXT
wifi_status_t wifi_start_sta_chan(const char *ssid, const char *password, uint8_t channel)
{
    if (ssid == NULL || ssid[0] == '\0')
    {
        return WIFI_ERR_INVALID_ARG;
    }

    if (!s_wifi_initialized)
    {
        wifi_init();
    }

    /* 1. Stop SoftAP if active */
    if (s_wifi_ap_running)
    {
        wifi_stop_ap();
#if defined(__riscv)
        uint64_t stop_wait = systimer_get_us();
        while (s_wifi_ap_running && (systimer_get_us() - stop_wait) < WIFI_STA_START_TIMEOUT_US)
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
#endif
    }

#if !defined(__riscv)
    (void)password;
    (void)channel;
#endif

#if defined(__riscv)
    if (s_vendor_wifi_inited)
    {
        esp_wifi_set_mode(WIFI_MODE_STA);

        wifi_config_t sta_cfg;
        memset(&sta_cfg, 0, sizeof(sta_cfg));
        strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
        if (password != NULL)
        {
            strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);
        }
        if (channel > 0U && channel <= 14U)
        {
            sta_cfg.sta.channel = channel;
            sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
        }
        else
        {
            sta_cfg.sta.channel = 0U;
            sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        }
        sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        sta_cfg.sta.threshold.rssi = WIFI_DEFAULT_SCAN_RSSI_THRESHOLD;
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
        sta_cfg.sta.pmf_cfg.capable = true;
        sta_cfg.sta.pmf_cfg.required = false;

        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        esp_wifi_config_11b_rate(WIFI_IF_STA, false);
        esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_vendor_rx_callback);

        if (s_wifi_telemetry.state != WIFI_STATE_ACTIVE && s_wifi_telemetry.state != WIFI_STATE_CONNECTED)
        {
            esp_wifi_start();

            /* Wait for STA_START event */
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

        /* Prevent modem sleep, assert hardware RF enable, initialize analog registers, and enable CCA */
        esp_wifi_set_ps(WIFI_PS_NONE);
        *MODEM_RF_ENABLE_REG |= MODEM_RF_ENABLE_MASTER_BIT;
        modem_rf_analog_init();
        wifi_set_cca_enabled(true);
        esp_wifi_set_max_tx_power(WIFI_DEFAULT_MAX_TX_POWER_INDEX);
        wifi_fence();

        console_puts("[Wi-Fi] Connecting to '");
        console_puts(ssid);
        if (channel > 0U)
        {
            console_puts("' on channel ");
            put_dec((uint32_t)channel);
            console_puts("...\r\n");
        }
        else
        {
            console_puts("'...\r\n");
        }

        esp_err_t err_conn = esp_wifi_connect();
        if (err_conn != 0)
        {
            console_puts("[Wi-Fi] ERROR: esp_wifi_connect err=");
            put_dec((uint32_t)err_conn);
            console_puts("\r\n");
            return WIFI_ERR_DMA_FAULT;
        }
    }
#endif

    s_wifi_sta_connected = false;
    return WIFI_OK;
}

WIFI_FLASH_TEXT
wifi_status_t wifi_start_sta(const char *ssid, const char *password)
{
    return wifi_start_sta_chan(ssid, password, 0U);
}

WIFI_FLASH_TEXT
wifi_status_t wifi_stop_sta(void)
{
#if defined(__riscv)
    if (s_vendor_wifi_inited)
    {
        esp_wifi_disconnect();
    }
#endif
    s_wifi_telemetry.state = WIFI_STATE_IDLE;
    s_wifi_sta_connected = false;
    return WIFI_OK;
}

WIFI_FLASH_TEXT
bool wifi_is_sta_connected(void)
{
    return s_wifi_sta_connected || (s_wifi_telemetry.state == WIFI_STATE_CONNECTED);
}

WIFI_FLASH_TEXT
wifi_status_t wifi_sta_get_bssid(uint8_t *out_bssid)
{
    if (out_bssid == NULL)
    {
        return WIFI_ERR_INVALID_ARG;
    }
    memcpy(out_bssid, s_wifi_sta_bssid, WIFI_MAC_ADDR_LEN);
    return WIFI_OK;
}

