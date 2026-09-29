/*
 * matter.h
 *
 * Iron V Google Home Matter-over-Thread/Wi-Fi Readiness & Commissioning Bridge
 *
 * Implements Matter Specification v1.2 setup payloads, manual pairing code generator
 * and parser with Verhoeff error detection, Base38 QR code serialization,
 * data model clusters (Basic Info, General Commissioning, On/Off), commissioning
 * lifecycle FSM, and ESP32-C6 hardware crypto accelerator (SHA/ECC) interfacing.
 */

#ifndef MATTER_H
#define MATTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Matter Commissioning Constants & Defaults                                 */
/* ========================================================================= */

#define MATTER_DEFAULT_VENDOR_ID                0xFFF1U     /* Standard CSA Test VID */
#define MATTER_DEFAULT_PRODUCT_ID               0x8001U     /* Standard CSA Test PID */
#define MATTER_DEFAULT_DISCRIMINATOR            3840U       /* 12-bit default discriminator (0x0F00) */
#define MATTER_DEFAULT_PASSCODE                 20202021U   /* 27-bit default setup passcode */

#define MATTER_DISCRIMINATOR_MASK               0x0FFFU     /* 12 bits */
#define MATTER_PASSCODE_MASK                    0x07FFFFFFU /* 27 bits */

#define MATTER_COMMISSIONING_FLOW_STANDARD      0U          /* Standard 11-digit pairing code */
#define MATTER_COMMISSIONING_FLOW_USER_ACTION   1U          /* Extended 21-digit code with VID/PID */
#define MATTER_COMMISSIONING_FLOW_CUSTOM        2U          /* Custom onboarding flow */

#define MATTER_DISCOVERY_CAP_SOFTAP             (1U << 0)   /* SoftAP supported */
#define MATTER_DISCOVERY_CAP_BLE                (1U << 1)   /* Bluetooth LE commissioning supported */
#define MATTER_DISCOVERY_CAP_ONNETWORK          (1U << 2)   /* IP on-network discovery supported */

#define MATTER_MANUAL_CODE_LEN_STANDARD         11U         /* 11 decimal digits */
#define MATTER_MANUAL_CODE_LEN_EXTENDED         21U         /* 21 decimal digits */
#define MATTER_MANUAL_CODE_MAX_BUF              32U         /* Buffer for string with hyphens */
#define MATTER_QR_CODE_MAX_BUF                  64U         /* Buffer for "MT:..." payload */

#define MATTER_QR_PREFIX                        "MT:"
#define MATTER_QR_PREFIX_LEN                    3U
#define MATTER_SETUP_PAYLOAD_QR_CHARS           19U

/* Hardware Silicon Accelerator Registers & Constants */
#define MATTER_SHA_BASE_ADDR                    0x60089000U
#define MATTER_ECC_BASE_ADDR                    0x6008B000U
#define MATTER_SHA_DATE_EXPECTED                0x20201229U
#define MATTER_ECC_DATE_EXPECTED                0x02201240U
#define MATTER_SHA256_BLOCK_SIZE                64U
#define MATTER_SHA256_DIGEST_SIZE               32U

/* Matter Data Model Endpoints & Clusters */
#define MATTER_ENDPOINT_ROOT                    0U
#define MATTER_ENDPOINT_APPLICATION             1U

#define MATTER_CLUSTER_IDENTIFY                 0x0003U
#define MATTER_CLUSTER_ONOFF                    0x0006U
#define MATTER_CLUSTER_DESCRIPTOR               0x001DU
#define MATTER_CLUSTER_BASIC_INFORMATION        0x0028U
#define MATTER_CLUSTER_GENERAL_COMMISSIONING    0x0030U
#define MATTER_CLUSTER_NETWORK_COMMISSIONING    0x0031U
#define MATTER_CLUSTER_OPERATIONAL_CREDENTIALS  0x003EU

/* On/Off Cluster Commands */
#define MATTER_CMD_ONOFF_OFF                    0x00U
#define MATTER_CMD_ONOFF_ON                     0x01U
#define MATTER_CMD_ONOFF_TOGGLE                 0x02U

/* General Commissioning Cluster Commands */
#define MATTER_CMD_GENCOMM_ARM_FAILSAFE         0x00U
#define MATTER_CMD_GENCOMM_SET_REGULATORY_CONFIG 0x02U
#define MATTER_CMD_GENCOMM_COMMISSIONING_COMPLETE 0x04U

/* ========================================================================= */
/* Status & Error Codes                                                      */
/* ========================================================================= */

typedef enum {
    MATTER_OK                   =  0,
    MATTER_ERR_INVALID_PARAM    = -1,
    MATTER_ERR_BUFFER_TOO_SMALL = -2,
    MATTER_ERR_CHECKSUM         = -3,
    MATTER_ERR_INVALID_STATE    = -4,
    MATTER_ERR_NOT_FOUND        = -5,
    MATTER_ERR_HW_FAULT         = -6
} matter_status_t;

/* ========================================================================= */
/* Lifecycle States & Transport Modes                                        */
/* ========================================================================= */

typedef enum {
    MATTER_COMMISSIONING_STATE_UNINITIALIZED        = 0,
    MATTER_COMMISSIONING_STATE_READY                = 1,    /* Commissionable advertising active */
    MATTER_COMMISSIONING_STATE_PASE_ENGAGED         = 2,    /* PASE session in progress */
    MATTER_COMMISSIONING_STATE_ARMED_FAILSAFE       = 3,    /* Fail-safe timer armed */
    MATTER_COMMISSIONING_STATE_CONFIGURING_NETWORK  = 4,    /* Thread/Wi-Fi credentials set */
    MATTER_COMMISSIONING_STATE_OPERATIONAL_CREDENTIALS = 5, /* Operational credentials provisioned */
    MATTER_COMMISSIONING_STATE_COMMISSIONED         = 6     /* Operational on fabric */
} matter_commissioning_state_t;

typedef enum {
    MATTER_TRANSPORT_THREAD = 0,    /* Matter-over-Thread via IEEE 802.15.4 (Primary) */
    MATTER_TRANSPORT_WIFI   = 1     /* Matter-over-Wi-Fi via 802.11ax (Secondary) */
} matter_transport_mode_t;

/* ========================================================================= */
/* Concrete Data Structures                                                  */
/* ========================================================================= */

/**
 * Concrete Matter Commissioning Configuration Info
 * Specified in docs/development-roadmap.md Section 2 Task 6.3
 */
typedef struct {
    uint16_t vendor_id;             /* Test VID: 0xFFF1 */
    uint16_t product_id;            /* Test PID: 0x8001 */
    uint16_t discriminator;         /* 12-bit commissioning discriminator */
    uint32_t setup_passcode;        /* 27-bit numeric passcode */
    uint8_t  commissioning_flow;     /* 0 = Standard */
    uint8_t  discovery_capabilities;/* Bitmask: SoftAP, BLE, OnNetwork */
} matter_commissioning_info_t;

/**
 * Subsystem Runtime Telemetry & Verification Counters
 */
typedef struct {
    matter_commissioning_state_t state;
    matter_transport_mode_t      transport;
    uint32_t                     fabric_count;
    uint64_t                     node_id;
    uint8_t                      fabric_index;
    bool                         onoff_state;
    bool                         failsafe_armed;
    uint16_t                     failsafe_remaining_sec;
    bool                         ble_reclaimed;
    bool                         crypto_hw_accelerated;
    uint32_t                     total_commands_processed;
    uint32_t                     total_attribute_reads;
    uint32_t                     total_attribute_writes;
    uint32_t                     sha_date_reg;
    uint32_t                     ecc_date_reg;
} matter_telemetry_t;

/* ========================================================================= */
/* Public API Functions                                                      */
/* ========================================================================= */

/**
 * Initialize Matter subsystem with configuration info (or NULL for default test config).
 */
matter_status_t matter_init(const matter_commissioning_info_t *cfg);

/**
 * Reset Matter subsystem to uncommissioned initial state.
 */
matter_status_t matter_reset(void);

/**
 * Retrieve current active commissioning configuration.
 */
matter_status_t matter_get_commissioning_info(matter_commissioning_info_t *out_info);

/**
 * Update commissioning configuration parameters.
 */
matter_status_t matter_set_commissioning_info(const matter_commissioning_info_t *info);

/**
 * Retrieve current subsystem telemetry.
 */
matter_status_t matter_get_telemetry(matter_telemetry_t *out_telem);

/**
 * Get current commissioning state.
 */
matter_commissioning_state_t matter_get_state(void);

/**
 * Arm commissioning fail-safe timer.
 */
matter_status_t matter_arm_failsafe(uint16_t expiry_sec);

/**
 * Transition state machine to commissioned state, installing fabric and node ID,
 * and triggering BLE memory reclamation.
 */
matter_status_t matter_complete_commissioning(uint64_t node_id, uint8_t fabric_index);

/**
 * Configure active network transport mode (Thread or Wi-Fi).
 */
matter_status_t matter_set_transport(matter_transport_mode_t transport);

/**
 * Retrieve current active network transport mode.
 */
matter_transport_mode_t matter_get_transport(void);

/**
 * Set application endpoint On/Off cluster attribute.
 */
matter_status_t matter_set_onoff(bool on);

/**
 * Query application endpoint On/Off cluster attribute.
 */
bool matter_get_onoff(void);

/**
 * Toggle application endpoint On/Off cluster attribute.
 */
matter_status_t matter_toggle_onoff(void);

/**
 * Compute Verhoeff error-detecting checksum digit for a string of decimal digits.
 */
uint8_t matter_verhoeff_compute(const char *digits);

/**
 * Validate a string of decimal digits containing a terminal Verhoeff check digit.
 */
bool matter_verhoeff_validate(const char *digits);

/**
 * Generate Matter manual pairing code string.
 * Supports standard 11-digit or extended 21-digit code with optional hyphens.
 */
matter_status_t matter_generate_manual_pairing_code(
    const matter_commissioning_info_t *info,
    char *out_str,
    size_t out_max,
    bool formatted
);

/**
 * Parse and validate a Matter manual pairing code string.
 * Strips hyphens, validates Verhoeff check digit, extracts discriminator and passcode.
 */
matter_status_t matter_parse_manual_pairing_code(
    const char *in_str,
    matter_commissioning_info_t *out_info
);

/**
 * Generate Base38 Matter QR code onboarding payload string prefixed with "MT:".
 */
matter_status_t matter_generate_qr_code_payload(
    const matter_commissioning_info_t *info,
    char *out_str,
    size_t out_max
);

/**
 * Parse and validate a Base38 Matter QR code onboarding payload string.
 */
matter_status_t matter_parse_qr_code_payload(
    const char *in_str,
    matter_commissioning_info_t *out_info
);

/**
 * Process inbound Matter cluster command for Endpoint 0 or 1.
 */
matter_status_t matter_process_cluster_command(
    uint16_t endpoint,
    uint32_t cluster_id,
    uint32_t command_id,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *resp_buf,
    size_t *resp_len
);

/**
 * Initialize ESP32-C6 hardware crypto accelerators (SHA and ECC).
 */
matter_status_t matter_crypto_hw_init(void);

/**
 * Compute SHA-256 digest over arbitrary binary data.
 * Utilizes ESP32-C6 hardware SHA engine when running on physical silicon,
 * with pure C fallback on host test environments.
 */
matter_status_t matter_crypto_sha256(
    const uint8_t *data,
    size_t len,
    uint8_t *digest_out
);

/**
 * Read hardware silicon revision date from SHA accelerator.
 */
uint32_t matter_get_sha_date(void);

/**
 * Read hardware silicon revision date from ECC accelerator.
 */
uint32_t matter_get_ecc_date(void);

/**
 * String conversion helpers for human-readable logging and CLI display.
 */
const char *matter_state_to_str(matter_commissioning_state_t state);
const char *matter_transport_to_str(matter_transport_mode_t transport);

#ifdef __cplusplus
}
#endif

#endif /* MATTER_H */
