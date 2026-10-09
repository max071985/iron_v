/*
 * Iron V - IEEE 802.15.4 module (REV-33)
 *
 * The 802.15.4 transceiver driver is a facade from the legacy track (not reviewed in depth, REV-27):
 * no application uses it, so it is not in the production profiles and the boot no longer
 * initialises it (the driver initialises itself on first use). In the dev profile do-test and the
 * `15.4` command still exercise it.
 */
#include "ieee802154.h"
#include "module.h"
#include "shell.h"
#include "console.h"
#include "utils.h"
#include "string.h"

static void ieee802154_module_info(void)
{
    ieee802154_telemetry_t ztel;
    ieee802154_get_telemetry(&ztel);
    console_puts(" 15.4:    State: ");
    if (ztel.state == IEEE802154_STATE_DISABLE) console_puts("DISABLE");
    else if (ztel.state == IEEE802154_STATE_IDLE) console_puts("IDLE");
    else if (ztel.state == IEEE802154_STATE_TRX_OFF) console_puts("TRX_OFF");
    else if (ztel.state == IEEE802154_STATE_RX) console_puts("RX");
    else if (ztel.state == IEEE802154_STATE_TX) console_puts("TX");
    else console_puts("CCA");
    console_puts(", Channel: ");
    put_dec(ztel.channel);
    console_puts(" (");
    put_dec(ztel.freq_mhz);
    console_puts(" MHz), PAN: ");
    put_hex(ztel.pan_id);
    console_puts(", Short: ");
    put_hex(ztel.short_addr);
    console_puts("\r\n");
}

static void ieee802154_module_shell(char *args)
{
    char *subcmd = args;

    if (strncmp(subcmd, "chan", 4) == 0)
    {
        char *arg = subcmd + 4;
        while (*arg == ' ') arg++;
        uint32_t chan = 0;
        if (shell_parse_uint(&arg, &chan))
        {
            ieee802154_status_t cst = ieee802154_set_channel((uint8_t)chan);
            if (cst == IEEE802154_OK)
            {
                console_puts("IEEE 802.15.4 channel set to ");
                put_dec(chan);
                console_puts(" (");
                put_dec(ieee802154_get_freq_mhz((uint8_t)chan));
                console_puts(" MHz).\r\n");
            }
            else
            {
                console_puts("Error: Invalid channel (allowed 11-26).\r\n");
            }
        }
        else
        {
            console_puts("Usage: 15.4 chan <11-26>\r\n");
        }
    }
    else if (strncmp(subcmd, "pan", 3) == 0)
    {
        char *arg = subcmd + 3;
        while (*arg == ' ') arg++;
        uint32_t pan = 0;
        if (shell_parse_uint(&arg, &pan))
        {
            ieee802154_set_pan_id((uint16_t)pan);
            console_puts("IEEE 802.15.4 PAN ID set to ");
            put_hex(pan);
            console_puts(".\r\n");
        }
        else
        {
            console_puts("Usage: 15.4 pan <hex_pan_id>\r\n");
        }
    }
    else if (strncmp(subcmd, "short", 5) == 0)
    {
        char *arg = subcmd + 5;
        while (*arg == ' ') arg++;
        uint32_t saddr = 0;
        if (shell_parse_uint(&arg, &saddr))
        {
            ieee802154_set_short_address((uint16_t)saddr);
            console_puts("IEEE 802.15.4 Short Address set to ");
            put_hex(saddr);
            console_puts(".\r\n");
        }
        else
        {
            console_puts("Usage: 15.4 short <hex_short_addr>\r\n");
        }
    }
    else if (strncmp(subcmd, "rx", 2) == 0)
    {
        ieee802154_cmd(IEEE802154_CMD_RX_START);
        console_puts("IEEE 802.15.4 Transceiver entered RX state.\r\n");
    }
    else if (strncmp(subcmd, "tx", 2) == 0)
    {
        ieee802154_cmd(IEEE802154_CMD_TX_START);
        console_puts("IEEE 802.15.4 Transceiver entered TX state.\r\n");
    }
    else if (strncmp(subcmd, "stop", 4) == 0)
    {
        ieee802154_cmd(IEEE802154_CMD_FORCE_TRX_OFF);
        console_puts("IEEE 802.15.4 Transceiver entered TRX_OFF standby state.\r\n");
    }
    else
    {
        ieee802154_telemetry_t zt;
        ieee802154_get_telemetry(&zt);
        console_puts("IEEE 802.15.4 (Zigbee / Thread) Radio Transceiver Status:\r\n");
        console_puts("  State:             ");
        if (zt.state == IEEE802154_STATE_DISABLE) console_puts("DISABLE");
        else if (zt.state == IEEE802154_STATE_IDLE) console_puts("IDLE");
        else if (zt.state == IEEE802154_STATE_TRX_OFF) console_puts("TRX_OFF");
        else if (zt.state == IEEE802154_STATE_RX) console_puts("RX");
        else if (zt.state == IEEE802154_STATE_TX) console_puts("TX");
        else console_puts("CCA");
        console_puts("\r\n");
        console_puts("  Channel:           ");
        put_dec(zt.channel);
        console_puts(" (");
        put_dec(zt.freq_mhz);
        console_puts(" MHz, 2.4 GHz ISM)\r\n");
        console_puts("  PAN ID:            ");
        put_hex(zt.pan_id);
        console_puts("\r\n");
        console_puts("  Short Address:     ");
        put_hex(zt.short_addr);
        console_puts("\r\n");
        console_puts("  Extended EUI-64:   ");
        for (int i = 0; i < 8; i++)
        {
            uint8_t byte = zt.ext_addr[i];
            const char hex_chars[] = "0123456789abcdef";
            console_putc(hex_chars[(byte >> 4) & 0x0F]);
            console_putc(hex_chars[byte & 0x0F]);
            if (i < 7) console_putc(':');
        }
        console_puts("\r\n");
        console_puts("  TX Power Level:    ");
        put_dec(zt.tx_power);
        console_puts(" / 31\r\n");
        console_puts("  Auto-ACK TX / RX:  ");
        console_puts(zt.auto_ack_tx ? "ENABLED" : "DISABLED");
        console_puts(" / ");
        console_puts(zt.auto_ack_rx ? "ENABLED" : "DISABLED");
        console_puts("\r\n");
        console_puts("  Promiscuous Mode:  ");
        console_puts(zt.promiscuous ? "ENABLED" : "DISABLED");
        console_puts("\r\n");
        console_puts("  Hardware Version:  ");
        put_hex(zt.date_version);
        console_puts("\r\n");
        console_puts("  Transceiver Cmds:  TX=");
        put_dec(zt.tx_count);
        console_puts(", RX=");
        put_dec(zt.rx_count);
        console_puts("\r\n");
    }
}

MODULE_DEFINE(s_ieee802154_module, {
    .name       = "ieee802154",
    .order      = MODULE_ORDER_IEEE802154,
    .print_info = ieee802154_module_info,
});

SHELL_COMMAND_DEFINE(s_ieee802154_cmd, {
    .name = "15.4",
    .help = "15.4 [status|chan|pan|short|rx|tx|stop] - Show or control IEEE 802.15.4 radio transceiver",
    .run  = ieee802154_module_shell,
});
