/*
 * Iron V - MQTT 3.1.1 client (REV-23)
 *
 * The board's one network peer in normal operation (review 7.1 principle 3): a broker on the LAN,
 * plain TCP, IPv4 literal (no DNS). Spec: docs/reference/property-model-v1.md section 3.
 *
 *   ironv/<id>/availability   board -> broker  QoS 1, retained  "online" after CONNACK, Will "offline"
 *   ironv/<id>/light/state    board -> broker  QoS 1, retained  on change (coalesced) + once per session
 *   ironv/<id>/light/set      broker -> board  QoS 1            commands; retained ones are dropped
 *   homeassistant/light/<id>/config            QoS 1, retained  HA discovery, once per session
 *
 * Clean session (commands sent while the board is offline are dropped), keepalive 180 s, reconnect
 * with backoff 1 s .. 5 min +/- 20 % (the Wi-Fi link manager's policy). Settings live in NVS and are
 * changed from the shell (`mqtt`) or REST (/api/v1/mqtt); nothing runs until a broker is set.
 */
#ifndef IRON_V_MQTT_H
#define IRON_V_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Protocol (MQTT 3.1.1, OASIS standard) */
#define MQTT_PROTOCOL_NAME               "MQTT"
#define MQTT_PROTOCOL_LEVEL              4U
#define MQTT_PKT_CONNECT                 0x10U
#define MQTT_PKT_CONNACK                 0x20U
#define MQTT_PKT_PUBLISH                 0x30U
#define MQTT_PKT_PUBACK                  0x40U
#define MQTT_PKT_SUBSCRIBE               0x82U   /* type 8, reserved flags 0010 */
#define MQTT_PKT_SUBACK                  0x90U
#define MQTT_PKT_PINGREQ                 0xC0U
#define MQTT_PKT_PINGRESP                0xD0U
#define MQTT_PKT_DISCONNECT              0xE0U
#define MQTT_PKT_TYPE_MASK               0xF0U
#define MQTT_PUB_FLAG_RETAIN             0x01U
#define MQTT_PUB_QOS_SHIFT               1U
#define MQTT_PUB_QOS_MASK                0x03U
#define MQTT_PUB_FLAG_DUP                0x08U
#define MQTT_CONN_FLAG_USERNAME          0x80U
#define MQTT_CONN_FLAG_PASSWORD          0x40U
#define MQTT_CONN_FLAG_WILL_RETAIN       0x20U
#define MQTT_CONN_WILL_QOS_SHIFT         3U
#define MQTT_CONN_FLAG_WILL              0x04U
#define MQTT_CONN_FLAG_CLEAN             0x02U
#define MQTT_CONNACK_LEN                 2U
#define MQTT_CONNACK_ACCEPTED            0U
#define MQTT_SUBACK_FAILURE              0x80U
#define MQTT_QOS0                        0U
#define MQTT_QOS1                        1U
#define MQTT_REMLEN_MAX_BYTES            4U
#define MQTT_REMLEN_DIGIT_MASK           0x7FU
#define MQTT_REMLEN_CONTINUE             0x80U
#define MQTT_REMLEN_SHIFT                7U
#define MQTT_U16_HI_SHIFT                8U
#define MQTT_BYTE_MASK                   0xFFU
#define MQTT_FIXED_HDR_MAX               (1U + MQTT_REMLEN_MAX_BYTES)
#define MQTT_PACKET_ID_LEN               2U
#define MQTT_STR_LEN_BYTES               2U

/* Session policy (spec 3.1) */
#define MQTT_DEFAULT_PORT                1883U
#define MQTT_KEEPALIVE_S                 180U
#define MQTT_US_PER_S                    1000000ULL
#define MQTT_KEEPALIVE_US                ((uint64_t)MQTT_KEEPALIVE_S * MQTT_US_PER_S)
#define MQTT_PINGRESP_TIMEOUT_US         30000000ULL   /* then the session counts as dead */
#define MQTT_CONNACK_TIMEOUT_US          10000000ULL
#define MQTT_ARP_RETRY_US                1000000ULL
#define MQTT_ARP_TRIES                   3U
#define MQTT_STABLE_SESSION_US           60000000ULL   /* a session this long resets the backoff */

/* Buffers and names */
#define MQTT_RX_BUF_LEN                  256U    /* largest inbound packet kept (a command) */
#define MQTT_TX_BUF_LEN                  256U    /* CONNECT and PUBLISH headers; payloads go to TCP directly */
#define MQTT_TOPIC_MAX                   48U     /* longest: homeassistant/light/<id>/config, 39 */
#define MQTT_TOPIC_ROOT                  "ironv/"
#define MQTT_TOPIC_AVAIL_SUFFIX          "/availability"
#define MQTT_TOPIC_STATE_SUFFIX          "/light/state"
#define MQTT_TOPIC_SET_SUFFIX            "/light/set"
#define MQTT_TOPIC_DISCOVERY_PREFIX      "homeassistant/light/"
#define MQTT_TOPIC_DISCOVERY_SUFFIX      "/config"
#define MQTT_PAYLOAD_ONLINE              "online"
#define MQTT_PAYLOAD_OFFLINE             "offline"
#define MQTT_USER_MAX_LEN                32U     /* characters, without NUL */
#define MQTT_PASS_MAX_LEN                63U     /* NVS_VAL_MAX_LEN - NUL */

/* NVS keys */
#define MQTT_NVS_KEY_ENABLED             "mqtt_en"
#define MQTT_NVS_KEY_HOST                "mqtt_host"
#define MQTT_NVS_KEY_PORT                "mqtt_port"
#define MQTT_NVS_KEY_USER                "mqtt_user"
#define MQTT_NVS_KEY_PASS                "mqtt_pass"

typedef struct {
    bool     enabled;
    uint32_t host;               /* IPv4, host byte order; 0: not set */
    uint16_t port;
    char     user[MQTT_USER_MAX_LEN + 1U];
    char     pass[MQTT_PASS_MAX_LEN + 1U];
} mqtt_config_t;

typedef enum {
    MQTT_ST_OFF = 0,             /* disabled or no broker set */
    MQTT_ST_WAIT_LINK,           /* waiting for the STA and an IPv4 lease */
    MQTT_ST_ARP,                 /* resolving the broker (or gateway) MAC */
    MQTT_ST_TCP,                 /* TCP handshake */
    MQTT_ST_CONNACK,             /* CONNECT sent */
    MQTT_ST_ONLINE,
    MQTT_ST_BACKOFF              /* waiting before the next attempt */
} mqtt_state_t;

typedef enum {
    MQTT_ERR_NONE = 0,
    MQTT_ERR_ARP_TIMEOUT,
    MQTT_ERR_TCP_TIMEOUT,
    MQTT_ERR_TCP_RESET,
    MQTT_ERR_PEER_CLOSED,
    MQTT_ERR_CONNACK_TIMEOUT,
    MQTT_ERR_REFUSED,            /* CONNACK return code != 0 (see last_refusal) */
    MQTT_ERR_PING_TIMEOUT,
    MQTT_ERR_PROTOCOL,
    MQTT_ERR_IP_CHANGED,
    MQTT_ERR_NO_PCB,
    MQTT_ERR_TX_FULL,
    MQTT_ERR_NO_ROUTE
} mqtt_error_t;

typedef struct {
    mqtt_state_t state;
    mqtt_error_t last_error;
    uint8_t      last_refusal;           /* CONNACK return code */
    uint32_t     attempts;
    uint32_t     sessions;               /* CONNACKs accepted */
    uint32_t     reconnects;             /* sessions lost after being online */
    uint32_t     publishes;
    uint32_t     pubacks;
    uint32_t     commands;               /* PUBLISHes on the set topic */
    uint32_t     commands_rejected;
    uint32_t     retained_dropped;       /* light_cmd_retained_dropped (spec 3.2) */
    uint32_t     pings;
    uint32_t     rx_oversized;           /* inbound packets over MQTT_RX_BUF_LEN, skipped */
    uint32_t     tx_full;                /* sends refused: TCP send buffer full */
    uint32_t     arp_requests;
    uint64_t     retry_delay_us;         /* current backoff step (before jitter) */
    uint64_t     next_retry_us;
    uint64_t     online_since_us;
} mqtt_status_t;

/* ------------------------------------------------------------------------- */
/* Packet codec (pure, host-tested). Each returns the packet length, 0 if it does not fit. */
/* ------------------------------------------------------------------------- */
size_t mqtt_encode_remaining_length(uint32_t len, uint8_t out[MQTT_REMLEN_MAX_BYTES]);
size_t mqtt_encode_connect(uint8_t *buf, size_t max, const char *client_id, const char *user,
                           const char *pass, uint16_t keepalive_s, const char *will_topic,
                           const char *will_msg, bool clean);
size_t mqtt_encode_publish(uint8_t *buf, size_t max, const char *topic, const void *payload,
                           size_t payload_len, uint8_t qos, bool retain, uint16_t packet_id);
/* Everything of a PUBLISH before its payload (the payload follows in the stream) */
size_t mqtt_encode_publish_header(uint8_t *buf, size_t max, const char *topic, size_t payload_len,
                                  uint8_t qos, bool retain, uint16_t packet_id);
size_t mqtt_encode_subscribe(uint8_t *buf, size_t max, uint16_t packet_id, const char *topic, uint8_t qos);
size_t mqtt_encode_puback(uint8_t *buf, size_t max, uint16_t packet_id);
size_t mqtt_encode_simple(uint8_t *buf, size_t max, uint8_t type);   /* PINGREQ, DISCONNECT */

typedef struct {
    const char    *topic;                /* not NUL-terminated */
    uint16_t       topic_len;
    const uint8_t *payload;
    size_t         payload_len;
    uint8_t        qos;
    bool           retain;
    uint16_t       packet_id;
} mqtt_publish_t;

/* body = bytes after the fixed header */
bool mqtt_parse_publish(uint8_t first_byte, const uint8_t *body, size_t body_len, mqtt_publish_t *out);

/* Stream reassembly: packets are handed over whole; ones over MQTT_RX_BUF_LEN are skipped */
typedef void (*mqtt_packet_fn)(void *arg, uint8_t first_byte, const uint8_t *body, size_t body_len);
typedef struct {
    uint8_t  buf[MQTT_RX_BUF_LEN];
    size_t   len;
    uint32_t skip;                       /* bytes of an oversized packet still to drop */
    uint32_t oversized;
    bool     malformed;                  /* remaining length over 4 bytes: stream unusable */
} mqtt_rx_t;
void mqtt_rx_reset(mqtt_rx_t *rx);
void mqtt_rx_feed(mqtt_rx_t *rx, const uint8_t *data, size_t len, mqtt_packet_fn fn, void *arg);

/* ------------------------------------------------------------------------- */
/* Client                                                                    */
/* ------------------------------------------------------------------------- */
void mqtt_init(void);                                    /* identity, topics, NVS settings */
void mqtt_tick(uint64_t now_us, bool link_up);
bool mqtt_link_usable(void);                             /* STA joined and an IPv4 lease bound */
void mqtt_get_status(mqtt_status_t *out);
void mqtt_get_config(mqtt_config_t *out);
/* Saves to NVS and restarts the session with the new settings (graceful disconnect first) */
bool mqtt_set_config(const mqtt_config_t *cfg, uint64_t now_us);
const char *mqtt_device_id(void);
const char *mqtt_topic_set(void);
const char *mqtt_topic_state(void);
const char *mqtt_topic_availability(void);
const char *mqtt_topic_discovery(void);
const char *mqtt_state_str(mqtt_state_t st);
const char *mqtt_state_class_str(mqtt_state_t st);      /* off / connecting / connected / backoff */
const char *mqtt_error_str(mqtt_error_t err);
/* HA discovery document (spec section 4) */
size_t mqtt_build_discovery(char *buf, size_t max);
void mqtt_shell(const char *args, uint64_t now_us);

#endif /* IRON_V_MQTT_H */
