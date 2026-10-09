/*
 * Iron V - MQTT 3.1.1 client (REV-23). See mqtt.h and docs/reference/property-model-v1.md.
 */
#include "mqtt.h"
#include "light.h"
#include "tcp.h"
#include "net.h"
#include "nvs.h"
#include "device.h"
#include "wifi.h"
#include "dhcp.h"
#include "wifi_link.h"
#include "hw_rng.h"
#include "config.h"
#include "string.h"

#if defined(__riscv)
#include "console.h"
#else
#include <stdio.h>
#define console_puts(s) printf("%s", (s))
#endif

#define MQTT_DISCOVERY_MAX               640U
#define MQTT_DEC_BUF_LEN                 12U
#define MQTT_DEC_BASE                    10U
#define MQTT_PORT_MAX                    65535U
#define MQTT_LINE_MAX                    96U
#define MQTT_US_PER_MS                   1000U

/* ========================================================================= */
/* Text helpers                                                              */
/* ========================================================================= */

static bool mqtt_append(char *buf, size_t max, size_t *pos, const char *src)
{
    size_t n = strlen(src);
    if (*pos + n + 1U > max)
    {
        return false;
    }
    memcpy(buf + *pos, src, n);
    *pos += n;
    buf[*pos] = '\0';
    return true;
}

static void mqtt_u32_to_dec(uint32_t v, char *out, size_t max)
{
    char tmp[MQTT_DEC_BUF_LEN];
    size_t n = 0U;
    do
    {
        tmp[n++] = (char)('0' + (v % MQTT_DEC_BASE));
        v /= MQTT_DEC_BASE;
    } while (v != 0U && n < sizeof(tmp));
    if (n + 1U > max)
    {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0U; i < n; i++)
    {
        out[i] = tmp[n - 1U - i];
    }
    out[n] = '\0';
}

static bool mqtt_append_u32(char *buf, size_t max, size_t *pos, uint32_t v)
{
    char num[MQTT_DEC_BUF_LEN];
    mqtt_u32_to_dec(v, num, sizeof(num));
    return mqtt_append(buf, max, pos, num);
}

/* ========================================================================= */
/* Packet codec                                                              */
/* ========================================================================= */

size_t mqtt_encode_remaining_length(uint32_t len, uint8_t out[MQTT_REMLEN_MAX_BYTES])
{
    size_t n = 0U;
    do
    {
        uint8_t digit = (uint8_t)(len & MQTT_REMLEN_DIGIT_MASK);
        len >>= MQTT_REMLEN_SHIFT;
        if (len > 0U)
        {
            digit |= MQTT_REMLEN_CONTINUE;
        }
        if (n >= MQTT_REMLEN_MAX_BYTES)
        {
            return 0U;
        }
        out[n++] = digit;
    } while (len > 0U);
    return n;
}

typedef struct {
    uint8_t *buf;
    size_t   max;
    size_t   pos;
    bool     ok;
} mqtt_w_t;

static void mw_u8(mqtt_w_t *w, uint8_t v)
{
    if (w->pos + 1U > w->max)
    {
        w->ok = false;
        return;
    }
    w->buf[w->pos++] = v;
}

static void mw_u16(mqtt_w_t *w, uint16_t v)
{
    mw_u8(w, (uint8_t)(v >> MQTT_U16_HI_SHIFT));
    mw_u8(w, (uint8_t)(v & MQTT_BYTE_MASK));
}

static void mw_bytes(mqtt_w_t *w, const void *data, size_t len)
{
    if (w->pos + len > w->max)
    {
        w->ok = false;
        return;
    }
    if (len > 0U)
    {
        memcpy(w->buf + w->pos, data, len);
    }
    w->pos += len;
}

static void mw_str(mqtt_w_t *w, const char *s)
{
    size_t n = strlen(s);
    mw_u16(w, (uint16_t)n);
    mw_bytes(w, s, n);
}

/* Fixed header for a packet whose remaining part is rem bytes */
static void mw_header(mqtt_w_t *w, uint8_t first, size_t rem)
{
    uint8_t enc[MQTT_REMLEN_MAX_BYTES];
    size_t n = mqtt_encode_remaining_length((uint32_t)rem, enc);
    if (n == 0U)
    {
        w->ok = false;
        return;
    }
    mw_u8(w, first);
    mw_bytes(w, enc, n);
}

static size_t mw_done(const mqtt_w_t *w)
{
    return w->ok ? w->pos : 0U;
}

size_t mqtt_encode_connect(uint8_t *buf, size_t max, const char *client_id, const char *user,
                           const char *pass, uint16_t keepalive_s, const char *will_topic,
                           const char *will_msg, bool clean)
{
    bool has_user = user != NULL && user[0] != '\0';
    bool has_pass = has_user && pass != NULL && pass[0] != '\0';   /* 3.1.1: no password without a user */
    bool has_will = will_topic != NULL && will_msg != NULL;
    uint8_t flags = clean ? MQTT_CONN_FLAG_CLEAN : 0U;
    size_t rem = MQTT_STR_LEN_BYTES + strlen(MQTT_PROTOCOL_NAME) + 1U + 1U + 2U +
                 MQTT_STR_LEN_BYTES + strlen(client_id);
    if (has_will)
    {
        flags |= MQTT_CONN_FLAG_WILL | (uint8_t)(MQTT_QOS1 << MQTT_CONN_WILL_QOS_SHIFT) | MQTT_CONN_FLAG_WILL_RETAIN;
        rem += MQTT_STR_LEN_BYTES + strlen(will_topic) + MQTT_STR_LEN_BYTES + strlen(will_msg);
    }
    if (has_user)
    {
        flags |= MQTT_CONN_FLAG_USERNAME;
        rem += MQTT_STR_LEN_BYTES + strlen(user);
    }
    if (has_pass)
    {
        flags |= MQTT_CONN_FLAG_PASSWORD;
        rem += MQTT_STR_LEN_BYTES + strlen(pass);
    }

    mqtt_w_t w = {buf, max, 0U, true};
    mw_header(&w, MQTT_PKT_CONNECT, rem);
    mw_str(&w, MQTT_PROTOCOL_NAME);
    mw_u8(&w, MQTT_PROTOCOL_LEVEL);
    mw_u8(&w, flags);
    mw_u16(&w, keepalive_s);
    mw_str(&w, client_id);
    if (has_will)
    {
        mw_str(&w, will_topic);
        mw_str(&w, will_msg);
    }
    if (has_user)
    {
        mw_str(&w, user);
    }
    if (has_pass)
    {
        mw_str(&w, pass);
    }
    return mw_done(&w);
}

size_t mqtt_encode_publish_header(uint8_t *buf, size_t max, const char *topic, size_t payload_len,
                                  uint8_t qos, bool retain, uint16_t packet_id)
{
    size_t rem = MQTT_STR_LEN_BYTES + strlen(topic) + payload_len + ((qos > MQTT_QOS0) ? MQTT_PACKET_ID_LEN : 0U);
    uint8_t first = (uint8_t)(MQTT_PKT_PUBLISH | ((qos & MQTT_PUB_QOS_MASK) << MQTT_PUB_QOS_SHIFT) |
                              (retain ? MQTT_PUB_FLAG_RETAIN : 0U));
    mqtt_w_t w = {buf, max, 0U, true};
    mw_header(&w, first, rem);
    mw_str(&w, topic);
    if (qos > MQTT_QOS0)
    {
        mw_u16(&w, packet_id);
    }
    return mw_done(&w);
}

size_t mqtt_encode_publish(uint8_t *buf, size_t max, const char *topic, const void *payload,
                           size_t payload_len, uint8_t qos, bool retain, uint16_t packet_id)
{
    size_t hdr = mqtt_encode_publish_header(buf, max, topic, payload_len, qos, retain, packet_id);
    if (hdr == 0U || hdr + payload_len > max)
    {
        return 0U;
    }
    if (payload_len > 0U)
    {
        memcpy(buf + hdr, payload, payload_len);
    }
    return hdr + payload_len;
}

size_t mqtt_encode_subscribe(uint8_t *buf, size_t max, uint16_t packet_id, const char *topic, uint8_t qos)
{
    size_t rem = MQTT_PACKET_ID_LEN + MQTT_STR_LEN_BYTES + strlen(topic) + 1U;
    mqtt_w_t w = {buf, max, 0U, true};
    mw_header(&w, MQTT_PKT_SUBSCRIBE, rem);
    mw_u16(&w, packet_id);
    mw_str(&w, topic);
    mw_u8(&w, qos);
    return mw_done(&w);
}

size_t mqtt_encode_puback(uint8_t *buf, size_t max, uint16_t packet_id)
{
    mqtt_w_t w = {buf, max, 0U, true};
    mw_header(&w, MQTT_PKT_PUBACK, MQTT_PACKET_ID_LEN);
    mw_u16(&w, packet_id);
    return mw_done(&w);
}

size_t mqtt_encode_simple(uint8_t *buf, size_t max, uint8_t type)
{
    mqtt_w_t w = {buf, max, 0U, true};
    mw_header(&w, type, 0U);
    return mw_done(&w);
}

bool mqtt_parse_publish(uint8_t first_byte, const uint8_t *body, size_t body_len, mqtt_publish_t *out)
{
    if ((first_byte & MQTT_PKT_TYPE_MASK) != MQTT_PKT_PUBLISH || body_len < MQTT_STR_LEN_BYTES)
    {
        return false;
    }
    uint8_t qos = (uint8_t)((first_byte >> MQTT_PUB_QOS_SHIFT) & MQTT_PUB_QOS_MASK);
    if (qos > MQTT_QOS1 + 1U)
    {
        return false;   /* QoS 3 is reserved */
    }
    size_t tlen = ((size_t)body[0] << MQTT_U16_HI_SHIFT) | body[1];
    size_t pos = MQTT_STR_LEN_BYTES + tlen;
    if (pos > body_len)
    {
        return false;
    }
    out->topic = (const char *)(body + MQTT_STR_LEN_BYTES);
    out->topic_len = (uint16_t)tlen;
    out->qos = qos;
    out->retain = (first_byte & MQTT_PUB_FLAG_RETAIN) != 0U;
    out->packet_id = 0U;
    if (qos > MQTT_QOS0)
    {
        if (pos + MQTT_PACKET_ID_LEN > body_len)
        {
            return false;
        }
        out->packet_id = (uint16_t)(((uint16_t)body[pos] << MQTT_U16_HI_SHIFT) | body[pos + 1U]);
        pos += MQTT_PACKET_ID_LEN;
    }
    out->payload = body + pos;
    out->payload_len = body_len - pos;
    return true;
}

void mqtt_rx_reset(mqtt_rx_t *rx)
{
    rx->len = 0U;
    rx->skip = 0U;
    rx->malformed = false;
}

/* Fixed header: >0 header length and *rem filled; 0 incomplete; -1 malformed */
static int mqtt_rx_header(const uint8_t *buf, size_t len, uint32_t *rem)
{
    uint32_t value = 0U;
    uint32_t shift = 0U;
    for (size_t i = 1U; i <= MQTT_REMLEN_MAX_BYTES; i++)
    {
        if (i >= len)
        {
            return 0;
        }
        value |= (uint32_t)(buf[i] & MQTT_REMLEN_DIGIT_MASK) << shift;
        if ((buf[i] & MQTT_REMLEN_CONTINUE) == 0U)
        {
            *rem = value;
            return (int)(i + 1U);
        }
        shift += MQTT_REMLEN_SHIFT;
    }
    return -1;
}

void mqtt_rx_feed(mqtt_rx_t *rx, const uint8_t *data, size_t len, mqtt_packet_fn fn, void *arg)
{
    size_t i = 0U;
    while (i < len && !rx->malformed)
    {
        if (rx->skip > 0U)
        {
            size_t n = len - i;
            if (n > rx->skip)
            {
                n = rx->skip;
            }
            rx->skip -= (uint32_t)n;
            i += n;
            continue;
        }
        rx->buf[rx->len++] = data[i++];
        uint32_t rem = 0U;
        int hdr = mqtt_rx_header(rx->buf, rx->len, &rem);
        if (hdr < 0)
        {
            rx->malformed = true;
            break;
        }
        if (hdr == 0)
        {
            continue;
        }
        size_t total = (size_t)hdr + rem;
        if (total > MQTT_RX_BUF_LEN)
        {
            rx->skip = (uint32_t)(total - rx->len);
            rx->len = 0U;
            rx->oversized++;
            continue;
        }
        if (rx->len == total)
        {
            fn(arg, rx->buf[0], rx->buf + hdr, rem);
            rx->len = 0U;
        }
    }
}

/* ========================================================================= */
/* Client state                                                              */
/* ========================================================================= */

static mqtt_config_t s_cfg;
static mqtt_status_t s_st;
static mqtt_rx_t     s_rx;
static tcp_pcb_t    *s_pcb;
static uint16_t      s_pcb_port;            /* our local port: proves the slot is still ours */
static bool          s_pcb_failed;          /* the stack dropped the connection */
static tcp_status_t  s_pcb_err;
static uint32_t      s_session_ip;          /* our address when the session started */
static uint32_t      s_hop_ip;
static uint32_t      s_arp_tries;
static uint64_t      s_deadline_us;
static uint64_t      s_last_tx_us;
static bool          s_ping_outstanding;
static uint64_t      s_ping_sent_us;
static uint16_t      s_next_packet_id;
static uint64_t      s_now_us;              /* time of the current tick (callbacks use it) */
static mqtt_error_t  s_pending_error;       /* set inside TCP callbacks, acted on by mqtt_tick */
static bool          s_cfg_pending;         /* new settings wait for mqtt_tick (see mqtt_set_config) */
static mqtt_config_t s_cfg_next;
static uint8_t       s_tx[MQTT_TX_BUF_LEN];

static char s_topic_avail[MQTT_TOPIC_MAX];
static char s_topic_state[MQTT_TOPIC_MAX];
static char s_topic_set[MQTT_TOPIC_MAX];
static char s_topic_disc[MQTT_TOPIC_MAX];

/* The stack can re-initialise behind our back (do-test runs tcp_init()); a slot that is no longer
 * ours must never be written, closed or aborted */
static bool mqtt_pcb_owned(void)
{
    return s_pcb != NULL && s_pcb->in_use && s_pcb->local_port == s_pcb_port &&
           s_pcb->remote_ip == s_cfg.host && s_pcb->remote_port == s_cfg.port;
}

static uint16_t mqtt_packet_id(void)
{
    s_next_packet_id++;
    if (s_next_packet_id == 0U)
    {
        s_next_packet_id = 1U;
    }
    return s_next_packet_id;
}

static bool mqtt_send(const uint8_t *data, size_t len)
{
    if (!mqtt_pcb_owned() || len == 0U)
    {
        return false;
    }
    if (tcp_sndbuf_space(s_pcb) < len || tcp_write(s_pcb, data, (uint16_t)len) != TCP_OK)
    {
        s_st.tx_full++;
        return false;
    }
    s_last_tx_us = s_now_us;
    return true;
}

/* Header from s_tx, payload straight from the caller: both land in the TCP send buffer together */
static bool mqtt_publish(const char *topic, const void *payload, size_t len, bool retain)
{
    size_t n = mqtt_encode_publish_header(s_tx, sizeof(s_tx), topic, len, MQTT_QOS1, retain, mqtt_packet_id());
    if (!mqtt_pcb_owned() || n == 0U || tcp_sndbuf_space(s_pcb) < n + len)
    {
        s_st.tx_full++;
        return false;
    }
    if (!mqtt_send(s_tx, n) || (len > 0U && !mqtt_send((const uint8_t *)payload, len)))
    {
        return false;
    }
    s_st.publishes++;
    return true;
}

static bool mqtt_publish_state(void)
{
    light_state_t ls;
    char json[LIGHT_STATE_JSON_MAX];
    light_get(&ls);
    size_t n = light_state_to_json(&ls, json, sizeof(json));
    return n > 0U && mqtt_publish(s_topic_state, json, n, true);
}

/* Drops the connection without a goodbye (the broker publishes the Will) */
static void mqtt_drop_pcb(void)
{
    if (mqtt_pcb_owned())
    {
        tcp_set_recv_cb(s_pcb, NULL);
        tcp_set_err_cb(s_pcb, NULL);
        (void)tcp_abort(s_pcb);
    }
    s_pcb = NULL;
    s_pcb_failed = false;
    s_ping_outstanding = false;
    mqtt_rx_reset(&s_rx);
}

static void mqtt_fail(mqtt_error_t err)
{
    bool was_online = (s_st.state == MQTT_ST_ONLINE);
    s_st.last_error = err;
    mqtt_drop_pcb();
    if (was_online)
    {
        s_st.reconnects++;
        if ((s_now_us - s_st.online_since_us) >= MQTT_STABLE_SESSION_US)
        {
            s_st.retry_delay_us = 0U;   /* a long session: start the backoff over */
        }
    }
    s_st.retry_delay_us = wifi_link_next_backoff_us(s_st.retry_delay_us);
    s_st.next_retry_us = s_now_us + wifi_link_jitter_us(s_st.retry_delay_us, hw_rng_u32());
    s_st.state = MQTT_ST_BACKOFF;
}

/* Polite end: "offline" (retained) since DISCONNECT suppresses the Will, then DISCONNECT and FIN */
static void mqtt_disconnect_gracefully(void)
{
    if (s_st.state == MQTT_ST_ONLINE && mqtt_pcb_owned())
    {
        (void)mqtt_publish(s_topic_avail, MQTT_PAYLOAD_OFFLINE, strlen(MQTT_PAYLOAD_OFFLINE), true);
        size_t n = mqtt_encode_simple(s_tx, sizeof(s_tx), MQTT_PKT_DISCONNECT);
        (void)mqtt_send(s_tx, n);
        tcp_set_recv_cb(s_pcb, NULL);
        tcp_set_err_cb(s_pcb, NULL);
        (void)tcp_close(s_pcb);
        s_pcb = NULL;
        s_ping_outstanding = false;
        mqtt_rx_reset(&s_rx);
    }
    else
    {
        mqtt_drop_pcb();
    }
}

static void mqtt_go_online(void)
{
    s_st.state = MQTT_ST_ONLINE;
    s_st.sessions++;
    s_st.online_since_us = s_now_us;
    s_st.last_error = MQTT_ERR_NONE;
    s_ping_outstanding = false;

    char disc[MQTT_DISCOVERY_MAX];
    size_t dlen = mqtt_build_discovery(disc, sizeof(disc));
    size_t n;
    bool ok = mqtt_publish(s_topic_avail, MQTT_PAYLOAD_ONLINE, strlen(MQTT_PAYLOAD_ONLINE), true) &&
              dlen > 0U && mqtt_publish(s_topic_disc, disc, dlen, true) &&
              mqtt_publish_state();
    if (ok)
    {
        light_mark_reported(s_now_us);
        n = mqtt_encode_subscribe(s_tx, sizeof(s_tx), mqtt_packet_id(), s_topic_set, MQTT_QOS1);
        ok = mqtt_send(s_tx, n);
    }
    if (!ok)
    {
        s_pending_error = MQTT_ERR_TX_FULL;   /* runs inside the TCP callback: no teardown here */
    }
}

static void mqtt_on_publish(const mqtt_publish_t *pub)
{
    if (pub->qos == MQTT_QOS1)
    {
        size_t n = mqtt_encode_puback(s_tx, sizeof(s_tx), pub->packet_id);
        (void)mqtt_send(s_tx, n);
    }
    size_t set_len = strlen(s_topic_set);
    if (pub->topic_len != set_len || memcmp(pub->topic, s_topic_set, set_len) != 0)
    {
        return;
    }
    s_st.commands++;
    if (pub->retain)
    {
        s_st.retained_dropped++;   /* a stale retained command must never replay (principle 5) */
        return;
    }
    if (light_command((const char *)pub->payload, pub->payload_len, LIGHT_SRC_MQTT, s_now_us) != LIGHT_CMD_OK)
    {
        s_st.commands_rejected++;
    }
}

static void mqtt_on_packet(void *arg, uint8_t first, const uint8_t *body, size_t body_len)
{
    (void)arg;
    uint8_t type = first & MQTT_PKT_TYPE_MASK;
    if (type == MQTT_PKT_CONNACK)
    {
        if (s_st.state != MQTT_ST_CONNACK || body_len != MQTT_CONNACK_LEN)
        {
            s_pending_error = MQTT_ERR_PROTOCOL;
            return;
        }
        s_st.last_refusal = body[1];
        if (body[1] == MQTT_CONNACK_ACCEPTED)
        {
            mqtt_go_online();
        }
        else
        {
            s_pending_error = MQTT_ERR_REFUSED;
        }
    }
    else if (type == MQTT_PKT_PUBLISH)
    {
        mqtt_publish_t pub;
        if (s_st.state == MQTT_ST_ONLINE && mqtt_parse_publish(first, body, body_len, &pub))
        {
            mqtt_on_publish(&pub);
        }
    }
    else if (type == MQTT_PKT_PUBACK)
    {
        s_st.pubacks++;
    }
    else if (type == MQTT_PKT_PINGRESP)
    {
        s_ping_outstanding = false;
    }
    /* SUBACK: a failure code leaves the session up but deaf; counted via commands staying 0 */
}

static net_status_t mqtt_tcp_recv(void *arg, tcp_pcb_t *pcb, const uint8_t *data, uint16_t len)
{
    (void)arg;
    if (pcb != s_pcb)
    {
        return NET_OK;
    }
    mqtt_rx_feed(&s_rx, data, len, mqtt_on_packet, NULL);
    s_st.rx_oversized = s_rx.oversized;
    return NET_OK;
}

static void mqtt_tcp_err(void *arg, tcp_status_t err)
{
    (void)arg;
    s_pcb = NULL;   /* already freed by the stack */
    s_pcb_failed = true;
    s_pcb_err = err;
}

static void mqtt_start_tcp(void)
{
    s_pcb = tcp_new();
    if (s_pcb == NULL)
    {
        mqtt_fail(MQTT_ERR_NO_PCB);
        return;
    }
    s_pcb_failed = false;
    mqtt_rx_reset(&s_rx);
    tcp_set_idle_timeout(s_pcb, TCP_IDLE_TIMEOUT_NEVER);   /* keepalive is MQTT's job */
    tcp_set_recv_cb(s_pcb, mqtt_tcp_recv);
    tcp_set_err_cb(s_pcb, mqtt_tcp_err);
    if (tcp_connect(s_pcb, s_cfg.host, s_cfg.port) != TCP_OK)
    {
        mqtt_fail(MQTT_ERR_NO_PCB);
        return;
    }
    s_pcb_port = s_pcb->local_port;
    s_st.state = MQTT_ST_TCP;
}

static void mqtt_start_attempt(void)
{
    net_config_t nc;
    net_get_config(&nc);
    s_st.attempts++;
    s_session_ip = nc.ip;
    s_hop_ip = net_next_hop(s_cfg.host);
    if (s_hop_ip == 0U)
    {
        mqtt_fail(MQTT_ERR_NO_ROUTE);
        return;
    }
    uint8_t mac[ETH_ADDR_LEN];
    if (arp_lookup(s_hop_ip, mac) == NET_OK)
    {
        mqtt_start_tcp();
        return;
    }
    s_arp_tries = 1U;
    s_st.arp_requests++;
    (void)net_arp_request(s_hop_ip);
    s_deadline_us = s_now_us + MQTT_ARP_RETRY_US;
    s_st.state = MQTT_ST_ARP;
}

static mqtt_error_t mqtt_pcb_error(void)
{
    if (s_pcb_err == TCP_ERR_RST)
    {
        return MQTT_ERR_TCP_RESET;
    }
    if (s_pcb_err == TCP_ERR_TIMEOUT)
    {
        return MQTT_ERR_TCP_TIMEOUT;
    }
    return MQTT_ERR_PROTOCOL;
}

void mqtt_tick(uint64_t now_us, bool link_up)
{
    s_now_us = now_us;
    if (s_cfg_pending)
    {
        /* New settings: end the current session politely and start over without the backoff wait */
        s_cfg_pending = false;
        mqtt_disconnect_gracefully();
        s_cfg = s_cfg_next;
        s_st.state = MQTT_ST_OFF;
        s_st.retry_delay_us = 0U;
        s_pending_error = MQTT_ERR_NONE;
    }
    bool configured = s_cfg.enabled && s_cfg.host != 0U;

    if (!configured)
    {
        if (s_st.state != MQTT_ST_OFF)
        {
            mqtt_disconnect_gracefully();
            s_st.state = MQTT_ST_OFF;
        }
        return;
    }
    if (s_pending_error != MQTT_ERR_NONE)
    {
        mqtt_error_t err = s_pending_error;
        s_pending_error = MQTT_ERR_NONE;
        mqtt_fail(err);
        return;
    }
    if (s_rx.malformed)
    {
        mqtt_fail(MQTT_ERR_PROTOCOL);
        return;
    }
    if (s_pcb_failed && (s_st.state == MQTT_ST_TCP || s_st.state == MQTT_ST_CONNACK || s_st.state == MQTT_ST_ONLINE))
    {
        mqtt_fail(mqtt_pcb_error());
        return;
    }

    switch (s_st.state)
    {
        case MQTT_ST_OFF:
            s_st.state = MQTT_ST_WAIT_LINK;
            s_st.retry_delay_us = 0U;
            break;

        case MQTT_ST_BACKOFF:
            if ((int64_t)(now_us - s_st.next_retry_us) < 0)
            {
                break;
            }
            s_st.state = MQTT_ST_WAIT_LINK;
            /* fall through */
        case MQTT_ST_WAIT_LINK:
            if (link_up)
            {
                mqtt_start_attempt();
            }
            break;

        case MQTT_ST_ARP:
        {
            uint8_t mac[ETH_ADDR_LEN];
            if (arp_lookup(s_hop_ip, mac) == NET_OK)
            {
                mqtt_start_tcp();
            }
            else if ((int64_t)(now_us - s_deadline_us) >= 0)
            {
                if (s_arp_tries >= MQTT_ARP_TRIES)
                {
                    mqtt_fail(MQTT_ERR_ARP_TIMEOUT);
                }
                else
                {
                    s_arp_tries++;
                    s_st.arp_requests++;
                    (void)net_arp_request(s_hop_ip);
                    s_deadline_us = now_us + MQTT_ARP_RETRY_US;
                }
            }
            break;
        }

        case MQTT_ST_TCP:
            if (!mqtt_pcb_owned())
            {
                s_pcb = NULL;
                mqtt_fail(MQTT_ERR_TCP_RESET);
                break;
            }
            if (s_pcb->state == TCP_STATE_ESTABLISHED)
            {
                size_t n = mqtt_encode_connect(s_tx, sizeof(s_tx), device_id(), s_cfg.user, s_cfg.pass,
                                               MQTT_KEEPALIVE_S, s_topic_avail, MQTT_PAYLOAD_OFFLINE, true);
                if (!mqtt_send(s_tx, n))
                {
                    mqtt_fail(MQTT_ERR_TX_FULL);
                    break;
                }
                s_deadline_us = now_us + MQTT_CONNACK_TIMEOUT_US;
                s_st.state = MQTT_ST_CONNACK;
            }
            break;

        case MQTT_ST_CONNACK:
            if (!mqtt_pcb_owned())
            {
                s_pcb = NULL;
                mqtt_fail(MQTT_ERR_TCP_RESET);
            }
            else if ((int64_t)(now_us - s_deadline_us) >= 0)
            {
                mqtt_fail(MQTT_ERR_CONNACK_TIMEOUT);
            }
            break;

        case MQTT_ST_ONLINE:
        {
            net_config_t nc;
            net_get_config(&nc);
            if (nc.ip != s_session_ip)
            {
                mqtt_fail(MQTT_ERR_IP_CHANGED);
                break;
            }
            if (!mqtt_pcb_owned() || s_pcb->state != TCP_STATE_ESTABLISHED)
            {
                mqtt_fail(MQTT_ERR_PEER_CLOSED);
                break;
            }
            if (s_ping_outstanding && (now_us - s_ping_sent_us) >= MQTT_PINGRESP_TIMEOUT_US)
            {
                mqtt_fail(MQTT_ERR_PING_TIMEOUT);
                break;
            }
            /* State report (coalesced); only taken when it fits, so a full buffer just delays it */
            if (tcp_sndbuf_space(s_pcb) >= MQTT_TX_BUF_LEN + LIGHT_STATE_JSON_MAX && light_take_report(now_us))
            {
                (void)mqtt_publish_state();
            }
            if (!s_ping_outstanding && (now_us - s_last_tx_us) >= MQTT_KEEPALIVE_US)
            {
                size_t n = mqtt_encode_simple(s_tx, sizeof(s_tx), MQTT_PKT_PINGREQ);
                if (mqtt_send(s_tx, n))
                {
                    s_ping_outstanding = true;
                    s_ping_sent_us = now_us;
                    s_st.pings++;
                }
            }
            break;
        }

        default:
            break;
    }
}

bool mqtt_link_usable(void)
{
    net_config_t nc;
    net_get_config(&nc);
    dhcp_client_state_t d = dhcp_client_get_state();
    bool leased = d == DHCP_CLIENT_STATE_BOUND || d == DHCP_CLIENT_STATE_RENEWING ||
                  d == DHCP_CLIENT_STATE_REBINDING || d == DHCP_CLIENT_STATE_STATIC;
    return wifi_is_sta_connected() && leased && nc.ip != 0U;
}

/* ========================================================================= */
/* Identity, settings                                                        */
/* ========================================================================= */

static void mqtt_build_topic(char *out, const char *prefix, const char *suffix)
{
    size_t pos = 0U;
    out[0] = '\0';
    (void)(mqtt_append(out, MQTT_TOPIC_MAX, &pos, prefix) &&
           mqtt_append(out, MQTT_TOPIC_MAX, &pos, device_id()) &&
           mqtt_append(out, MQTT_TOPIC_MAX, &pos, suffix));
}

static void mqtt_load_config(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.port = MQTT_DEFAULT_PORT;
    uint32_t v = 0U;
    if (nvs_get_u32(MQTT_NVS_KEY_ENABLED, &v) == NVS_OK)
    {
        s_cfg.enabled = (v != 0U);
    }
    if (nvs_get_u32(MQTT_NVS_KEY_HOST, &v) == NVS_OK)
    {
        s_cfg.host = v;
    }
    if (nvs_get_u32(MQTT_NVS_KEY_PORT, &v) == NVS_OK && v != 0U && v <= MQTT_PORT_MAX)
    {
        s_cfg.port = (uint16_t)v;
    }
    if (nvs_get_str(MQTT_NVS_KEY_USER, s_cfg.user, sizeof(s_cfg.user)) != NVS_OK)
    {
        s_cfg.user[0] = '\0';
    }
    if (nvs_get_str(MQTT_NVS_KEY_PASS, s_cfg.pass, sizeof(s_cfg.pass)) != NVS_OK)
    {
        s_cfg.pass[0] = '\0';
    }
}

void mqtt_init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    mqtt_rx_reset(&s_rx);
    s_rx.oversized = 0U;
    s_pcb = NULL;
    s_pcb_failed = false;
    s_ping_outstanding = false;
    s_pending_error = MQTT_ERR_NONE;
    s_cfg_pending = false;
    s_next_packet_id = (uint16_t)hw_rng_u32();

    mqtt_build_topic(s_topic_avail, MQTT_TOPIC_ROOT, MQTT_TOPIC_AVAIL_SUFFIX);
    mqtt_build_topic(s_topic_state, MQTT_TOPIC_ROOT, MQTT_TOPIC_STATE_SUFFIX);
    mqtt_build_topic(s_topic_set, MQTT_TOPIC_ROOT, MQTT_TOPIC_SET_SUFFIX);
    mqtt_build_topic(s_topic_disc, MQTT_TOPIC_DISCOVERY_PREFIX, MQTT_TOPIC_DISCOVERY_SUFFIX);

    mqtt_load_config();
}

void mqtt_get_status(mqtt_status_t *out)
{
    *out = s_st;
}

void mqtt_get_config(mqtt_config_t *out)
{
    *out = s_cfg_pending ? s_cfg_next : s_cfg;
}

bool mqtt_set_config(const mqtt_config_t *cfg, uint64_t now_us)
{
    if (cfg == NULL || cfg->port == 0U || strlen(cfg->user) > MQTT_USER_MAX_LEN ||
        strlen(cfg->pass) > MQTT_PASS_MAX_LEN)
    {
        return false;
    }
    bool ok = nvs_set_u32(MQTT_NVS_KEY_ENABLED, cfg->enabled ? 1U : 0U) == NVS_OK &&
              nvs_set_u32(MQTT_NVS_KEY_HOST, cfg->host) == NVS_OK &&
              nvs_set_u32(MQTT_NVS_KEY_PORT, cfg->port) == NVS_OK &&
              nvs_set_str(MQTT_NVS_KEY_USER, cfg->user) == NVS_OK &&
              nvs_set_str(MQTT_NVS_KEY_PASS, cfg->pass) == NVS_OK;
    if (!ok)
    {
        return false;
    }
    /* Applied by the next mqtt_tick: callers may run inside another connection's TCP callback */
    (void)now_us;
    s_cfg_next = *cfg;
    s_cfg_pending = true;
    return true;
}

const char *mqtt_device_id(void)          { return device_id(); }
const char *mqtt_topic_set(void)          { return s_topic_set; }
const char *mqtt_topic_state(void)        { return s_topic_state; }
const char *mqtt_topic_availability(void) { return s_topic_avail; }
const char *mqtt_topic_discovery(void)    { return s_topic_disc; }

const char *mqtt_state_str(mqtt_state_t st)
{
    switch (st)
    {
        case MQTT_ST_OFF:       return "off";
        case MQTT_ST_WAIT_LINK: return "waiting_link";
        case MQTT_ST_ARP:       return "arp";
        case MQTT_ST_TCP:       return "tcp_connect";
        case MQTT_ST_CONNACK:   return "connack_wait";
        case MQTT_ST_ONLINE:    return "connected";
        case MQTT_ST_BACKOFF:   return "backoff";
        default:                return "unknown";
    }
}

const char *mqtt_state_class_str(mqtt_state_t st)
{
    switch (st)
    {
        case MQTT_ST_OFF:     return "off";
        case MQTT_ST_ONLINE:  return "connected";
        case MQTT_ST_BACKOFF: return "backoff";
        default:              return "connecting";
    }
}

const char *mqtt_error_str(mqtt_error_t err)
{
    switch (err)
    {
        case MQTT_ERR_NONE:            return "none";
        case MQTT_ERR_ARP_TIMEOUT:     return "arp_timeout";
        case MQTT_ERR_TCP_TIMEOUT:     return "tcp_timeout";
        case MQTT_ERR_TCP_RESET:       return "tcp_reset";
        case MQTT_ERR_PEER_CLOSED:     return "peer_closed";
        case MQTT_ERR_CONNACK_TIMEOUT: return "connack_timeout";
        case MQTT_ERR_REFUSED:         return "refused";
        case MQTT_ERR_PING_TIMEOUT:    return "ping_timeout";
        case MQTT_ERR_PROTOCOL:        return "protocol";
        case MQTT_ERR_IP_CHANGED:      return "ip_changed";
        case MQTT_ERR_NO_PCB:          return "no_pcb";
        case MQTT_ERR_TX_FULL:         return "tx_full";
        case MQTT_ERR_NO_ROUTE:        return "no_route";
        default:                       return "unknown";
    }
}

size_t mqtt_build_discovery(char *buf, size_t max)
{
    size_t pos = 0U;
    buf[0] = '\0';
    bool ok = mqtt_append(buf, max, &pos, "{\"name\":null,\"unique_id\":\"") &&
              mqtt_append(buf, max, &pos, device_id()) &&
              mqtt_append(buf, max, &pos, "_light\",\"schema\":\"json\",\"command_topic\":\"") &&
              mqtt_append(buf, max, &pos, s_topic_set) &&
              mqtt_append(buf, max, &pos, "\",\"state_topic\":\"") &&
              mqtt_append(buf, max, &pos, s_topic_state) &&
              mqtt_append(buf, max, &pos, "\",\"availability_topic\":\"") &&
              mqtt_append(buf, max, &pos, s_topic_avail) &&
              mqtt_append(buf, max, &pos, "\",\"payload_available\":\"" MQTT_PAYLOAD_ONLINE
                                          "\",\"payload_not_available\":\"" MQTT_PAYLOAD_OFFLINE
                                          "\",\"brightness\":true,\"brightness_scale\":") &&
              mqtt_append_u32(buf, max, &pos, LIGHT_BRIGHTNESS_MAX) &&
              mqtt_append(buf, max, &pos, ",\"supported_color_modes\":[\"rgb\"],\"qos\":1,\"retain\":false,"
                                          "\"optimistic\":false,\"device\":{\"identifiers\":[\"") &&
              mqtt_append(buf, max, &pos, device_id()) &&
              mqtt_append(buf, max, &pos, "\"],\"name\":\"" CONFIG_DEVICE_HOSTNAME
                                          "\",\"manufacturer\":\"" CONFIG_DEVICE_MANUFACTURER
                                          "\",\"model\":\"" CONFIG_DEVICE_MODEL_NUMBER
                                          "\",\"sw_version\":\"" CONFIG_FIRMWARE_REVISION "\"}}");
    if (!ok)
    {
        buf[0] = '\0';
        return 0U;
    }
    return pos;
}

/* ========================================================================= */
/* Shell: mqtt | mqtt on|off | mqtt set host|port|user|pass <v>             */
/* ========================================================================= */

static void mqtt_print_kv(const char *key, const char *val)
{
    console_puts("  ");
    console_puts(key);
    console_puts(val);
    console_puts("\r\n");
}

static void mqtt_print_num(const char *key, uint32_t v)
{
    char num[MQTT_DEC_BUF_LEN];
    mqtt_u32_to_dec(v, num, sizeof(num));
    mqtt_print_kv(key, num);
}

static void mqtt_shell_status(uint64_t now_us)
{
    mqtt_config_t cfg;
    mqtt_get_config(&cfg);
    char ip[NET_IP_STR_BUF_LEN];
    net_ip_to_str(cfg.host, ip, sizeof(ip));
    console_puts("MQTT client (REV-23)\r\n");
    mqtt_print_kv("device id:   ", device_id());
    mqtt_print_kv("enabled:     ", cfg.enabled ? "yes" : "no");
    mqtt_print_kv("broker:      ", cfg.host != 0U ? ip : "(not set)");
    mqtt_print_num("port:        ", cfg.port);
    mqtt_print_kv("user:        ", cfg.user[0] != '\0' ? cfg.user : "(none)");
    mqtt_print_kv("password:    ", cfg.pass[0] != '\0' ? "(set)" : "(none)");
    mqtt_print_kv("state:       ", mqtt_state_str(s_st.state));
    mqtt_print_kv("last error:  ", mqtt_error_str(s_st.last_error));
    if (s_st.last_error == MQTT_ERR_REFUSED)
    {
        mqtt_print_num("refusal:     ", s_st.last_refusal);
    }
    if (s_st.state == MQTT_ST_BACKOFF)
    {
        uint64_t left = ((int64_t)(s_st.next_retry_us - now_us) > 0) ? (s_st.next_retry_us - now_us) : 0U;
        mqtt_print_num("retry in ms: ", (uint32_t)(left / MQTT_US_PER_MS));
    }
    if (s_st.state == MQTT_ST_ONLINE)
    {
        mqtt_print_num("online s:    ", (uint32_t)((now_us - s_st.online_since_us) / MQTT_US_PER_S));
    }
    mqtt_print_num("attempts:    ", s_st.attempts);
    mqtt_print_num("sessions:    ", s_st.sessions);
    mqtt_print_num("reconnects:  ", s_st.reconnects);
    mqtt_print_num("publishes:   ", s_st.publishes);
    mqtt_print_num("pubacks:     ", s_st.pubacks);
    mqtt_print_num("commands:    ", s_st.commands);
    mqtt_print_num("cmd rejected:", s_st.commands_rejected);
    mqtt_print_num("retained drop:", s_st.retained_dropped);
    mqtt_print_num("pings:       ", s_st.pings);
    mqtt_print_num("arp requests:", s_st.arp_requests);
    mqtt_print_num("tx full:     ", s_st.tx_full);
    mqtt_print_num("rx oversized:", s_st.rx_oversized);
    mqtt_print_kv("set topic:   ", s_topic_set);
}

static bool mqtt_parse_port(const char *s, uint16_t *out)
{
    uint32_t v = 0U;
    if (*s == '\0')
    {
        return false;
    }
    for (; *s != '\0'; s++)
    {
        if (*s < '0' || *s > '9')
        {
            return false;
        }
        v = v * MQTT_DEC_BASE + (uint32_t)(*s - '0');
        if (v > MQTT_PORT_MAX)
        {
            return false;
        }
    }
    if (v == 0U)
    {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

void mqtt_shell(const char *args, uint64_t now_us)
{
    while (*args == ' ')
    {
        args++;
    }
    if (*args == '\0' || strcmp(args, "status") == 0)
    {
        mqtt_shell_status(now_us);
        return;
    }
    mqtt_config_t cfg;
    mqtt_get_config(&cfg);
    if (strcmp(args, "on") == 0 || strcmp(args, "off") == 0)
    {
        cfg.enabled = (args[1] == 'n');
        if (cfg.enabled && cfg.host == 0U)
        {
            console_puts("Set a broker first: mqtt set host <a.b.c.d>\r\n");
            return;
        }
    }
    else if (strncmp(args, "set host ", 9) == 0)
    {
        uint32_t ip = net_str_to_ip(args + 9);
        if (ip == 0U)
        {
            console_puts("Usage: mqtt set host <a.b.c.d> (IPv4 only, no DNS)\r\n");
            return;
        }
        cfg.host = ip;
    }
    else if (strncmp(args, "set port ", 9) == 0)
    {
        if (!mqtt_parse_port(args + 9, &cfg.port))
        {
            console_puts("Usage: mqtt set port <1-65535>\r\n");
            return;
        }
    }
    else if (strncmp(args, "set user", 8) == 0 && (args[8] == ' ' || args[8] == '\0'))
    {
        const char *v = (args[8] == ' ') ? args + 9 : "";
        if (strlen(v) > MQTT_USER_MAX_LEN)
        {
            console_puts("User name too long\r\n");
            return;
        }
        strcpy(cfg.user, v);
    }
    else if (strncmp(args, "set pass", 8) == 0 && (args[8] == ' ' || args[8] == '\0'))
    {
        const char *v = (args[8] == ' ') ? args + 9 : "";
        if (strlen(v) > MQTT_PASS_MAX_LEN)
        {
            console_puts("Password too long\r\n");
            return;
        }
        strcpy(cfg.pass, v);
    }
    else
    {
        console_puts("Usage: mqtt [status] | mqtt on|off | mqtt set host <ip> | mqtt set port <n> |\r\n"
                     "       mqtt set user [<name>] | mqtt set pass [<password>]   (empty value clears)\r\n");
        return;
    }
    if (!mqtt_set_config(&cfg, now_us))
    {
        console_puts("Saving the MQTT settings failed\r\n");
        return;
    }
    console_puts("Saved.\r\n");
    mqtt_shell_status(now_us);
}
