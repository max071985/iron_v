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
/* Subsystem Lifecycle Initialization                                        */
/* ========================================================================= */

wifi_status_t wifi_init(void)
{
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

    s_wifi_telemetry.state = WIFI_STATE_IDLE;
    s_wifi_initialized = true;

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
