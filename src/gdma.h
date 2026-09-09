/*
 * src/gdma.h
 *
 * ESP32-C6 GDMA Multi-Channel Engine & Circular Buffer Descriptor Rings Driver
 * TRM Chapter 4 (GDMA Controller, §4.1-§4.8)
 *
 * Implements multi-channel DMA configuration, circular descriptor chain creation,
 * Inlink/Outlink lifecycle controls, ownership handshakes, and status telemetry.
 */

#ifndef IRON_V_GDMA_H
#define IRON_V_GDMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================= */
/* Peripheral Base Addresses (TRM Table 3-3 Peripheral Memory Cartography)   */
/* ========================================================================= */
#define GDMA_BASE_ADDR                   0x60080000U
#define PCR_BASE_ADDR                    0x60096000U

/* ========================================================================= */
/* PCR Peripheral Clock Distribution & Reset Offsets                         */
/* ========================================================================= */
#define PCR_GDMA_CONF_OFFSET             0x00BCU
#ifndef PCR_GDMA_CONF_REG
#define PCR_GDMA_CONF_REG                ((volatile uint32_t *)(PCR_BASE_ADDR + PCR_GDMA_CONF_OFFSET))
#endif
#define PCR_GDMA_CLK_EN_BIT              (1U << 0)
#define PCR_GDMA_RST_EN_BIT              (1U << 1)

/* ========================================================================= */
/* GDMA Channel Geometry & Strides (TRM §4.8 Register Summary)               */
/* ========================================================================= */
#define GDMA_CHANNEL_COUNT               3U
#define GDMA_CHANNEL_0                   0U
#define GDMA_CHANNEL_1                   1U
#define GDMA_CHANNEL_2                   2U

#define GDMA_CH_STRIDE                   0x00C0U
#define GDMA_INT_CH_STRIDE               0x0010U

/* ========================================================================= */
/* Channel Base Address Calculation Macros                                   */
/* ========================================================================= */
#define GDMA_IN_CH_OFFSET(ch)            (0x0070U + ((uint32_t)(ch) * GDMA_CH_STRIDE))
#define GDMA_OUT_CH_OFFSET(ch)           (0x00D0U + ((uint32_t)(ch) * GDMA_CH_STRIDE))

/* ========================================================================= */
/* Channel Relative Register Offsets                                         */
/* ========================================================================= */
#define GDMA_CH_CONF0_OFFSET             0x00U
#define GDMA_CH_CONF1_OFFSET             0x04U
#define GDMA_CH_FIFO_STATUS_OFFSET       0x08U
#define GDMA_CH_POP_PUSH_OFFSET          0x0CU
#define GDMA_CH_LINK_OFFSET              0x10U
#define GDMA_CH_STATE_OFFSET             0x14U
#define GDMA_CH_SUC_EOF_OFFSET           0x18U
#define GDMA_CH_ERR_EOF_OFFSET           0x1CU
#define GDMA_CH_DSCR_OFFSET              0x20U
#define GDMA_CH_DSCR_BF0_OFFSET          0x24U
#define GDMA_CH_DSCR_BF1_OFFSET          0x28U
#define GDMA_CH_PRI_OFFSET               0x2CU
#define GDMA_CH_PERI_SEL_OFFSET          0x30U

/* ========================================================================= */
/* Parameterized IN (Rx) Channel MMIO Register Accessor Macros               */
/* ========================================================================= */
#define GDMA_IN_CONF0_REG(ch)            ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_CONF0_OFFSET))
#define GDMA_IN_CONF1_REG(ch)            ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_CONF1_OFFSET))
#define GDMA_INFIFO_STATUS_REG(ch)       ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_FIFO_STATUS_OFFSET))
#define GDMA_IN_POP_REG(ch)              ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_POP_PUSH_OFFSET))
#define GDMA_IN_LINK_REG(ch)             ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_LINK_OFFSET))
#define GDMA_IN_STATE_REG(ch)            ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_STATE_OFFSET))
#define GDMA_IN_SUC_EOF_DES_REG(ch)      ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_SUC_EOF_OFFSET))
#define GDMA_IN_ERR_EOF_DES_REG(ch)      ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_ERR_EOF_OFFSET))
#define GDMA_IN_DSCR_REG(ch)             ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_DSCR_OFFSET))
#define GDMA_IN_DSCR_BF0_REG(ch)         ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_DSCR_BF0_OFFSET))
#define GDMA_IN_DSCR_BF1_REG(ch)         ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_DSCR_BF1_OFFSET))
#define GDMA_IN_PRI_REG(ch)              ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_PRI_OFFSET))
#define GDMA_IN_PERI_SEL_REG(ch)         ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_IN_CH_OFFSET(ch) + GDMA_CH_PERI_SEL_OFFSET))

/* ========================================================================= */
/* Parameterized OUT (Tx) Channel MMIO Register Accessor Macros              */
/* ========================================================================= */
#define GDMA_OUT_CONF0_REG(ch)           ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_CONF0_OFFSET))
#define GDMA_OUT_CONF1_REG(ch)           ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_CONF1_OFFSET))
#define GDMA_OUTFIFO_STATUS_REG(ch)      ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_FIFO_STATUS_OFFSET))
#define GDMA_OUT_PUSH_REG(ch)            ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_POP_PUSH_OFFSET))
#define GDMA_OUT_LINK_REG(ch)            ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_LINK_OFFSET))
#define GDMA_OUT_STATE_REG(ch)           ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_STATE_OFFSET))
#define GDMA_OUT_EOF_DES_REG(ch)         ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_SUC_EOF_OFFSET))
#define GDMA_OUT_EOF_BFR_DES_REG(ch)     ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_ERR_EOF_OFFSET))
#define GDMA_OUT_DSCR_REG(ch)            ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_DSCR_OFFSET))
#define GDMA_OUT_DSCR_BF0_REG(ch)        ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_DSCR_BF0_OFFSET))
#define GDMA_OUT_DSCR_BF1_REG(ch)        ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_DSCR_BF1_OFFSET))
#define GDMA_OUT_PRI_REG(ch)             ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_PRI_OFFSET))
#define GDMA_OUT_PERI_SEL_REG(ch)        ((volatile uint32_t *)(GDMA_BASE_ADDR + GDMA_OUT_CH_OFFSET(ch) + GDMA_CH_PERI_SEL_OFFSET))

/* ========================================================================= */
/* Parameterized Channel Interrupt Status/Enable MMIO Register Accessors     */
/* ========================================================================= */
#define GDMA_IN_INT_RAW_REG(ch)          ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0000U + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))
#define GDMA_IN_INT_ST_REG(ch)           ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0004U + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))
#define GDMA_IN_INT_ENA_REG(ch)          ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0008U + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))
#define GDMA_IN_INT_CLR_REG(ch)          ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x000CU + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))

#define GDMA_OUT_INT_RAW_REG(ch)         ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0030U + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))
#define GDMA_OUT_INT_ST_REG(ch)          ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0034U + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))
#define GDMA_OUT_INT_ENA_REG(ch)         ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0038U + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))
#define GDMA_OUT_INT_CLR_REG(ch)         ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x003CU + ((uint32_t)(ch) * GDMA_INT_CH_STRIDE)))

/* ========================================================================= */
/* Global GDMA Registers                                                     */
/* ========================================================================= */
#define GDMA_AHB_TEST_REG                ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0060U))
#define GDMA_MISC_CONF_REG               ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0064U))
#define GDMA_DATE_REG                    ((volatile uint32_t *)(GDMA_BASE_ADDR + 0x0068U))

/* ========================================================================= */
/* Bitfield Definitions & Masks                                              */
/* ========================================================================= */
/* IN_CONF0 Register */
#define GDMA_IN_CONF0_IN_RST_BIT         (1U << 0)
#define GDMA_IN_CONF0_IN_LOOP_TEST_BIT   (1U << 1)
#define GDMA_IN_CONF0_INDSCR_BURST_BIT   (1U << 2)
#define GDMA_IN_CONF0_IN_DATA_BURST_BIT  (1U << 3)
#define GDMA_IN_CONF0_MEM_TRANS_EN_BIT   (1U << 4)
#define GDMA_IN_CONF0_IN_ETM_EN_BIT      (1U << 5)

/* OUT_CONF0 Register */
#define GDMA_OUT_CONF0_OUT_RST_BIT       (1U << 0)
#define GDMA_OUT_CONF0_OUT_LOOP_TEST_BIT (1U << 1)
#define GDMA_OUT_CONF0_OUT_AUTO_WRBACK_B (1U << 2)
#define GDMA_OUT_CONF0_OUT_EOF_MODE_BIT  (1U << 3)
#define GDMA_OUT_CONF0_OUTDSCR_BURST_BIT (1U << 4)
#define GDMA_OUT_CONF0_OUT_DATA_BURST_B  (1U << 5)
#define GDMA_OUT_CONF0_OUT_ETM_EN_BIT    (1U << 6)

/* IN_LINK Register */
#define GDMA_IN_LINK_ADDR_MASK           (0x000FFFFFU)
#define GDMA_IN_LINK_AUTO_RET_BIT        (1U << 20)
#define GDMA_IN_LINK_STOP_BIT            (1U << 21)
#define GDMA_IN_LINK_START_BIT           (1U << 22)
#define GDMA_IN_LINK_RESTART_BIT         (1U << 23)
#define GDMA_IN_LINK_PARK_BIT            (1U << 24)

/* OUT_LINK Register */
#define GDMA_OUT_LINK_ADDR_MASK          (0x000FFFFFU)
#define GDMA_OUT_LINK_STOP_BIT           (1U << 20)
#define GDMA_OUT_LINK_START_BIT          (1U << 21)
#define GDMA_OUT_LINK_RESTART_BIT        (1U << 22)
#define GDMA_OUT_LINK_PARK_BIT           (1U << 23)

/* IN Interrupts */
#define GDMA_IN_INT_DONE_BIT             (1U << 0)
#define GDMA_IN_INT_SUC_EOF_BIT          (1U << 1)
#define GDMA_IN_INT_ERR_EOF_BIT          (1U << 2)
#define GDMA_IN_INT_DSCR_ERR_BIT         (1U << 3)
#define GDMA_IN_INT_DSCR_EMPTY_BIT       (1U << 4)
#define GDMA_IN_INT_INFIFO_OVF_BIT       (1U << 5)
#define GDMA_IN_INT_INFIFO_UDF_BIT       (1U << 6)

/* OUT Interrupts */
#define GDMA_OUT_INT_DONE_BIT            (1U << 0)
#define GDMA_OUT_INT_EOF_BIT             (1U << 1)
#define GDMA_OUT_INT_DSCR_ERR_BIT        (1U << 2)
#define GDMA_OUT_INT_TOTAL_EOF_BIT       (1U << 3)
#define GDMA_OUT_INT_OUTFIFO_OVF_BIT     (1U << 4)
#define GDMA_OUT_INT_OUTFIFO_UDF_BIT     (1U << 5)

/* MISC_CONF Register */
#define GDMA_MISC_CONF_AHBM_RST_INT_BIT  (1U << 0)
#define GDMA_MISC_CONF_ARB_PRI_DIS_BIT   (1U << 2)
#define GDMA_MISC_CONF_CLK_EN_BIT        (1U << 3)

/* ========================================================================= */
/* Descriptor Word 0 Bitfield Constants (TRM §4 GDMA, dma_types.h layout)   */
/* ========================================================================= */
/*
 * ESP32-C6 GDMA Descriptor Word 0 layout (matches ESP-IDF dma_types.h):
 *   bits 11:0  - size       (12 bits, buffer capacity)
 *   bits 23:12 - length     (12 bits, valid bytes count)
 *   bits 27:24 - reserved   (4 bits, must be zero)
 *   bit  28    - err_eof    (error end-of-frame, hardware sets for error RX)
 *   bit  29    - reserved   (must be zero)
 *   bit  30    - suc_eof    (success end-of-frame, last descriptor in chain)
 *   bit  31    - owner      (0 = CPU, 1 = DMA)
 */
#define DMA_DESC_SIZE_MASK               (0x00000FFFU)
#define DMA_DESC_SIZE_SHIFT              0U
#define DMA_DESC_MAX_SIZE                4095U

#define DMA_DESC_LENGTH_MASK             (0x00FFF000U)
#define DMA_DESC_LENGTH_SHIFT            12U

/* Bits 27:24 are reserved in Word 0 - no offset field on ESP32-C6 GDMA */
#define DMA_DESC_RESERVED24_MASK         (0x0F000000U)

#define DMA_DESC_ERR_EOF_BIT             (1U << 28)
/* Bit 29 is reserved in Word 0 */
#define DMA_DESC_SUC_EOF_BIT             (1U << 30)
#define DMA_DESC_OWNER_BIT               (1U << 31)

#define DMA_OWNER_CPU                    0U
#define DMA_OWNER_DMA                    1U

#define DMA_DESC_ALIGN_BYTES             4U
#define DMA_DESC_ALIGN_MASK              0x3U

#define DMA_DRAM_START_ADDR              0x40800000U
#define DMA_DRAM_END_ADDR                0x40880000U

/* Expected hardware version date register on ESP32-C6 silicon */
#define GDMA_HARDWARE_DATE_EXPECTED      0x02202250U

#define GDMA_PERI_SEL_M2M                1U

/* ========================================================================= */
/* Concrete Data Structures                                                  */
/* ========================================================================= */

/**
 * @brief Linked List DMA Descriptor Structure (lldesc_t)
 *
 * Bitfield layout is exact to ESP32-C6 hardware (dma_types.h):
 *   Word 0: size[11:0] | length[23:12] | reserved[27:24] | err_eof[28] | reserved[29] | suc_eof[30] | owner[31]
 *   Word 1: buffer  (full 32-bit DRAM address)
 *   Word 2: next    (full 32-bit DRAM address, or NULL to terminate)
 *
 * Hardware requires descriptors to be strictly 4-byte aligned in HP SRAM DRAM.
 * Total size is exactly 12 bytes (3 x 32-bit words).
 */
typedef struct dma_descriptor_s {
    union {
        struct {
            uint32_t size         : 12; /**< Buffer capacity in bytes (max 4095) */
            uint32_t length       : 12; /**< Valid byte count in buffer (max 4095) */
            uint32_t reserved24   : 4;  /**< Reserved, must be zero */
            uint32_t err_eof      : 1;  /**< Error end-of-frame flag (bit 28) */
            uint32_t reserved29   : 1;  /**< Reserved, must be zero (bit 29) */
            uint32_t suc_eof      : 1;  /**< Success EOF: last descriptor in chain (bit 30) */
            uint32_t owner        : 1;  /**< Ownership: 0=CPU, 1=DMA (bit 31) */
        };
        uint32_t dw0;
    };
    uint32_t buffer_addr;                     /**< Full 32-bit pointer to payload buffer in DRAM */
    struct dma_descriptor_s *next_descriptor; /**< Full 32-bit pointer to next descriptor (NULL terminates) */
} __attribute__((aligned(4))) dma_descriptor_t;

/**
 * @brief Driver Return Status Codes
 */
typedef enum {
    GDMA_OK = 0,
    GDMA_ERR_INVALID_ARG = -1,
    GDMA_ERR_UNALIGNED = -2,
    GDMA_ERR_OUT_OF_RANGE = -3,
    GDMA_ERR_BUSY = -4,
    GDMA_ERR_NOT_INITIALIZED = -5
} gdma_status_t;

/**
 * @brief Per-Channel Telemetry Structure
 */
typedef struct {
    uint32_t in_state;
    uint32_t in_dscr_addr;
    uint32_t out_state;
    uint32_t out_dscr_addr;
    uint32_t in_int_raw;
    uint32_t out_int_raw;
    uint8_t in_active;
    uint8_t out_active;
} gdma_channel_telemetry_t;

/**
 * @brief Global Subsystem Telemetry Structure
 */
typedef struct {
    uint32_t date_version;
    uint8_t ch0_in_active;
    uint8_t ch0_out_active;
    gdma_channel_telemetry_t channels[GDMA_CHANNEL_COUNT];
} gdma_telemetry_t;

/* ========================================================================= */
/* Public API Function Prototypes                                            */
/* ========================================================================= */

/**
 * @brief Initialize GDMA peripheral clocks, release reset, and configure AHB bus.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_init(void);

/**
 * @brief Initialize a single GDMA channel pair (In/Out) with clean default states.
 * @param channel Channel index (0 to GDMA_CHANNEL_COUNT - 1).
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_channel_init(uint32_t channel);

/**
 * @brief Reset FIFO and state machine of a GDMA channel.
 * @param channel Channel index (0 to GDMA_CHANNEL_COUNT - 1).
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_channel_reset(uint32_t channel);

/**
 * @brief Set the starting descriptor address for an IN (Rx) channel.
 * @param channel Channel index (0 to GDMA_CHANNEL_COUNT - 1).
 * @param desc Pointer to 4-byte aligned descriptor in DRAM.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_inlink_set(uint32_t channel, const dma_descriptor_t *desc);

/**
 * @brief Start IN (Rx) DMA processing on the configured inlink.
 * @param channel Channel index.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_inlink_start(uint32_t channel);

/**
 * @brief Stop IN (Rx) DMA processing.
 * @param channel Channel index.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_inlink_stop(uint32_t channel);

/**
 * @brief Restart IN (Rx) DMA processing.
 * @param channel Channel index.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_inlink_restart(uint32_t channel);

/**
 * @brief Set the starting descriptor address for an OUT (Tx) channel.
 * @param channel Channel index (0 to GDMA_CHANNEL_COUNT - 1).
 * @param desc Pointer to 4-byte aligned descriptor in DRAM.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_outlink_set(uint32_t channel, const dma_descriptor_t *desc);

/**
 * @brief Start OUT (Tx) DMA processing on the configured outlink.
 * @param channel Channel index.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_outlink_start(uint32_t channel);

/**
 * @brief Stop OUT (Tx) DMA processing.
 * @param channel Channel index.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_outlink_stop(uint32_t channel);

/**
 * @brief Restart OUT (Tx) DMA processing.
 * @param channel Channel index.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_outlink_restart(uint32_t channel);

/**
 * @brief Retrieve current telemetry for a single channel.
 * @param channel Channel index.
 * @param out_telem Output pointer for channel telemetry.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_get_channel_telemetry(uint32_t channel, gdma_channel_telemetry_t *out_telem);

/**
 * @brief Retrieve global telemetry for all GDMA channels.
 * @param out_telem Output pointer for global telemetry.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_get_telemetry(gdma_telemetry_t *out_telem);

/**
 * @brief Format and initialize a DMA descriptor structure.
 * @param desc Pointer to descriptor to initialize.
 * @param buf Pointer to payload buffer in DRAM.
 * @param size Buffer capacity in bytes (max 4095).
 * @param length Valid data length in bytes (max 4095).
 * @param owner DMA_OWNER_CPU or DMA_OWNER_DMA.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_desc_init(dma_descriptor_t *desc, void *buf, uint16_t size, uint16_t length, uint8_t owner);

/**
 * @brief Statically link two descriptors into a circular ring.
 * @param desc0 First descriptor in the ring.
 * @param desc1 Second descriptor in the ring.
 * @return GDMA_OK on success, negative error code otherwise.
 */
gdma_status_t gdma_desc_link_circular(dma_descriptor_t *desc0, dma_descriptor_t *desc1);

/**
 * @brief Read hardware date/version register.
 * @return 32-bit version register value.
 */
uint32_t gdma_get_date_version(void);

#endif /* IRON_V_GDMA_H */
