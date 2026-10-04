/*
 * src/mdns.c
 *
 * Bare-Metal Multicast DNS (mDNS) Responder Engine
 * RFC 6762 (Multicast DNS), RFC 6763 (DNS-Based Service Discovery)
 *
 * Implements zero-allocation freestanding mDNS resolution:
 * - Listens on UDP port 5353 (multicast group 224.0.0.251)
 * - Broadcasts and resolves "iron-v.local" IPv4 host address (Type A)
 * - Synthesizes unsolicited announcement packets on LAN join / DHCP acquisition
 * - Provides case-insensitive DNS name compression and question matching.
 */

#include "mdns.h"
#include "net.h"
#include "string.h"

#if defined(__riscv)
#include "console.h"
#include "utils.h"
#endif

/* ========================================================================= */
/* Static Storage (Zero Heap Allocation)                                     */
/* ========================================================================= */
static mdns_telemetry_t s_mdns_telem;
static bool             s_mdns_active = false;
static bool             s_mdns_initialized = false;

/* Standard DNS Header Structure */
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_hdr_t;

/* ========================================================================= */
/* Internal Helper Routines (.flash.text)                                    */
/* ========================================================================= */
static char mdns_tolower(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static bool mdns_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        if (mdns_tolower(*s1) != mdns_tolower(*s2))
        {
            return false;
        }
        s1++;
        s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

static size_t mdns_encode_name(const char *hostname, uint8_t *out_buf, size_t max_len)
{
    size_t host_len = strlen(hostname);
    const char domain[] = MDNS_DOMAIN;
    size_t dom_len = strlen(domain);

    size_t total_needed = 1U + host_len + 1U + dom_len + 1U;
    if (total_needed > max_len)
    {
        return 0U;
    }

    size_t pos = 0U;
    /* Hostname label */
    out_buf[pos++] = (uint8_t)host_len;
    memcpy(&out_buf[pos], hostname, host_len);
    pos += host_len;

    /* Domain label ("local") */
    out_buf[pos++] = (uint8_t)dom_len;
    memcpy(&out_buf[pos], domain, dom_len);
    pos += dom_len;

    /* Terminating root label */
    out_buf[pos++] = 0x00U;

    return pos;
}

static bool mdns_parse_query_name(const uint8_t *packet, size_t packet_len,
                                  size_t *inout_offset, char *out_name, size_t max_name_len)
{
    size_t offset = *inout_offset;
    size_t out_pos = 0U;

    while (offset < packet_len)
    {
        uint8_t len = packet[offset++];
        if (len == 0U)
        {
            break;
        }

        /* Check for compression pointer */
        if ((len & 0xC0U) == 0xC0U)
        {
            if (offset >= packet_len) return false;
            uint16_t ptr_offset = (uint16_t)(((uint16_t)(len & 0x3FU) << 8U) | (uint16_t)packet[offset++]);
            size_t temp_offset = ptr_offset;
            char ptr_name[64];
            if (!mdns_parse_query_name(packet, packet_len, &temp_offset, ptr_name, sizeof(ptr_name)))
            {
                return false;
            }
            size_t plen = strlen(ptr_name);
            if ((out_pos + plen + 1U) >= max_name_len) return false;
            if (out_pos > 0U && out_name[out_pos - 1] != '.') out_name[out_pos++] = '.';
            memcpy(&out_name[out_pos], ptr_name, plen);
            out_pos += plen;
            break;
        }

        if ((offset + len) > packet_len)
        {
            return false;
        }

        if (out_pos > 0U && (out_pos + 1U) < max_name_len)
        {
            out_name[out_pos++] = '.';
        }

        if ((out_pos + len) >= max_name_len)
        {
            return false;
        }

        memcpy(&out_name[out_pos], &packet[offset], len);
        out_pos += len;
        offset += len;
    }

    out_name[out_pos] = '\0';
    *inout_offset = offset;
    return true;
}

/* ========================================================================= */
/* mDNS Announcement & Packet Processing (.flash.text)                       */
/* ========================================================================= */
mdns_status_t mdns_announce(void)
{
    if (!s_mdns_active)
    {
        return MDNS_ERR_INVALID_ARG;
    }

    uint8_t packet[MDNS_MAX_PACKET_LEN];
    dns_hdr_t *hdr = (dns_hdr_t *)packet;
    hdr->id = 0U;
    hdr->flags = NET_HTONS(MDNS_FLAGS_RESPONSE_AA);
    hdr->qdcount = 0U;
    hdr->ancount = NET_HTONS(1U);
    hdr->nscount = 0U;
    hdr->arcount = 0U;

    size_t pos = sizeof(dns_hdr_t);

    /* Answer Record Name: <hostname>.local */
    size_t name_len = mdns_encode_name(s_mdns_telem.hostname, &packet[pos], sizeof(packet) - pos);
    if (name_len == 0U) return MDNS_ERR_BUFFER_SMALL;
    pos += name_len;

    /* Type A (0x0001) */
    packet[pos++] = 0x00U; packet[pos++] = (uint8_t)MDNS_TYPE_A;
    /* Class IN + Cache-Flush (0x8001) */
    packet[pos++] = 0x80U; packet[pos++] = (uint8_t)MDNS_CLASS_IN;
    /* TTL: 120 seconds (0x00000078) */
    packet[pos++] = 0x00U; packet[pos++] = 0x00U;
    packet[pos++] = 0x00U; packet[pos++] = (uint8_t)MDNS_DEFAULT_TTL_SEC;
    /* Data Length: 4 bytes */
    packet[pos++] = 0x00U; packet[pos++] = 0x04U;

    /* IPv4 Address */
    net_config_t ncfg;
    net_get_config(&ncfg);
    if (ncfg.ip == 0U)
    {
        return MDNS_ERR_INVALID_ARG;
    }
    uint32_t ip = ncfg.ip;
    s_mdns_telem.advertised_ip = ip;

    packet[pos++] = (uint8_t)((ip >> 24U) & 0xFFU);
    packet[pos++] = (uint8_t)((ip >> 16U) & 0xFFU);
    packet[pos++] = (uint8_t)((ip >> 8U)  & 0xFFU);
    packet[pos++] = (uint8_t)(ip & 0xFFU);

    /* Send unsolicited multicast response to 224.0.0.251:5353 */
    net_status_t nst = net_send_udp(MDNS_MULTICAST_IPV4, MDNS_PORT, MDNS_PORT, packet, (uint16_t)pos);
    if (nst == NET_OK)
    {
        s_mdns_telem.announcements_sent++;
        s_mdns_telem.responses_sent++;
        return MDNS_OK;
    }

    return MDNS_ERR_TX_FAIL;
}

mdns_status_t mdns_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len)
{
    (void)eth_frame;
    if (payload == NULL || len < sizeof(dns_hdr_t))
    {
        s_mdns_telem.invalid_packets++;
        return MDNS_ERR_CORRUPT_FRAME;
    }

    s_mdns_telem.queries_received++;
    const dns_hdr_t *hdr = (const dns_hdr_t *)payload;
    uint16_t flags = NET_NTOHS(hdr->flags);

    /* If QR bit (bit 15) is 1, this is a response, not a query */
    if ((flags & 0x8000U) != 0U)
    {
        return MDNS_ERR_NOT_QUERY;
    }

    uint16_t qdcount = NET_NTOHS(hdr->qdcount);
    if (qdcount == 0U)
    {
        return MDNS_ERR_NO_MATCH;
    }

    size_t offset = sizeof(dns_hdr_t);
    char query_name[64];
    bool matched = false;

    for (uint16_t q = 0; q < qdcount; q++)
    {
        if (!mdns_parse_query_name(payload, len, &offset, query_name, sizeof(query_name)))
        {
            s_mdns_telem.invalid_packets++;
            return MDNS_ERR_CORRUPT_FRAME;
        }

        if ((offset + 4U) > len)
        {
            return MDNS_ERR_CORRUPT_FRAME;
        }

        uint16_t qtype  = (uint16_t)(((uint16_t)payload[offset] << 8U) | (uint16_t)payload[offset + 1U]);
        offset += 4U; /* Skip QTYPE and QCLASS */

        /* Target match: "<hostname>.local" */
        char target_fqdn[48];
        size_t hlen = strlen(s_mdns_telem.hostname);
        memcpy(target_fqdn, s_mdns_telem.hostname, hlen);
        memcpy(target_fqdn + hlen, ".local", 7);

        if (mdns_strcasecmp(query_name, target_fqdn) &&
            (qtype == MDNS_TYPE_A || qtype == MDNS_TYPE_ANY))
        {
            matched = true;
            s_mdns_telem.host_queries_matched++;
            break;
        }
    }

    if (!matched)
    {
        return MDNS_ERR_NO_MATCH;
    }

    /* Synthesize Response Packet */
    uint8_t response[MDNS_MAX_PACKET_LEN];
    dns_hdr_t *resp_hdr = (dns_hdr_t *)response;
    resp_hdr->id = 0U; /* mDNS answers use ID 0 */
    resp_hdr->flags = NET_HTONS(MDNS_FLAGS_RESPONSE_AA);
    resp_hdr->qdcount = 0U;
    resp_hdr->ancount = NET_HTONS(1U);
    resp_hdr->nscount = 0U;
    resp_hdr->arcount = 0U;

    size_t pos = sizeof(dns_hdr_t);

    /* Answer Name */
    size_t name_len = mdns_encode_name(s_mdns_telem.hostname, &response[pos], sizeof(response) - pos);
    if (name_len == 0U) return MDNS_ERR_BUFFER_SMALL;
    pos += name_len;

    /* Type A (0x0001) */
    response[pos++] = 0x00U; response[pos++] = (uint8_t)MDNS_TYPE_A;
    /* Class IN + Cache-Flush (0x8001) */
    response[pos++] = 0x80U; response[pos++] = (uint8_t)MDNS_CLASS_IN;
    /* TTL: 120 seconds */
    response[pos++] = 0x00U; response[pos++] = 0x00U;
    response[pos++] = 0x00U; response[pos++] = (uint8_t)MDNS_DEFAULT_TTL_SEC;
    /* Data Length: 4 bytes */
    response[pos++] = 0x00U; response[pos++] = 0x04U;

    net_config_t ncfg;
    net_get_config(&ncfg);
    if (ncfg.ip == 0U)
    {
        return MDNS_OK;
    }
    uint32_t ip = ncfg.ip;
    s_mdns_telem.advertised_ip = ip;

    response[pos++] = (uint8_t)((ip >> 24U) & 0xFFU);
    response[pos++] = (uint8_t)((ip >> 16U) & 0xFFU);
    response[pos++] = (uint8_t)((ip >> 8U)  & 0xFFU);
    response[pos++] = (uint8_t)(ip & 0xFFU);

    net_send_udp(MDNS_MULTICAST_IPV4, MDNS_PORT, MDNS_PORT, response, (uint16_t)pos);
    s_mdns_telem.responses_sent++;

    return MDNS_OK;
}

void mdns_print_status(void)
{
#if defined(__riscv)
    console_puts("\r\n=======================================================\r\n");
    console_puts("        MULTICAST DNS (mDNS) RESPONDER TELEMETRY       \r\n");
    console_puts("=======================================================\r\n");
    console_puts("  Active:              ");
    console_puts(s_mdns_active ? "YES" : "NO");
    console_puts("\r\n  Hostname:            ");
    console_puts(s_mdns_telem.hostname);
    console_puts(".local\r\n  Advertised IP:       ");
    net_config_t ncfg;
    net_get_config(&ncfg);
    uint32_t ip = ncfg.ip;
    if (ip == 0U)
    {
        console_puts("0.0.0.0 (unassigned)");
    }
    else
    {
        put_dec((ip >> 24U) & 0xFFU); console_puts(".");
        put_dec((ip >> 16U) & 0xFFU); console_puts(".");
        put_dec((ip >> 8U) & 0xFFU);  console_puts(".");
        put_dec(ip & 0xFFU);
    }
    console_puts("\r\n  Queries Received:    ");
    put_dec(s_mdns_telem.queries_received);
    console_puts("\r\n  Queries Matched:     ");
    put_dec(s_mdns_telem.host_queries_matched);
    console_puts("\r\n  Responses Sent:      ");
    put_dec(s_mdns_telem.responses_sent);
    console_puts("\r\n  Announcements Sent:  ");
    put_dec(s_mdns_telem.announcements_sent);
    console_puts("\r\n=======================================================\r\n");
#endif
}

/* ========================================================================= */
/* Core Lifecycle APIs (.flash.text)                                         */
/* ========================================================================= */
mdns_status_t mdns_init(void)
{
    memset(&s_mdns_telem, 0, sizeof(s_mdns_telem));
    strncpy(s_mdns_telem.hostname, MDNS_DEFAULT_HOSTNAME, MDNS_MAX_HOSTNAME_LEN);
    s_mdns_active = true;
    s_mdns_telem.active = true;
    s_mdns_initialized = true;
    return MDNS_OK;
}

mdns_status_t mdns_start(const char *hostname)
{
    if (!s_mdns_initialized)
    {
        mdns_init();
    }
    if (hostname != NULL && strlen(hostname) > 0U && strlen(hostname) <= MDNS_MAX_HOSTNAME_LEN)
    {
        strncpy(s_mdns_telem.hostname, hostname, MDNS_MAX_HOSTNAME_LEN);
    }
    s_mdns_active = true;
    s_mdns_telem.active = true;
    return MDNS_OK;
}

mdns_status_t mdns_stop(void)
{
    s_mdns_active = false;
    s_mdns_telem.active = false;
    return MDNS_OK;
}

bool mdns_is_active(void)
{
    return s_mdns_active;
}

const char *mdns_get_hostname(void)
{
    return s_mdns_telem.hostname;
}

mdns_status_t mdns_set_hostname(const char *hostname)
{
    if (hostname == NULL || strlen(hostname) == 0U || strlen(hostname) > MDNS_MAX_HOSTNAME_LEN)
    {
        return MDNS_ERR_INVALID_ARG;
    }
    strncpy(s_mdns_telem.hostname, hostname, MDNS_MAX_HOSTNAME_LEN);
    return MDNS_OK;
}

mdns_status_t mdns_get_telemetry(mdns_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return MDNS_ERR_INVALID_ARG;
    }
    s_mdns_telem.active = s_mdns_active;
    memcpy(out_telem, &s_mdns_telem, sizeof(mdns_telemetry_t));
    return MDNS_OK;
}
