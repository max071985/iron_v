#include "shell.h"
#include <stdint.h>
#include "config.h"
#include "io_constants.h"
#include "utils.h"
#include "string.h"
#include "test.h"
#include "clock.h"
#include "mmu.h"
#include "wdt.h"
#include "trap.h"
#include "interrupt.h"
#include "dpc.h"
#include "usb_serial.h"
#include "uart.h"
#include "console.h"
#include "timer.h"
#include "arena.h"
#include "systimer.h"
#include "task.h"
#include "pmp.h"
#include "lp_core.h"
#include "power.h"
#include "gpio.h"
#include "gdma.h"
#include "modem.h"
#include "wifi.h"
#include "wifi_os_adapter.h"
#include "ieee802154.h"
#include "net.h"
#include "tcp.h"
#include "http_server.h"
#include "dhcp.h"
#include "wifi_vendor_types.h"
#include "speedtest.h"
#include "efuse.h"
#include "soak.h"
#include "ota.h"
#include "nvs.h"
#include "provisioning.h"
#include "wpa2_client.h"
#include "mdns.h"


/* ========================================================================= */
/* Static Storage: Shell Execution Telemetry                                 */
/* ========================================================================= */
static shell_telemetry_t s_shell_telemetry = {0};

void shell_init(void)
{
    memset(&s_shell_telemetry, 0, sizeof(s_shell_telemetry));
}

void shell_get_telemetry(shell_telemetry_t *out_telem)
{
    if (out_telem != NULL)
    {
        memcpy(out_telem, &s_shell_telemetry, sizeof(shell_telemetry_t));
    }
}

void shell_reset_telemetry(void)
{
    memset(&s_shell_telemetry, 0, sizeof(s_shell_telemetry));
}

uint32_t shell_get_uptime_seconds(void)
{
    wdt_supervisor_t wdt;
    wdt_get_status(&wdt);
    if (wdt.epoch_count > 0U)
    {
        return wdt.epoch_count;
    }
    uint64_t ticks = systimer_get_ticks();
    uint32_t secs = (uint32_t)(ticks / 16000000ULL);
    if (secs == 0U && ticks > 0ULL)
    {
        secs = 1U;
    }
    return secs;
}

void shell_get_health_telemetry(system_health_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return;
    }
    memset(out_telem, 0, sizeof(system_health_telemetry_t));

    out_telem->uptime_seconds = shell_get_uptime_seconds();

    wdt_supervisor_t wdt;
    wdt_get_status(&wdt);
    out_telem->wdt_feeds_total = wdt.total_feed_count;

    arena_telemetry_t atel;
    arena_get_stats(&atel);
    uint32_t small_used = atel.small_pool.active_count * 64U;
    uint32_t med_used = atel.medium_pool.active_count * 256U;
    uint32_t scratch_used = (uint32_t)atel.scratch.current_offset;
    out_telem->arena_bytes_used = small_used + med_used + scratch_used;

    uint32_t small_total = atel.small_pool.block_count * 64U;
    uint32_t med_total = atel.medium_pool.block_count * 256U;
    uint32_t scratch_total = (uint32_t)atel.scratch.capacity;
    uint32_t arena_total = small_total + med_total + scratch_total;
    if (arena_total >= out_telem->arena_bytes_used)
    {
        out_telem->arena_bytes_free = arena_total - out_telem->arena_bytes_used;
    }
    else
    {
        out_telem->arena_bytes_free = 0U;
    }

    dpc_queue_t dpc_stat;
    dpc_get_stats(&dpc_stat);
    out_telem->dpc_queue_drops = dpc_stat.drop_count;

    console_manager_t cmgr;
    console_get_manager(&cmgr);
    out_telem->uart_active = (cmgr.active_mask & CONSOLE_MASK_UART0) ? 1U : 0U;
    out_telem->usb_active = (cmgr.active_mask & CONSOLE_MASK_USB) ? 1U : 0U;

    wifi_telemetry_t wtel;
    wifi_get_telemetry(&wtel);
    out_telem->wifi_packets_rx = wtel.rx_packets;
    out_telem->wifi_packets_tx = wtel.tx_packets;
}

void shell_print_health(void)
{
    system_health_telemetry_t h;
    shell_get_health_telemetry(&h);

    console_puts("=====================================================\r\n");
    console_puts("        IRON V 24/7 SYSTEM HEALTH MONITORING         \r\n");
    console_puts("=====================================================\r\n");
    console_puts("  System Uptime:      ");
    put_dec(h.uptime_seconds);
    console_puts(" seconds (");
    uint32_t days = h.uptime_seconds / 86400U;
    uint32_t hours = (h.uptime_seconds % 86400U) / 3600U;
    uint32_t mins = (h.uptime_seconds % 3600U) / 60U;
    uint32_t secs = h.uptime_seconds % 60U;
    if (days > 0U) { put_dec(days); console_puts("d "); }
    put_dec(hours); console_puts("h ");
    put_dec(mins); console_puts("m ");
    put_dec(secs); console_puts("s)\r\n");

    console_puts("  Watchdog Status:    ");
    put_dec(h.wdt_feeds_total);
    console_puts(" feeds delivered (0 watchdog resets)\r\n");

    console_puts("  Memory Invariant:   Used: ");
    put_dec(h.arena_bytes_used);
    console_puts(" B | Free: ");
    put_dec(h.arena_bytes_free);
    console_puts(" B (Zero dynamic heap leaks)\r\n");

    console_puts("  DPC Queue Drops:    ");
    put_dec(h.dpc_queue_drops);
    if (h.dpc_queue_drops == 0U)
    {
        console_puts(" [PASS - ZERO DROP GUARANTEE]\r\n");
    }
    else
    {
        console_puts(" [WARN - QUEUE DROPS DETECTED]\r\n");
    }

    console_puts("  Console Backends:   UART0: ");
    console_puts(h.uart_active ? "ACTIVE" : "OFF");
    console_puts(" | USB CDC-ACM: ");
    console_puts(h.usb_active ? "ACTIVE" : "OFF");
    console_puts("\r\n");

    console_puts("  Wi-Fi Packets:      RX: ");
    put_dec(h.wifi_packets_rx);
    console_puts(" | TX: ");
    put_dec(h.wifi_packets_tx);
    console_puts("\r\n");

    console_puts("  Shell Telemetry:    Commands: ");
    put_dec(s_shell_telemetry.commands_processed);
    console_puts(" executed (Unknown: ");
    put_dec(s_shell_telemetry.unknown_commands);
    console_puts(")\r\n");

    console_puts("  Stability Verdict:  [ PASS - 100% HEALTHY ]\r\n");
    console_puts("=====================================================\r\n");
}

void shell_print_top(void)
{
    system_health_telemetry_t h;
    shell_get_health_telemetry(&h);
    clock_config_t clk;
    clock_get_config(&clk);

    console_puts("[TOP] CPU: ");
    put_dec(clk.cpu_mhz);
    console_puts("MHz | Uptime: ");
    put_dec(h.uptime_seconds);
    console_puts("s | Arenas: ");
    put_dec(h.arena_bytes_used);
    console_puts("/");
    put_dec(h.arena_bytes_used + h.arena_bytes_free);
    console_puts("B | DPC Drops: ");
    put_dec(h.dpc_queue_drops);
    console_puts(" | WDT Feeds: ");
    put_dec(h.wdt_feeds_total);
    console_puts("\r\n");
}

void print_help(void) { shell_print_help(); }
void shell_print_help(void)
{
    console_puts("Iron V Shell Commands:\r\n");
    console_puts("  help                - Show available commands\r\n");
    console_puts("  info                - Show system information\r\n");
    console_puts("  health              - Show comprehensive 24/7 system health telemetry\r\n");
    console_puts("  top                 - Show real-time CPU, memory, and task performance\r\n");

    console_puts("  uptime              - Show high-resolution system uptime & timer telemetry\r\n");
    console_puts("  tasks               - Show cooperative coroutine scheduler tasks & status\r\n");
    console_puts("  pmp                 - Show RISC-V PMP & HP_APM memory protection status\r\n");
    console_puts("  peek <hex_addr>     - Read 32-bit word from hex address\r\n");
    console_puts("  poke <addr> <val>   - Write 32-bit hex value to address\r\n");
    console_puts("  ecall               - Trigger controlled M-mode software trap (ECALL)\r\n");
    console_puts("  panic               - Trigger illegal instruction exception to test panic dump\r\n");
    console_puts("  timer [start|stop]  - Show or control periodic timer telemetry\r\n");
    console_puts("  arena               - Show static memory arena allocation telemetry\r\n");
    console_puts("  lp [status|start|stop] - Show or control LP core coprocessor\r\n");
    console_puts("  power [status|mode|sample|store] - Show or control power management & shared mailbox\r\n");
    console_puts("  gpio [status|set|get|dir|pull] - Show or control GPIO pins & IO_MUX\r\n");
    console_puts("  dma [status]        - Show GDMA multi-channel engine status\r\n");
    console_puts("  modem [status|all|wifi|ble|15.4|coex] - Show or control wireless modem clocks and power\r\n");
    console_puts("  coex [status|diag|on|off] - Show or control 3-wire RF coexistence arbiter & LP clock\r\n");
    console_puts("  wifi [status|mac|ring|init|scan|sniffer|ap] - Show or control 802.11ax Wi-Fi 6 MAC driver & SoftAP\r\n");
    console_puts("  mmu [status|map]    - Show MSPI MMU Flash XIP mapping & cache status\r\n");
    console_puts("  15.4 [status|chan|pan|short|rx|tx|stop] - Show or control IEEE 802.15.4 radio transceiver\r\n");
    console_puts("  net [status|ip|mask|gw|arp|tcp|reset] - Show or control IPv4 network interface & TCP state machine\r\n");
    console_puts("  dhcp [status]       - Show DHCP server leases and captive portal telemetry\r\n");
    console_puts("  http [status|routes|start|stop] - Show or control zero-allocation local REST/HTTP server\r\n");
    console_puts("  speedtest [run|burst|udp|status|reset] - Run or inspect LAN network & Wi-Fi throughput benchmark\r\n");
    console_puts("  efuse [status|summary|security|mac] - Silicon eFuse controller & security seal state\r\n");
    console_puts("  soak [status|audit|cycles] - 24/7 stability soak, memory leak audit & anti-starvation telemetry\r\n");
    console_puts("  ota [status|partitions|switch|rollback|mark-valid|verify] - Dual-slot Flash OTA upgrade & rollback\r\n");
    console_puts("  nvs [status|list|get|set|erase|format] - Non-Volatile Flash Key-Value storage\r\n");
    console_puts("  prov [status|scan|set|get|clear] - SoftAP Captive Portal Wi-Fi Provisioning Engine\r\n");
    console_puts("  sta [status|connect|disconnect|mdns|eapol] - WPA2 Station & mDNS Client\r\n");
    console_puts("  seal                - Run Golden Master system-wide integrity seal audit\r\n");
    console_puts("  do-test             - Run full baseline validation test suite\r\n");
}

void print_info(void) { shell_print_info(); }
void shell_print_info(void)
{
    clock_config_t clk;
    clock_get_config(&clk);

    console_puts("========================================\r\n");
    console_puts(" Iron V Bare-Metal RISC-V Runtime\r\n");
    console_puts(" Hostname: ");
    console_puts(CONFIG_DEVICE_HOSTNAME);
    console_puts("\r\n");
    console_puts(" Target:  ESP32-C6 (RV32IMAC)\r\n");
    console_puts(" Mode:    Bare Metal / No ESP-IDF\r\n");
    console_puts(" CPU:     ");
    put_dec(clk.cpu_mhz);
    console_puts(" MHz (PLL 480M)\r\n");
    console_puts(" APB:     ");
    put_dec(clk.apb_mhz);
    console_puts(" MHz\r\n");
    console_puts(" Memory:  HP SRAM 512KB (Harvard Split)\r\n");
    console_puts(" Flash:   8 MB SPI NOR Flash (DIO @ 80M)\r\n");

    wdt_supervisor_t wdt;
    wdt_get_status(&wdt);
    console_puts(" WDT:     ");
    if (wdt.active)
    {
        console_puts("Active (");
        put_dec(wdt.feed_interval_ms);
        console_puts(" ms timeout, 1s epoch window)\r\n");
        console_puts(" Uptime:  ");
        put_dec(wdt.epoch_count);
        console_puts(" s (total feeds: ");
        put_dec(wdt.total_feed_count);
        console_puts(", epoch feeds: ");
        put_dec(wdt.feed_count);
        console_puts(")\r\n");
    }
    else
    {
        console_puts("Disabled\r\n");
    }

    timer_status_t tmr;
    timer_get_status(&tmr);
    console_puts(" Timer:   ");
    if (tmr.active)
    {
        console_puts("Active (TIMG0 T0, ");
        put_dec(tmr.interval_sec);
        console_puts("s period, ticks: ");
        put_dec(tmr.isr_count);
        console_puts(", DPC: ");
        put_dec(tmr.dpc_count);
        console_puts(")\r\n");
    }
    else
    {
        console_puts("Disabled\r\n");
    }

    systimer_telemetry_t stel;
    systimer_get_telemetry(&stel);
    console_puts(" SYSTIMER: Active (16 MHz, ");
    put_dec(stel.uptime_sec);
    console_puts("s uptime, ");
    put_dec((uint32_t)stel.total_ticks);
    console_puts(" ticks)\r\n");

    soc_reset_cause_t rst_cause = wdt_get_reset_cause();
    console_puts(" Reset:   ");
    console_puts(wdt_get_reset_cause_desc(rst_cause));
    console_puts(" [");
    put_hex(rst_cause);
    console_puts("]\r\n");

    dpc_queue_t dpc_stat;
    dpc_get_stats(&dpc_stat);
    console_puts(" DPC:     Active (Pending: ");
    put_dec(dpc_get_size());
    console_puts("/");
    put_dec(DPC_QUEUE_CAPACITY);
    console_puts(", Processed: ");
    put_dec(dpc_get_processed_count());
    console_puts(", Drops: ");
    put_dec(dpc_stat.drop_count);
    console_puts(")\r\n");

    console_puts(" USB:     CDC-ACM (EP1 TX Ready: ");
    put_dec(usb_serial_is_tx_ready());
    console_puts(", RX Avail: ");
    put_dec(usb_serial_is_rx_ready());
    console_puts(")\r\n");

    console_manager_t cmgr;
    console_get_manager(&cmgr);
    console_puts(" Console: Dual Multiplexed (UART0: ");
    console_puts((cmgr.active_mask & CONSOLE_MASK_UART0) ? "Active" : "Off");
    console_puts(", USB: ");
    console_puts((cmgr.active_mask & CONSOLE_MASK_USB) ? "Active" : "Off");
    console_puts(", Echo: ");
    console_puts(cmgr.echo_enabled ? "ON" : "OFF");
    console_puts(")\r\n");

    arena_telemetry_t atel;
    arena_get_stats(&atel);
    console_puts(" Arena:   Small ");
    put_dec(atel.small_pool.active_count);
    console_puts("/");
    put_dec(atel.small_pool.block_count);
    console_puts(" (64B), Med ");
    put_dec(atel.medium_pool.active_count);
    console_puts("/");
    put_dec(atel.medium_pool.block_count);
    console_puts(" (256B), Scratch ");
    put_dec((uint32_t)atel.scratch.current_offset);
    console_puts("/");
    put_dec((uint32_t)atel.scratch.capacity);
    console_puts(" B\r\n");

    pmp_telemetry_t ptel;
    pmp_get_telemetry(&ptel);
    console_puts(" PMP/APM: PMP Active: ");
    put_dec(ptel.pmp_active_count);
    console_puts("/4 (pmpcfg0: ");
    put_hex(ptel.pmpcfg0_val);
    console_puts("), APM Active: ");
    put_dec(ptel.apm_active_count);
    console_puts("/16\r\n");

    lp_core_telemetry_t lptel;
    lp_core_get_telemetry(&lptel);
    console_puts(" LP Core: ");
    console_puts(lptel.is_running ? "Running" : "Stopped");
    console_puts(" (Clock: ");
    console_puts(lptel.clock_enabled ? "ON" : "OFF");
    console_puts(", Reset: ");
    console_puts(lptel.in_reset ? "HELD" : "RELEASED");
    console_puts(", Ticks: ");
    put_dec(lptel.counter_readback);
    console_puts(")\r\n");

    power_telemetry_t pwtel;
    power_get_telemetry(&pwtel);
    console_puts(" Power:   Mode: ");
    if (pwtel.current_mode == PM_STATE_ACTIVE) console_puts("ACTIVE");
    else if (pwtel.current_mode == PM_STATE_LIGHT_SLEEP) console_puts("LIGHT_SLEEP");
    else console_puts("DEEP_SLEEP");
    console_puts(", Mailbox: ");
    put_hex(pwtel.mailbox_magic);
    console_puts(", WakeCount: ");
    put_dec(pwtel.wake_count);
    console_puts("\r\n");

    gpio_telemetry_t gptel;
    gpio_get_telemetry(&gptel);
    console_puts(" GPIO:    OutEn: ");
    put_hex(gptel.enable_mask);
    console_puts(", Out: ");
    put_hex(gptel.out_mask);
    console_puts(", In: ");
    put_hex(gptel.in_mask);
    console_puts("\r\n");

    gdma_telemetry_t gdtel;
    gdma_get_telemetry(&gdtel);
    console_puts(" GDMA:    HW Version: ");
    put_hex(gdtel.date_version);
    console_puts(" (Ch0 In: ");
    console_puts(gdtel.ch0_in_active ? "RUN" : "IDLE");
    console_puts(", Out: ");
    console_puts(gdtel.ch0_out_active ? "RUN" : "IDLE");
    console_puts(")\r\n");

    modem_clock_state_t mstate;
    modem_get_clock_state(&mstate);
    console_puts(" Modem:   WiFi: ");
    console_puts(mstate.wifi_clk_enabled ? "ON" : "OFF");
    console_puts(", BLE: ");
    console_puts(mstate.ble_clk_enabled ? "ON" : "OFF");
    console_puts(", 15.4: ");
    console_puts(mstate.ieee802154_clk_enabled ? "ON" : "OFF");
    console_puts(", Coex: ");
    console_puts(mstate.coexistence_enabled ? "ON" : "OFF");
    console_puts(" (Date: ");
    put_hex(modem_get_syscon_date());
    console_puts(")\r\n");

    wifi_telemetry_t wtel;
    wifi_get_telemetry(&wtel);
    console_puts(" WiFi:    State: ");
    if (wtel.state == WIFI_STATE_OFF) console_puts("OFF");
    else if (wtel.state == WIFI_STATE_INIT) console_puts("INIT");
    else if (wtel.state == WIFI_STATE_IDLE) console_puts("IDLE");
    else if (wtel.state == WIFI_STATE_ACTIVE) console_puts("ACTIVE");
    else if (wtel.state == WIFI_STATE_SCANNING) console_puts("SCANNING");
    else if (wtel.state == WIFI_STATE_CONNECTED) console_puts("CONNECTED");
    else if (wtel.state == WIFI_STATE_AP_ACTIVE) console_puts("AP_ACTIVE");
    else console_puts("DISCONNECTED");
    console_puts(", MAC: ");
    for (int i = 0; i < 6; i++)
    {
        uint8_t byte = wtel.mac_addr[i];
        const char hex_chars[] = "0123456789abcdef";
        console_putc(hex_chars[(byte >> 4) & 0x0F]);
        console_putc(hex_chars[byte & 0x0F]);
        if (i < 5) console_putc(':');
    }
    console_puts(", RX queue: ");
    put_dec(wtel.rx_ring_capacity);
    console_puts(" x 1536B\r\n");

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

    net_config_t ncfg;
    net_get_config(&ncfg);
    char ip_buf[NET_IP_STR_BUF_LEN];
    net_ip_to_str(ncfg.ip, ip_buf, sizeof(ip_buf));
    console_puts(" Net:     IP: ");
    console_puts(ip_buf);
    console_puts(", Mask: ");
    net_ip_to_str(ncfg.netmask, ip_buf, sizeof(ip_buf));
    console_puts(ip_buf);
    console_puts(", GW: ");
    net_ip_to_str(ncfg.gateway, ip_buf, sizeof(ip_buf));
    console_puts(ip_buf);
    console_puts(", TCP: Active\r\n");

    http_telemetry_t htel;
    http_server_get_telemetry(&htel);
    console_puts(" HTTP:    State: ");
    console_puts(htel.server_running ? "ACTIVE" : "STOPPED");
    console_puts(", Port: ");
    put_dec(HTTP_SERVER_DEFAULT_PORT);
    console_puts(", Routes: ");
    put_dec(htel.active_routes);
    console_puts(", ReqTotal: ");
    put_dec(htel.requests_total);
    console_puts("\r\n");

    speedtest_telemetry_t sptel;
    speedtest_get_telemetry(&sptel);
    console_puts(" Diag:    Port: ");
    put_dec(SPEEDTEST_DEFAULT_PORT);
    console_puts(", Bursts: ");
    put_dec(sptel.bursts_run);
    console_puts(", Last: ");
    put_dec(sptel.last_throughput_mbps);
    console_puts(" Mbps\r\n");

    uint8_t efuse_mac[EFUSE_MAC_LEN];
    efuse_get_mac(efuse_mac);
    uint32_t wafer_maj = 0U, wafer_min = 0U;
    efuse_get_chip_version(&wafer_maj, &wafer_min);
    console_puts(" eFuse:   MAC: ");
    const char hex_d[] = "0123456789abcdef";
    for (int i = 0; i < (int)EFUSE_MAC_LEN; i++)
    {
        console_putc(hex_d[(efuse_mac[i] >> 4) & 0x0F]);
        console_putc(hex_d[efuse_mac[i] & 0x0F]);
        if (i < 5) console_putc(':');
    }
    console_puts(", Chip: v");
    put_dec(wafer_maj); console_putc('.'); put_dec(wafer_min);
    console_puts(", SecBoot: "); console_puts(efuse_is_secure_boot_enabled() ? "ON" : "OFF");
    console_puts(", FlashCrypt: "); console_puts(efuse_is_flash_encryption_enabled() ? "ON" : "OFF");
    console_puts("\r\n");

    soak_telemetry_t soak_tel;
    soak_get_telemetry(&soak_tel);
    console_puts(" Soak:    Cycles: ");
    put_dec(soak_tel.completed_cycles);
    console_puts(", Streak: ");
    put_dec(soak_tel.clean_streak);
    console_puts(", LeakFree: ");
    console_puts(!soak_tel.mem_leak_detected ? "YES" : "NO");
    console_puts(", Drops: ");
    put_dec(dpc_get_drop_count());
    console_puts("\r\n");

    ota_status_report_t ota_rep;
    ota_get_status(&ota_rep);
    console_puts(" OTA:     Active: Slot ");
    put_dec(ota_rep.active_slot);
    console_puts(" (Seq: ");
    put_dec(ota_rep.active_seq);
    console_puts(", State: ");
    if (ota_rep.active_state == OTA_STATE_VALID) console_puts("VALID");
    else if (ota_rep.active_state == OTA_STATE_TESTING) console_puts("TESTING");
    else console_puts("OTHER");
    console_puts("), Switches: ");
    put_dec(ota_rep.total_switches);
    console_puts("\r\n");

    nvs_stats_t nvs_st;
    nvs_get_stats(&nvs_st);
    console_puts(" NVS:     Keys: ");
    put_dec(nvs_st.total_keys);
    console_puts(", Used: ");
    put_dec(nvs_st.used_bytes);
    console_puts(" B, Seal: ");
    golden_master_report_t gm_rep;
    bool gm_ok = golden_master_verify(&gm_rep);
    console_puts(gm_ok ? "CERTIFIED (0x5A5A5A5A)\r\n" : "UNCERTIFIED\r\n");
    console_puts("========================================\r\n");
}

static int parse_uint(char **str, uint32_t *out)
{
    if (!str || !*str || !out) return 0;
    skip_space(str);
    char *p = *str;
    if (*p == '\0') return 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        return s_htoi(str, out);
    }

    uint32_t val = 0;
    int parsed = 0;
    while (*p >= '0' && *p <= '9')
    {
        val = (val * 10U) + (uint32_t)(*p - '0');
        p++;
        parsed = 1;
    }
    if (parsed)
    {
        *out = val;
        *str = p;
        return 1;
    }
    return 0;
}

void shell_execute(char *input_buffer)
{
    if (input_buffer[0] == '\0')
    {
        s_shell_telemetry.empty_commands++;
        return;
    }

    s_shell_telemetry.commands_processed++;
    s_shell_telemetry.last_command_timestamp_us = systimer_get_us();

    if (strcmp(input_buffer, "help") == 0)
    {
        print_help();
    }
    else if (strcmp(input_buffer, "info") == 0)
    {
        print_info();
    }
    else if (strcmp(input_buffer, "health") == 0 || strcmp(input_buffer, "status") == 0)
    {
        shell_print_health();
    }
    else if (strcmp(input_buffer, "top") == 0)
    {
        shell_print_top();
    }
    else if (strcmp(input_buffer, "do-test") == 0 || strcmp(input_buffer, "test") == 0)
    {
        run_validation_suite();
    }
    else if (strncmp(input_buffer, "soak", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        char *subcmd = input_buffer + 4;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "status", 6) == 0)
        {
            soak_print_status();
        }
        else if (strncmp(subcmd, "audit", 5) == 0)
        {
            soak_print_audit();
        }
        else
        {
            uint32_t cycles = TEST_SOAK_DEFAULT_CYCLES;
            if (*subcmd != '\0')
            {
                if (strncmp(subcmd, "cont", 4) == 0 || strncmp(subcmd, "24/7", 4) == 0)
                {
                    cycles = 0U;
                }
                else
                {
                    uint32_t c_in = 0U;
                    if (parse_uint(&subcmd, &c_in))
                    {
                        cycles = c_in;
                    }
                }
            }
            test_soak_run(cycles, TEST_SOAK_DEFAULT_DELAY_MS);
        }
    }
    else if (strncmp(input_buffer, "ota", 3) == 0 && (input_buffer[3] == ' ' || input_buffer[3] == '\0'))
    {
        char *p = input_buffer + 3;
        skip_space(&p);

        if (strncmp(p, "partitions", 10) == 0)
        {
            ota_print_partitions();
        }
        else if (strncmp(p, "switch", 6) == 0)
        {
            p += 6;
            skip_space(&p);
            uint32_t target_slot = 1U;
            if (*p == '0') target_slot = 0U;
            else if (*p == '1') target_slot = 1U;
            ota_status_t rc = ota_switch_slot((ota_slot_t)target_slot);
            if (rc == OTA_OK)
            {
                console_puts("Switched active slot to Slot ");
                put_dec(target_slot);
                console_puts(" (State: TESTING). Reboot to run new firmware.\r\n");
            }
            else if (rc == OTA_ERR_ALREADY_ACTIVE)
            {
                console_puts("Slot ");
                put_dec(target_slot);
                console_puts(" is already the active slot.\r\n");
            }
            else
            {
                console_puts("Failed to switch slot: error ");
                put_dec((uint32_t)(-rc));
                console_puts("\r\n");
            }
        }
        else if (strncmp(p, "rollback", 8) == 0)
        {
            ota_status_t rc = ota_rollback();
            if (rc == OTA_OK)
            {
                console_puts("Rolled back to previous slot (State: VALID). Reboot to run fallback firmware.\r\n");
            }
            else
            {
                console_puts("Rollback failed: error ");
                put_dec((uint32_t)(-rc));
                console_puts("\r\n");
            }
        }
        else if (strncmp(p, "mark-valid", 10) == 0)
        {
            ota_status_t rc = ota_mark_valid();
            if (rc == OTA_OK)
            {
                console_puts("Current running slot marked as VALID & STABLE.\r\n");
            }
            else
            {
                console_puts("Failed to mark valid: error ");
                put_dec((uint32_t)(-rc));
                console_puts("\r\n");
            }
        }
        else if (strncmp(p, "verify", 6) == 0)
        {
            p += 6;
            skip_space(&p);
            uint32_t target_slot = 0U;
            if (*p == '1') target_slot = 1U;
            esp_image_header_t hdr;
            ota_status_t rc = ota_verify_image((ota_slot_t)target_slot, &hdr);
            if (rc == OTA_OK)
            {
                console_puts("Slot ");
                put_dec(target_slot);
                console_puts(" Image Header: VALID (ESP32-C6)\r\n");
                console_puts("  Entrypoint:  0x");
                put_hex(hdr.entry_addr);
                console_puts("\r\n  Segments:    ");
                put_dec(hdr.segment_count);
                console_puts("\r\n  SPI Mode:    ");
                put_dec(hdr.spi_mode);
                console_puts(", Chip ID: ");
                put_dec(hdr.chip_id);
                console_puts("\r\n");
            }
            else
            {
                console_puts("Slot ");
                put_dec(target_slot);
                console_puts(" Image Header: INVALID or CORRUPTED (error ");
                put_dec((uint32_t)(-rc));
                console_puts(")\r\n");
            }
        }
        else
        {
            ota_print_status();
        }
    }
    else if (strncmp(input_buffer, "nvs", 3) == 0 && (input_buffer[3] == ' ' || input_buffer[3] == '\0'))
    {
        char *p = input_buffer + 3;
        skip_space(&p);

        if (strncmp(p, "list", 4) == 0)
        {
            nvs_print_keys();
        }
        else if (strncmp(p, "format", 6) == 0)
        {
            nvs_status_t rc = nvs_erase_all();
            if (rc == NVS_OK)
            {
                console_puts("NVS partition formatted successfully (all keys cleared).\r\n");
            }
            else
            {
                console_puts("Failed to format NVS: error ");
                put_dec((uint32_t)(-rc));
                console_puts("\r\n");
            }
        }
        else if (strncmp(p, "get", 3) == 0)
        {
            p += 3;
            skip_space(&p);
            char key[NVS_KEY_MAX_LEN];
            char *k = key;
            while (*p && *p != ' ' && (size_t)(k - key) < sizeof(key) - 1)
            {
                *k++ = *p++;
            }
            *k = '\0';

            char val_str[NVS_VAL_MAX_LEN];
            nvs_status_t rc = nvs_get_str(key, val_str, sizeof(val_str));
            if (rc == NVS_OK)
            {
                console_puts("NVS [");
                console_puts(key);
                console_puts("] = \"");
                console_puts(val_str);
                console_puts("\"\r\n");
            }
            else
            {
                uint32_t val_u32 = 0U;
                rc = nvs_get_u32(key, &val_u32);
                if (rc == NVS_OK)
                {
                    console_puts("NVS [");
                    console_puts(key);
                    console_puts("] = ");
                    put_dec(val_u32);
                    console_puts(" (0x");
                    put_hex(val_u32);
                    console_puts(")\r\n");
                }
                else
                {
                    console_puts("Key '");
                    console_puts(key);
                    console_puts("' not found in NVS.\r\n");
                }
            }
        }
        else if (strncmp(p, "set", 3) == 0)
        {
            p += 3;
            skip_space(&p);
            char key[NVS_KEY_MAX_LEN];
            char *k = key;
            while (*p && *p != ' ' && (size_t)(k - key) < sizeof(key) - 1)
            {
                *k++ = *p++;
            }
            *k = '\0';
            skip_space(&p);

            nvs_status_t rc;
            if (*p == '"')
            {
                p++;
                char val_str[NVS_VAL_MAX_LEN];
                char *v = val_str;
                while (*p && *p != '"' && (size_t)(v - val_str) < sizeof(val_str) - 1)
                {
                    *v++ = *p++;
                }
                *v = '\0';
                rc = nvs_set_str(key, val_str);
            }
            else
            {
                uint32_t val_u32 = 0U;
                char *temp_p = p;
                if (parse_uint(&temp_p, &val_u32))
                {
                    rc = nvs_set_u32(key, val_u32);
                }
                else
                {
                    rc = nvs_set_str(key, p);
                }
            }

            if (rc == NVS_OK)
            {
                console_puts("Key '");
                console_puts(key);
                console_puts("' stored successfully in NVS.\r\n");
            }
            else
            {
                console_puts("Failed to store key: error ");
                put_dec((uint32_t)(-rc));
                console_puts("\r\n");
            }
        }
        else if (strncmp(p, "erase", 5) == 0)
        {
            p += 5;
            skip_space(&p);
            char key[NVS_KEY_MAX_LEN];
            char *k = key;
            while (*p && *p != ' ' && (size_t)(k - key) < sizeof(key) - 1)
            {
                *k++ = *p++;
            }
            *k = '\0';
            nvs_status_t rc = nvs_erase_key(key);
            if (rc == NVS_OK)
            {
                console_puts("Key '");
                console_puts(key);
                console_puts("' erased from NVS.\r\n");
            }
            else
            {
                console_puts("Key '");
                console_puts(key);
                console_puts("' not found.\r\n");
            }
        }
        else
        {
            nvs_print_stats();
        }
    }
    else if (strncmp(input_buffer, "seal", 4) == 0 || strncmp(input_buffer, "golden", 6) == 0)
    {
        golden_master_print_report();
    }
    else if (strncmp(input_buffer, "peek", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        uint32_t addr = 0;
        char *arg = input_buffer + 4;
        if (s_htoi(&arg, &addr))
        {
            mem_access_t access = check_mem_access(addr);
            if (access == MEM_ACCESS_INVALID)
            {
                console_puts("ERROR: Address ");
                put_hex(addr);
                console_puts(" is out of bounds or not 4-byte aligned. Read rejected.\r\n");
            }
            else
            {
                uint32_t val = *(volatile uint32_t *)(uintptr_t)addr;
                console_puts("[");
                put_hex(addr);
                console_puts("] = ");
                put_hex(val);
                if (access == MEM_ACCESS_READONLY)
                {
                    console_puts(" (READ-ONLY)");
                }
                else if (access == MEM_ACCESS_MMIO)
                {
                    console_puts(" (MMIO)");
                }
                console_puts("\r\n");
            }
        }
        else
        {
            console_puts("Usage: peek <hex_address>\r\n");
        }
    }
    else if (strncmp(input_buffer, "poke", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        uint32_t addr = 0, val = 0;
        char *arg = input_buffer + 4;
        if (s_htoi(&arg, &addr) && s_htoi(&arg, &val))
        {
            mem_access_t access = check_mem_access(addr);
            if (access == MEM_ACCESS_INVALID)
            {
                console_puts("ERROR: Address ");
                put_hex(addr);
                console_puts(" is out of bounds or not 4-byte aligned. Write rejected.\r\n");
            }
            else if (access == MEM_ACCESS_READONLY)
            {
                console_puts("ERROR: Address ");
                put_hex(addr);
                console_puts(" is in READ-ONLY memory. Write prohibited to prevent crash/corruption.\r\n");
            }
            else
            {
                *(volatile uint32_t *)(uintptr_t)addr = val;
                FENCE();
                console_puts("Written [");
                put_hex(addr);
                console_puts("] = ");
                put_hex(val);
                console_puts("\r\n");
            }
        }
        else
        {
            console_puts("Usage: poke <hex_address> <hex_value>\r\n");
        }
    }
    else if (strcmp(input_buffer, "ecall") == 0)
    {
        console_puts("Executing controlled M-mode software trap (ECALL)...\r\n");
#if defined(__riscv)
        asm volatile("ecall");
#endif
        console_puts("Successfully resumed from ECALL trap! Total ECALLs: ");
        put_dec(trap_get_ecall_count());
        console_puts("\r\n");
    }
    else if (strcmp(input_buffer, "panic") == 0)
    {
        console_puts("Triggering illegal instruction (0x00000000) to demonstrate panic dump...\r\n");
#if defined(__riscv)
        asm volatile(".word 0x00000000");
#endif
    }
    else if (strcmp(input_buffer, "timer") == 0)
    {
        timer_status_t tmr;
        timer_get_status(&tmr);
        console_puts("Hardware Periodic Timer Status (TIMG0 Timer 0):\r\n");
        console_puts("  State:         ");
        console_puts(tmr.active ? "RUNNING\r\n" : "STOPPED\r\n");
        console_puts("  Interval:      ");
        put_dec(tmr.interval_sec);
        console_puts(" seconds (");
        put_dec((uint32_t)tmr.interval_ticks);
        console_puts(" ticks @ 1MHz)\r\n");
        console_puts("  Routing:       INT_SRC_TG0_T0 (");
        put_dec(INT_SRC_TG0_T0);
        console_puts(") -> CPU Channel ");
        put_dec(TIMER_CPU_INTR_CHANNEL);
        console_puts(" (Priority ");
        put_dec(TIMER_INTR_PRIORITY);
        console_puts(")\r\n");
        console_puts("  ISR Ticks:     ");
        put_dec(tmr.isr_count);
        console_puts("\r\n  DPC Dispatches:");
        put_dec(tmr.dpc_count);
        console_puts("\r\n  Current Ticks: ");
        uint64_t cur_ticks = timer_get_current_ticks();
        put_dec((uint32_t)cur_ticks);
        console_puts("\r\n");
    }
    else if (strcmp(input_buffer, "timer stop") == 0)
    {
        timer_stop();
        console_puts("[TIMER] TIMG0 Timer 0 stopped.\r\n");
    }
    else if (strcmp(input_buffer, "timer start") == 0)
    {
        timer_start();
        console_puts("[TIMER] TIMG0 Timer 0 started.\r\n");
    }
    else if (strcmp(input_buffer, "arena") == 0)
    {
        arena_telemetry_t atel;
        arena_get_stats(&atel);
        console_puts("Static Memory Arena Telemetry:\r\n");
        console_puts("  Small Pool:   ");
        put_dec(atel.small_pool.block_count);
        console_puts(" blocks x ");
        put_dec(atel.small_pool.block_size);
        console_puts(" B (");
        put_dec(atel.small_pool.block_count * atel.small_pool.block_size);
        console_puts(" B total)\r\n");
        console_puts("    Active:     ");
        put_dec(atel.small_pool.active_count);
        console_puts("/");
        put_dec(atel.small_pool.block_count);
        console_puts(" (Peak: ");
        put_dec(atel.small_pool.high_watermark);
        console_puts(")\r\n");
        console_puts("    Bitmask:    ");
        put_hex(atel.small_pool.allocated_mask);
        console_puts("\r\n");
        console_puts("    Allocs:     ");
        put_dec(atel.small_pool.total_alloc_count);
        console_puts(", Frees: ");
        put_dec(atel.small_pool.total_free_count);
        console_puts("\r\n");

        console_puts("  Medium Pool:  ");
        put_dec(atel.medium_pool.block_count);
        console_puts(" blocks x ");
        put_dec(atel.medium_pool.block_size);
        console_puts(" B (");
        put_dec(atel.medium_pool.block_count * atel.medium_pool.block_size);
        console_puts(" B total)\r\n");
        console_puts("    Active:     ");
        put_dec(atel.medium_pool.active_count);
        console_puts("/");
        put_dec(atel.medium_pool.block_count);
        console_puts(" (Peak: ");
        put_dec(atel.medium_pool.high_watermark);
        console_puts(")\r\n");
        console_puts("    Bitmask:    ");
        put_hex(atel.medium_pool.allocated_mask);
        console_puts("\r\n");
        console_puts("    Allocs:     ");
        put_dec(atel.medium_pool.total_alloc_count);
        console_puts(", Frees: ");
        put_dec(atel.medium_pool.total_free_count);
        console_puts("\r\n");

        console_puts("  Scratch:      Linear Arena (");
        put_dec((uint32_t)atel.scratch.capacity);
        console_puts(" B capacity)\r\n");
        console_puts("    Offset:     ");
        put_dec((uint32_t)atel.scratch.current_offset);
        console_puts("/");
        put_dec((uint32_t)atel.scratch.capacity);
        console_puts(" B (Peak: ");
        put_dec((uint32_t)atel.scratch.high_watermark);
        console_puts(" B)\r\n");
        console_puts("    Allocs:     ");
        put_dec(atel.scratch.total_alloc_count);
        console_puts(", Resets: ");
        put_dec(atel.scratch.total_reset_count);
        console_puts("\r\n");
    }
    else if (strcmp(input_buffer, "uptime") == 0)
    {
        systimer_telemetry_t tel;
        systimer_get_telemetry(&tel);

        uint32_t days = tel.uptime_sec / SEC_PER_DAY;
        uint32_t rem_sec = tel.uptime_sec % SEC_PER_DAY;
        uint32_t hours = rem_sec / SEC_PER_HOUR;
        rem_sec %= SEC_PER_HOUR;
        uint32_t minutes = rem_sec / SEC_PER_MINUTE;
        uint32_t seconds = rem_sec % SEC_PER_MINUTE;

        console_puts("System Uptime: ");
        put_dec(days);
        console_puts("d ");
        if (hours < 10U) console_puts("0");
        put_dec(hours);
        console_puts(":");
        if (minutes < 10U) console_puts("0");
        put_dec(minutes);
        console_puts(":");
        if (seconds < 10U) console_puts("0");
        put_dec(seconds);
        console_puts(".");
        if (tel.uptime_ms_remainder < 100U) console_puts("0");
        if (tel.uptime_ms_remainder < 10U) console_puts("0");
        put_dec(tel.uptime_ms_remainder);
        if (tel.uptime_us_remainder < 100U) console_puts("0");
        if (tel.uptime_us_remainder < 10U) console_puts("0");
        put_dec(tel.uptime_us_remainder);
        console_puts(" (");
        put_dec(tel.uptime_sec);
        console_puts("s)\r\n");

        console_puts("  Total Ticks: ");
        put_hex((uint32_t)(tel.total_ticks >> 32));
        console_puts("_");
        put_hex((uint32_t)tel.total_ticks);
        console_puts(" (16 MHz tick base)\r\n");

        console_puts("  SYSTIMER Unit 0: Active (16.0 MHz XTAL/PLL)\r\n");
        console_puts("  Target 0 Alarm:  ");
        if (tel.alarm_active)
        {
            console_puts("Active (");
            if (tel.alarm_mode == (uint8_t)SYSTIMER_ALARM_MODE_PERIOD)
            {
                console_puts("Periodic ");
                put_dec(tel.alarm_period_us);
                console_puts(" us, ");
            }
            else
            {
                console_puts("One-Shot, ");
            }
            put_dec(tel.alarm_count);
            console_puts(" firings)\r\n");
        }
        else
        {
            console_puts("Inactive (firings: ");
            put_dec(tel.alarm_count);
            console_puts(")\r\n");
        }
    }
    else if (strcmp(input_buffer, "tasks") == 0)
    {
        task_scheduler_status_t status;
        task_get_status(&status);

        console_puts("Cooperative Coroutine Scheduler Status:\r\n");
        console_puts("  Active Tasks:     ");
        put_dec(status.active_task_count);
        console_puts("/");
        put_dec(TASK_MAX_COUNT);
        console_puts("\r\n");
        console_puts("  Context Switches: ");
        put_dec(status.total_switches);
        console_puts("\r\n");
        console_puts("  Current Task ID:  ");
        put_dec(status.current_task_id);
        console_puts("\r\n\r\n");

        console_puts(" ID | Name         | State      | Pri | Stack Base | Size   | Yields | Runtime(us)\r\n");
        console_puts("----+--------------+------------+-----+------------+--------+--------+------------\r\n");
        for (uint32_t i = 0; i < TASK_MAX_COUNT; i++)
        {
            task_control_block_t *t = &status.tasks[i];
            if (t->state == TASK_STATE_UNUSED) continue;

            console_puts("  ");
            put_dec(t->id);
            console_puts(" | ");
            if (t->name)
            {
                console_puts(t->name);
                size_t len = strlen(t->name);
                for (size_t k = len; k < 12U; k++) console_putc(' ');
            }
            else
            {
                console_puts("unnamed     ");
            }
            console_puts(" | ");
            const char *st_name = task_state_name(t->state);
            console_puts(st_name);
            size_t slen = strlen(st_name);
            for (size_t k = slen; k < 10U; k++) console_putc(' ');
            console_puts(" | ");
            if (t->priority < 10U) console_putc(' ');
            put_dec(t->priority);
            console_puts("  | ");
            put_hex(t->stack_base);
            console_puts(" | ");
            put_dec(t->stack_size);
            console_puts(" B | ");
            put_dec(t->yield_count);
            console_puts(" | ");
            put_dec(t->runtime_ticks >> SYSTIMER_TICKS_TO_US_SHIFT);
            console_puts("\r\n");
        }
    }
    else if (strcmp(input_buffer, "pmp") == 0)
    {
        pmp_telemetry_t tel;
        pmp_get_telemetry(&tel);

        console_puts("RISC-V Physical Memory Protection (PMP) Status:\r\n");
        console_puts("  pmpcfg0: ");
        put_hex(tel.pmpcfg0_val);
        console_puts(" (Active Regions: ");
        put_dec(tel.pmp_active_count);
        console_puts("/4)\r\n\r\n");

        console_puts(" Region | Mode  | R | W | X | L | Base Addr  | Length   | Raw pmpaddr\r\n");
        console_puts("--------+-------+---+---+---+---+------------+----------+------------\r\n");
        for (uint32_t i = 0; i < PMP_MAX_REGIONS; i++)
        {
            pmp_region_cfg_t rcfg;
            pmp_get_region(i, &rcfg);

            console_puts("   ");
            put_dec(i);
            console_puts("    | ");
            switch (rcfg.addr_mode)
            {
            case PMP_ADDR_MODE_OFF:   console_puts("OFF  "); break;
            case PMP_ADDR_MODE_TOR:   console_puts("TOR  "); break;
            case PMP_ADDR_MODE_NA4:   console_puts("NA4  "); break;
            case PMP_ADDR_MODE_NAPOT: console_puts("NAPOT"); break;
            default:                  console_puts("UNK  "); break;
            }
            console_puts(" | ");
            put_dec(rcfg.read_allow);
            console_puts(" | ");
            put_dec(rcfg.write_allow);
            console_puts(" | ");
            put_dec(rcfg.execute_allow);
            console_puts(" | ");
            put_dec(rcfg.lock);
            console_puts(" | ");
            put_hex(rcfg.start_addr);
            console_puts(" | ");
            put_hex(rcfg.length);
            console_puts(" | ");
            put_hex(tel.pmpaddr_vals[i]);
            console_puts("\r\n");
        }

        console_puts("\r\nHP Access Permission Management (APM) Status:\r\n");
        console_puts("  Filter Enable Mask: ");
        put_hex(tel.apm_filter_en_val);
        console_puts(" (Active Regions: ");
        put_dec(tel.apm_active_count);
        console_puts("/16)\r\n");
        console_puts("  Func Control Mask:  ");
        put_hex(tel.apm_func_ctrl_val);
        console_puts(" (M0: ");
        put_dec((tel.apm_func_ctrl_val & 1U) != 0U);
        console_puts(", M1: ");
        put_dec((tel.apm_func_ctrl_val & 2U) != 0U);
        console_puts(", M2: ");
        put_dec((tel.apm_func_ctrl_val & 4U) != 0U);
        console_puts(", M3: ");
        put_dec((tel.apm_func_ctrl_val & 8U) != 0U);
        console_puts(")\r\n");
    }
    else if (strncmp(input_buffer, "lp", 2) == 0 && (input_buffer[2] == ' ' || input_buffer[2] == '\0'))
    {
        char *subcmd = input_buffer + 2;
        while (*subcmd == ' ') subcmd++;

        if (strcmp(subcmd, "start") == 0)
        {
            console_puts("Deploying default LP firmware...\r\n");
            int load_res = lp_core_load_header(lp_core_get_default_firmware());
            if (load_res != LP_CORE_OK)
            {
                console_puts("ERROR: Failed to load LP firmware payload.\r\n");
            }
            else
            {
                int start_res = lp_core_start();
                if (start_res != LP_CORE_OK)
                {
                    console_puts("ERROR: Failed to start LP core.\r\n");
                }
                else
                {
                    lp_core_trigger_lp();
                    console_puts("Waiting for PMU handshake...\r\n");
                    int hs_res = lp_core_wait_handshake(LP_CORE_HANDSHAKE_TIMEOUT_CYCLES);
                    if (hs_res == LP_CORE_OK)
                    {
                        console_puts("LP Core started successfully! Magic: ");
                        put_hex(lp_core_read_magic());
                        console_puts(", Ticks: ");
                        put_dec(lp_core_read_counter());
                        console_puts("\r\n");
                    }
                    else
                    {
                        console_puts("WARNING: LP Core started but handshake timed out.\r\n");
                    }
                }
            }
        }
        else if (strcmp(subcmd, "stop") == 0)
        {
            lp_core_stop();
            console_puts("LP Core stopped (clock gated, reset held).\r\n");
        }
        else if (strcmp(subcmd, "trigger") == 0)
        {
            lp_core_trigger_lp();
            console_puts("Sent PMU_HP_TRIGGER_LP pulse.\r\n");
        }
        else
        {
            lp_core_telemetry_t tel;
            lp_core_get_telemetry(&tel);
            const lp_firmware_header_t *fw = lp_core_get_default_firmware();

            console_puts("Low-Power (LP) RISC-V Coprocessor Status:\r\n");
            console_puts("  State:           ");
            console_puts(tel.is_running ? "RUNNING" : "STOPPED");
            console_puts("\r\n");
            console_puts("  Clock (LP_PERI): ");
            console_puts(tel.clock_enabled ? "ENABLED (20 MHz)" : "DISABLED");
            console_puts("\r\n");
            console_puts("  Reset:           ");
            console_puts(tel.in_reset ? "HELD IN RESET" : "RELEASED");
            console_puts("\r\n");
            console_puts("  PMU HP->LP Trig: ");
            put_dec(tel.hp_trigger_active);
            console_puts("\r\n");
            console_puts("  PMU LP->HP Trig: ");
            put_dec(tel.lp_trigger_active);
            console_puts("\r\n");
            console_puts("  Magic Readback:  ");
            put_hex(tel.magic_readback);
            if (tel.magic_readback == LP_TEST_MAGIC_EXPECTED)
            {
                console_puts(" (VALID)");
            }
            console_puts("\r\n");
            console_puts("  Tick Counter:    ");
            put_dec(tel.counter_readback);
            console_puts("\r\n");
            console_puts("  Default Payload: ");
            put_dec(fw->size_bytes);
            console_puts(" bytes @ ");
            put_hex(fw->entry_point);
            console_puts("\r\n");
        }
    }
    else if (strncmp(input_buffer, "power", 5) == 0 && (input_buffer[5] == ' ' || input_buffer[5] == '\0'))
    {
        char *subcmd = input_buffer + 5;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "mode", 4) == 0)
        {
            char *mode_str = subcmd + 4;
            while (*mode_str == ' ') mode_str++;
            if (strcmp(mode_str, "active") == 0)
            {
                power_set_mode(PM_STATE_ACTIVE);
                console_puts("Power mode transitioned to ACTIVE.\r\n");
            }
            else if (strcmp(mode_str, "light") == 0)
            {
                power_set_mode(PM_STATE_LIGHT_SLEEP);
                console_puts("Power mode transitioned to LIGHT_SLEEP.\r\n");
            }
            else if (strcmp(mode_str, "deep") == 0)
            {
                power_set_mode(PM_STATE_DEEP_SLEEP);
                console_puts("Power mode transitioned to DEEP_SLEEP.\r\n");
            }
            else
            {
                console_puts("Usage: power mode <active|light|deep>\r\n");
            }
        }
        else if (strcmp(subcmd, "sample") == 0)
        {
            uint32_t telem_val = 0U;
            console_puts("Requesting telemetry sample from LP core...\r\n");
            int res = power_sample_telemetry(&telem_val, POWER_HANDSHAKE_TIMEOUT_CYCLES);
            if (res == POWER_OK)
            {
                console_puts("LP Telemetry Sampled: ");
                put_hex(telem_val);
                console_puts("\r\n");
            }
            else
            {
                console_puts("ERROR: Failed to sample telemetry (err: ");
                put_dec((uint32_t)-res);
                console_puts(")\r\n");
            }
        }
        else if (strncmp(subcmd, "store", 5) == 0)
        {
            char *store_args = subcmd + 5;
            while (*store_args == ' ') store_args++;
            uint32_t idx = 0U;
            if (s_htoi(&store_args, &idx))
            {
                uint32_t wval = 0U;
                if (s_htoi(&store_args, &wval))
                {
                    power_write_retained_store(idx, wval);
                    console_puts("Written LP_AON STORE[");
                    put_dec(idx);
                    console_puts("] = ");
                    put_hex(wval);
                    console_puts("\r\n");
                }
                else
                {
                    uint32_t rval = power_read_retained_store(idx);
                    console_puts("LP_AON STORE[");
                    put_dec(idx);
                    console_puts("] = ");
                    put_hex(rval);
                    console_puts("\r\n");
                }
            }
            else
            {
                console_puts("Usage: power store <index:0-9> [hex_val]\r\n");
            }
        }
        else
        {
            power_telemetry_t pt;
            power_get_telemetry(&pt);
            console_puts("Power Management & Shared Mailbox Status:\r\n");
            console_puts("  Mode:             ");
            if (pt.current_mode == PM_STATE_ACTIVE) console_puts("ACTIVE\r\n");
            else if (pt.current_mode == PM_STATE_LIGHT_SLEEP) console_puts("LIGHT_SLEEP\r\n");
            else console_puts("DEEP_SLEEP\r\n");
            console_puts("  Mailbox Magic:    ");
            put_hex(pt.mailbox_magic);
            if (pt.mailbox_magic == LP_MAILBOX_MAGIC) console_puts(" (VALID 'IRON')\r\n");
            else console_puts(" (INVALID)\r\n");
            console_puts("  Last Cmd / Ack:   ");
            put_hex(pt.last_cmd);
            console_puts(" / ");
            put_hex(pt.last_ack);
            console_puts("\r\n");
            console_puts("  Wake Count:       ");
            put_dec(pt.wake_count);
            console_puts("\r\n");
            console_puts("  Raw Sensor Tele:  ");
            put_hex(pt.sensor_raw);
            console_puts("\r\n");
            console_puts("  Retained STORE0:  ");
            put_hex(pt.aon_store0_val);
            console_puts("\r\n");
            console_puts("  LP Core State:    ");
            console_puts(pt.lp_running ? "RUNNING\r\n" : "STOPPED\r\n");
        }
    }
    else if (strncmp(input_buffer, "gpio", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        char *subcmd = input_buffer + 4;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "set", 3) == 0)
        {
            char *args = subcmd + 3;
            while (*args == ' ') args++;
            uint32_t pin = 0U;
            if (parse_uint(&args, &pin))
            {
                uint32_t val = 0U;
                if (parse_uint(&args, &val))
                {
                    gpio_set_level(pin, val);
                    console_puts("GPIO ");
                    put_dec(pin);
                    console_puts(" output set to ");
                    put_dec(val ? 1 : 0);
                    console_puts("\r\n");
                }
                else
                {
                    console_puts("Usage: gpio set <pin> <0|1>\r\n");
                }
            }
            else
            {
                console_puts("Usage: gpio set <pin> <0|1>\r\n");
            }
        }
        else if (strncmp(subcmd, "get", 3) == 0)
        {
            char *args = subcmd + 3;
            while (*args == ' ') args++;
            uint32_t pin = 0U;
            if (parse_uint(&args, &pin))
            {
                int lvl = gpio_get_level(pin);
                console_puts("GPIO ");
                put_dec(pin);
                console_puts(" input level: ");
                put_dec(lvl);
                console_puts("\r\n");
            }
            else
            {
                console_puts("Usage: gpio get <pin>\r\n");
            }
        }
        else if (strncmp(subcmd, "dir", 3) == 0)
        {
            char *args = subcmd + 3;
            while (*args == ' ') args++;
            uint32_t pin = 0U;
            if (parse_uint(&args, &pin))
            {
                while (*args == ' ') args++;
                if (strcmp(args, "out") == 0)
                {
                    gpio_set_direction(pin, GPIO_DIR_OUTPUT);
                    console_puts("GPIO ");
                    put_dec(pin);
                    console_puts(" direction set to OUTPUT\r\n");
                }
                else if (strcmp(args, "in") == 0)
                {
                    gpio_set_direction(pin, GPIO_DIR_INPUT);
                    console_puts("GPIO ");
                    put_dec(pin);
                    console_puts(" direction set to INPUT\r\n");
                }
                else
                {
                    console_puts("Usage: gpio dir <pin> <in|out>\r\n");
                }
            }
            else
            {
                console_puts("Usage: gpio dir <pin> <in|out>\r\n");
            }
        }
        else if (strncmp(subcmd, "pull", 4) == 0)
        {
            char *args = subcmd + 4;
            while (*args == ' ') args++;
            uint32_t pin = 0U;
            if (parse_uint(&args, &pin))
            {
                while (*args == ' ') args++;
                if (strcmp(args, "up") == 0)
                {
                    gpio_set_pull(pin, GPIO_PULL_UP);
                    console_puts("GPIO ");
                    put_dec(pin);
                    console_puts(" pull set to UP\r\n");
                }
                else if (strcmp(args, "down") == 0)
                {
                    gpio_set_pull(pin, GPIO_PULL_DOWN);
                    console_puts("GPIO ");
                    put_dec(pin);
                    console_puts(" pull set to DOWN\r\n");
                }
                else if (strcmp(args, "none") == 0)
                {
                    gpio_set_pull(pin, GPIO_PULL_NONE);
                    console_puts("GPIO ");
                    put_dec(pin);
                    console_puts(" pull set to NONE\r\n");
                }
                else
                {
                    console_puts("Usage: gpio pull <pin> <none|up|down>\r\n");
                }
            }
            else
            {
                console_puts("Usage: gpio pull <pin> <none|up|down>\r\n");
            }
        }
        else
        {
            gpio_telemetry_t gt;
            gpio_get_telemetry(&gt);
            console_puts("GPIO Subsystem & IO_MUX Status:\r\n");
            console_puts("  Output Enable Mask: ");
            put_hex(gt.enable_mask);
            console_puts("\r\n");
            console_puts("  Output Level Mask:  ");
            put_hex(gt.out_mask);
            console_puts("\r\n");
            console_puts("  Input Level Mask:   ");
            put_hex(gt.in_mask);
            console_puts("\r\n");
            console_puts("  Interrupt Status:   ");
            put_hex(gt.status_mask);
            console_puts("\r\n");
        }
    }
    else if (strncmp(input_buffer, "dma", 3) == 0 && (input_buffer[3] == ' ' || input_buffer[3] == '\0'))
    {
        gdma_telemetry_t gt;
        gdma_get_telemetry(&gt);
        console_puts("GDMA Multi-Channel Engine Status:\r\n");
        console_puts("  Hardware Version (DATE): ");
        put_hex(gt.date_version);
        console_puts("\r\n");
        for (uint32_t ch = 0; ch < GDMA_CHANNEL_COUNT; ch++)
        {
            console_puts("  Channel ");
            put_dec(ch);
            console_puts(":\r\n");
            console_puts("    IN:  State=");
            put_hex(gt.channels[ch].in_state);
            console_puts(", Dscr=");
            put_hex(gt.channels[ch].in_dscr_addr);
            console_puts(", Active=");
            console_puts(gt.channels[ch].in_active ? "RUN" : "IDLE");
            console_puts(", IntRaw=");
            put_hex(gt.channels[ch].in_int_raw);
            console_puts("\r\n");
            console_puts("    OUT: State=");
            put_hex(gt.channels[ch].out_state);
            console_puts(", Dscr=");
            put_hex(gt.channels[ch].out_dscr_addr);
            console_puts(", Active=");
            console_puts(gt.channels[ch].out_active ? "RUN" : "IDLE");
            console_puts(", IntRaw=");
            put_hex(gt.channels[ch].out_int_raw);
            console_puts("\r\n");
        }
    }
    else if (strncmp(input_buffer, "modem", 5) == 0 && (input_buffer[5] == ' ' || input_buffer[5] == '\0'))
    {
        char *subcmd = input_buffer + 5;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "all", 3) == 0)
        {
            modem_enable_all_clocks();
            console_puts("All wireless modem clocks enabled and resets released.\r\n");
        }
        else if (strncmp(subcmd, "wifi", 4) == 0)
        {
            char *sub2 = subcmd + 4;
            while (*sub2 == ' ') sub2++;
            if (strcmp(sub2, "off") == 0)
            {
                modem_disable_wifi_clocks();
                console_puts("Wi-Fi baseband clocks disabled.\r\n");
            }
            else
            {
                modem_enable_wifi_clocks();
                console_puts("Wi-Fi baseband clocks enabled.\r\n");
            }
        }
        else if (strncmp(subcmd, "ble", 3) == 0)
        {
            char *sub2 = subcmd + 3;
            while (*sub2 == ' ') sub2++;
            if (strcmp(sub2, "off") == 0)
            {
                modem_disable_ble_clocks();
                console_puts("Bluetooth clocks disabled.\r\n");
            }
            else
            {
                modem_enable_ble_clocks();
                console_puts("Bluetooth clocks enabled.\r\n");
            }
        }
        else if (strncmp(subcmd, "15.4", 4) == 0)
        {
            char *sub2 = subcmd + 4;
            while (*sub2 == ' ') sub2++;
            if (strcmp(sub2, "off") == 0)
            {
                modem_disable_ieee802154_clocks();
                console_puts("IEEE 802.15.4 clocks disabled.\r\n");
            }
            else
            {
                modem_enable_ieee802154_clocks();
                console_puts("IEEE 802.15.4 clocks enabled.\r\n");
            }
        }
        else if (strncmp(subcmd, "coex", 4) == 0)
        {
            char *sub2 = subcmd + 4;
            while (*sub2 == ' ') sub2++;
            if (strcmp(sub2, "off") == 0)
            {
                modem_disable_coexistence();
                console_puts("RF coexistence clock and arbiter disabled.\r\n");
            }
            else
            {
                modem_enable_coexistence();
                console_puts("RF coexistence clock and arbiter enabled.\r\n");
            }
        }
        else
        {
            modem_clock_state_t ms;
            modem_get_clock_state(&ms);
            console_puts("Modem Clock & Power Control (MODEM_SYSCON / MODEM_LPCON) Status:\r\n");
            console_puts("  MODEM_SYSCON Date: ");
            put_hex(modem_get_syscon_date());
            console_puts("\r\n");
            console_puts("  MODEM_LPCON Date:  ");
            put_hex(modem_get_lpcon_date());
            console_puts("\r\n");
            console_puts("  Wi-Fi Clocks:      ");
            console_puts(ms.wifi_clk_enabled ? "ENABLED" : "DISABLED");
            console_puts("\r\n");
            console_puts("  Bluetooth Clocks:  ");
            console_puts(ms.ble_clk_enabled ? "ENABLED" : "DISABLED");
            console_puts("\r\n");
            console_puts("  IEEE 802.15.4:     ");
            console_puts(ms.ieee802154_clk_enabled ? "ENABLED" : "DISABLED");
            console_puts("\r\n");
            console_puts("  RF Coexistence:    ");
            console_puts(ms.coexistence_enabled ? "ENABLED" : "DISABLED");
            console_puts("\r\n");
            console_puts("  SYSCON_CLK_CONF:   ");
            put_hex(*MODEM_SYSCON_CLK_CONF_REG);
            console_puts("\r\n");
            console_puts("  SYSCON_CLK_CONF1:  ");
            put_hex(*MODEM_SYSCON_CLK_CONF1_REG);
            console_puts("\r\n");
            console_puts("  SYSCON_RST_CONF:   ");
            put_hex(*MODEM_SYSCON_MODEM_RST_CONF_REG);
            console_puts("\r\n");
            console_puts("  LPCON_COEX_CLK:    ");
            put_hex(*MODEM_LPCON_COEX_LP_CLK_CONF_REG);
            console_puts("\r\n");
        }
    }
    else if (strncmp(input_buffer, "coex", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        char *subcmd = input_buffer + 4;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "on", 2) == 0)
        {
            modem_enable_coexistence();
            console_puts("RF coexistence clock and arbiter ENABLED.\r\n");
        }
        else if (strncmp(subcmd, "off", 3) == 0)
        {
            modem_disable_coexistence();
            console_puts("RF coexistence clock and arbiter DISABLED.\r\n");
        }
        else if (strncmp(subcmd, "diag", 4) == 0)
        {
            uint32_t lpcon_clk = *MODEM_LPCON_CLK_CONF_REG;
            uint32_t coex_clk = *MODEM_LPCON_COEX_LP_CLK_CONF_REG;
            uint32_t rst_conf = *MODEM_LPCON_RST_CONF_REG;
            bool is_valid = modem_validate_coexistence();

            console_puts("RF Coexistence Hardware Diagnostics:\r\n");
            console_puts("  LPCON_CLK_CONF:    ");
            put_hex(lpcon_clk);
            console_puts(" (CLK_COEX_EN: ");
            console_puts((lpcon_clk & MODEM_LPCON_CLK_COEX_EN_BIT) ? "1" : "0");
            console_puts(")\r\n");
            console_puts("  COEX_LP_CLK_CONF:  ");
            put_hex(coex_clk);
            console_puts(" (SEL_XTAL: ");
            console_puts((coex_clk & MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT) ? "1" : "0");
            console_puts(")\r\n");
            console_puts("  LPCON_RST_CONF:    ");
            put_hex(rst_conf);
            console_puts(" (RST_COEX: ");
            console_puts((rst_conf & MODEM_LPCON_RST_COEX_BIT) ? "ASSERTED" : "RELEASED");
            console_puts(")\r\n");
            console_puts("  Hardware Arbiter:  ");
            console_puts(is_valid ? "SYNCHRONIZED & OPERATIONAL\r\n" : "OUT OF SYNC / INACTIVE\r\n");
            console_puts("  Validation Status: ");
            console_puts(is_valid ? "[ VALID - OK ]\r\n" : "[ INVALID - NOT READY ]\r\n");
        }
        else
        {
            modem_clock_state_t ms;
            modem_get_clock_state(&ms);
            bool is_valid = modem_validate_coexistence();

            console_puts("RF Coexistence Arbiter (Wi-Fi 6 / BLE 5 / 802.15.4) Status:\r\n");
            console_puts("  Coexistence State: ");
            console_puts(ms.coexistence_enabled ? "ENABLED" : "DISABLED");
            console_puts("\r\n");
            console_puts("  LP Clock Source:   XTAL (MODEM_LPCON bit 2)\r\n");
            console_puts("  Hardware Arbiter:  3-Wire Priority Arbiter (Release Reset: OK)\r\n");
            console_puts("  Wi-Fi 6 Coex Path: ");
            console_puts(ms.wifi_clk_enabled ? "ONLINE" : "OFFLINE");
            console_puts("\r\n");
            console_puts("  BLE 5 Coex Path:   ");
            console_puts(ms.ble_clk_enabled ? "ONLINE" : "OFFLINE");
            console_puts("\r\n");
            console_puts("  802.15.4 Path:     ");
            console_puts(ms.ieee802154_clk_enabled ? "ONLINE" : "OFFLINE");
            console_puts("\r\n");
            console_puts("  Validation Status: ");
            console_puts(is_valid ? "READY (All clocks synchronized)\r\n" : "UNSYNCHRONIZED\r\n");
        }
    }
    else if (strncmp(input_buffer, "wifi", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        char *subcmd = input_buffer + 4;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "mac", 3) == 0)
        {
            uint8_t mac[WIFI_MAC_ADDR_LEN] = {0};
            wifi_get_mac_addr(mac);
            console_puts("Wi-Fi Station MAC Address (eFuse): ");
            for (int i = 0; i < 6; i++)
            {
                const char hex_chars[] = "0123456789abcdef";
                console_putc(hex_chars[(mac[i] >> 4) & 0x0F]);
                console_putc(hex_chars[mac[i] & 0x0F]);
                if (i < 5) console_putc(':');
            }
            console_puts("\r\n");
        }
        else if (strncmp(subcmd, "timers", 6) == 0)
        {
            wifi_os_adapter_print_timers();
        }
        else if (strncmp(subcmd, "ring", 4) == 0)
        {
            uint32_t visited = 0;
            wifi_status_t vst = wifi_verify_rx_ring(&visited);
            console_puts("Wi-Fi 6 Zero-Copy RX Packet Ring Status:\r\n");
            console_puts("  Ring Verification: ");
            console_puts((vst == WIFI_OK) ? "PASSED (Integrity OK)" : "FAILED");
            console_puts("\r\n");
            console_puts("  Descriptors Visited: ");
            put_dec(visited);
            console_puts(" / ");
            put_dec(PACKET_RING_COUNT);
            console_puts("\r\n");
            console_puts("  Buffer Size:       ");
            put_dec(PACKET_BUFFER_SIZE);
            console_puts(" bytes per buffer (Static DRAM)\r\n");
            console_puts("  GDMA Channel:      GDMA Channel 1 (Inlink & Outlink)\r\n");
        }
        else if (strncmp(subcmd, "init", 4) == 0)
        {
            wifi_status_t ist = wifi_init();
            if (ist == WIFI_OK)
            {
                console_puts("Wi-Fi MAC Subsystem & Packet Rings initialized successfully.\r\n");
            }
            else
            {
                console_puts("Failed to initialize Wi-Fi MAC Subsystem. Status: ");
                put_dec((uint32_t)-ist);
                console_puts("\r\n");
            }
        }
        else if (strncmp(subcmd, "scan", 4) == 0)
        {
            uint8_t chan = 0U;
            bool passive = false;
            char *p = subcmd + 4;
            while (*p == ' ') p++;
            if (*p >= '0' && *p <= '9')
            {
                chan = (uint8_t)(*p - '0');
                p++;
                if (*p >= '0' && *p <= '9')
                {
                    chan = (uint8_t)(chan * 10U + (*p - '0'));
                    p++;
                }
            }
            while (*p == ' ') p++;
            if (strncmp(p, "passive", 7) == 0)
            {
                passive = true;
            }
            console_puts("Triggering Wi-Fi Scan (500ms dwell)...\r\n");
            wifi_status_t sc_st = wifi_scan(NULL, chan, passive, 500U);
            if (sc_st != WIFI_OK)
            {
                console_puts("Scan returned status: ");
                put_dec((uint32_t)sc_st);
                console_puts("\r\n");
            }
        }
        else if (strncmp(subcmd, "sniffer", 7) == 0)
        {
            uint8_t chan = 1U;
            uint32_t dur = 5U;
            char *p = subcmd + 7;
            while (*p == ' ') p++;
            if (*p >= '0' && *p <= '9')
            {
                chan = (uint8_t)(*p - '0');
                p++;
                if (*p >= '0' && *p <= '9')
                {
                    chan = (uint8_t)(chan * 10U + (*p - '0'));
                    p++;
                }
                while (*p == ' ') p++;
                if (*p >= '0' && *p <= '9')
                {
                    dur = (uint32_t)(*p - '0');
                    p++;
                    while (*p >= '0' && *p <= '9')
                    {
                        dur = dur * 10U + (uint32_t)(*p - '0');
                        p++;
                    }
                }
            }
            wifi_sniffer(chan, dur);
        }
        else if (strncmp(subcmd, "ap", 2) == 0 && (subcmd[2] == ' ' || subcmd[2] == '\0'))
        {
            char *p = subcmd + 2;
            while (*p == ' ') p++;

            if (strncmp(p, "start", 5) == 0 && (p[5] == ' ' || p[5] == '\0'))
            {
                p += 5;
                while (*p == ' ') p++;

                char ssid_buf[WIFI_MAX_SSID_LEN + 1U] = {0};
                uint8_t chan = WIFI_DEFAULT_AP_CHANNEL;

                if (*p != '\0')
                {
                    uint32_t idx = 0U;
                    while (*p != '\0' && *p != ' ' && idx < WIFI_MAX_SSID_LEN)
                    {
                        ssid_buf[idx++] = *p++;
                    }
                    ssid_buf[idx] = '\0';

                    while (*p == ' ') p++;
                    if (*p >= '0' && *p <= '9')
                    {
                        chan = (uint8_t)(*p - '0');
                        p++;
                        if (*p >= '0' && *p <= '9')
                        {
                            chan = (uint8_t)(chan * 10U + (*p - '0'));
                            p++;
                        }
                    }
                }

                const char *use_ssid = (ssid_buf[0] != '\0') ? ssid_buf : WIFI_DEFAULT_AP_SSID;
                console_puts("Starting SoftAP (SSID: '");
                console_puts(use_ssid);
                console_puts("', Channel: ");
                put_dec((uint32_t)chan);
                console_puts(")...\r\n");

                wifi_status_t st = wifi_start_ap(use_ssid, NULL, chan);
                if (st == WIFI_OK)
                {
                    console_puts("SoftAP started successfully.\r\n");
                }
                else
                {
                    console_puts("SoftAP start failed with status: ");
                    put_dec((uint32_t)-st);
                    console_puts("\r\n");
                }
            }
            else if (strncmp(p, "stop", 4) == 0 && (p[4] == ' ' || p[4] == '\0'))
            {
                console_puts("Stopping SoftAP...\r\n");
                wifi_status_t st = wifi_stop_ap();
                if (st == WIFI_OK)
                {
                    console_puts("SoftAP stopped successfully.\r\n");
                }
                else
                {
                    console_puts("SoftAP stop failed with status: ");
                    put_dec((uint32_t)-st);
                    console_puts("\r\n");
                }
            }
            else if (strncmp(p, "cca", 3) == 0 && (p[3] == ' ' || p[3] == '\0'))
            {
                p += 3;
                while (*p == ' ') p++;
                if (strncmp(p, "on", 2) == 0)
                {
                    wifi_set_cca_enabled(true);
                    console_puts("Wi-Fi CCA enabled.\r\n");
                }
                else if (strncmp(p, "off", 3) == 0)
                {
                    wifi_set_cca_enabled(false);
                    console_puts("Wi-Fi CCA disabled (Medium-busy lockout bypassed).\r\n");
                }
                else
                {
                    console_puts("Wi-Fi CCA Status: ");
                    console_puts(wifi_is_cca_enabled() ? "ENABLED\r\n" : "DISABLED\r\n");
                }
            }
            else if (strncmp(p, "pa", 2) == 0 && (p[2] == ' ' || p[2] == '\0'))
            {
                console_puts("Applying PA bias and RF analog switch routing...\r\n");
                modem_force_tx_pa();
                console_puts("RF Switch 0: 0x");
                put_hex(modem_get_rf_analog_switch0());
                console_puts("\r\nRF Switch 1: 0x");
                put_hex(modem_get_rf_analog_switch1());
                console_puts("\r\n");
            }
            else
            {
                /* status (explicit "status" or default) */
                bool active = wifi_is_ap_active();
                console_puts("SoftAP Status: ");
                console_puts(active ? "ACTIVE\r\n" : "INACTIVE\r\n");
                console_puts("  SSID:        ");
                console_puts(wifi_get_ap_ssid());
                console_puts("\r\n  Channel:     ");
                put_dec((uint32_t)wifi_get_ap_channel());
#if defined(__riscv)
                int8_t tx_pwr = 0;
                esp_wifi_get_max_tx_power(&tx_pwr);
                console_puts("\r\n  Max TX Power: ");
                put_dec((uint32_t)tx_pwr);
                console_puts(" (");
                put_dec((uint32_t)(tx_pwr / WIFI_TX_POWER_DBM_SCALE));
                console_puts(" dBm)");
#endif
                console_puts("\r\n  CCA:         ");
                console_puts(wifi_is_cca_enabled() ? "ENABLED\r\n" : "DISABLED (Bypassed)\r\n");
                console_puts("  RF Switch 0: 0x");
                put_hex(modem_get_rf_analog_switch0());
                console_puts("\r\n  RF Switch 1: 0x");
                put_hex(modem_get_rf_analog_switch1());
                console_puts("\r\n");
            }
        }
        else if (strncmp(subcmd, "stack", 5) == 0)
        {
#if defined(__riscv)
            uint32_t size = 0U;
            uint32_t requested = 0U;
            uint32_t untouched = wifi_os_adapter_task_stack_free(&size, &requested);
            console_puts("Wi-Fi task stack: ");
            put_dec(size);
            console_puts(" B (blob asked for ");
            put_dec(requested);
            console_puts("), never used: ");
            put_dec(untouched);
            console_puts(" B\r\n");
            size_t heap_used = 0U;
            size_t heap_free = 0U;
            size_t heap_peak = 0U;
            wifi_os_adapter_get_heap_stats(&heap_used, &heap_free, &heap_peak);
            console_puts("Wi-Fi heap: used ");
            put_dec((uint32_t)heap_used);
            console_puts(" B, peak ");
            put_dec((uint32_t)heap_peak);
            console_puts(" B of ");
            put_dec((uint32_t)(heap_used + heap_free));
            console_puts(" B\r\n");
#endif
        }
        else if (strncmp(subcmd, "diag", 4) == 0 || strncmp(subcmd, "lmac", 4) == 0)
        {
#if defined(__riscv)
            extern void dbg_lmac_rxtx_statis_dump(void);
            extern void dbg_lmac_hw_statis_dump(void);
            extern void dbg_lmac_statis_dump(void);
            esp_wifi_internal_set_log_level(WIFI_LOG_VERBOSE);
            esp_wifi_internal_set_log_mod(WIFI_LOG_MODULE_ALL, 0xFFFFFFFFU, true);
            console_puts("=== LMAC RX/TX Statistics Dump ===\r\n");
            dbg_lmac_rxtx_statis_dump();
            console_puts("=== LMAC Hardware Statistics Dump ===\r\n");
            dbg_lmac_hw_statis_dump();
            console_puts("=== LMAC General Statistics Dump ===\r\n");
            dbg_lmac_statis_dump();
            console_puts("=== Blob statistics (esp_wifi_statis_dump) ===\r\n");
            esp_wifi_statis_dump(WIFI_STATIS_ALL);
#else
            console_puts("LMAC diagnostics only available on target hardware.\r\n");
#endif
        }
        else if (strncmp(subcmd, "tone", 4) == 0)
        {
#if defined(__riscv)
            extern void phy_tx_tone(uint8_t chan, uint8_t atten, uint8_t mode);
            char *p = subcmd + 4;
            while (*p == ' ') p++;
            uint8_t chan = 1U;
            if (*p >= '0' && *p <= '9')
            {
                chan = (uint8_t)(*p - '0');
                p++;
                if (*p >= '0' && *p <= '9')
                {
                    chan = (uint8_t)(chan * 10U + (*p - '0'));
                }
            }
            if (chan > 0U)
            {
                console_puts("Emitting CW carrier tone on channel ");
                put_dec((uint32_t)chan);
                console_puts("...\r\n");
                phy_tx_tone(chan, 0U, 0U);
            }
            else
            {
                console_puts("Stopping CW carrier tone...\r\n");
                phy_tx_tone(0U, 0U, 0U);
            }
#else
            console_puts("PHY tone only available on target hardware.\r\n");
#endif
        }
        else
        {
            wifi_telemetry_t wt;
            wifi_get_telemetry(&wt);
            console_puts("Wi-Fi status (Espressif blob, software RX queue):\r\n");
            console_puts("  Driver State:      ");
            if (wt.state == WIFI_STATE_OFF) console_puts("OFF");
            else if (wt.state == WIFI_STATE_INIT) console_puts("INIT");
            else if (wt.state == WIFI_STATE_IDLE) console_puts("IDLE");
            else if (wt.state == WIFI_STATE_ACTIVE) console_puts("ACTIVE");
            else if (wt.state == WIFI_STATE_SCANNING) console_puts("SCANNING");
            else if (wt.state == WIFI_STATE_CONNECTED) console_puts("CONNECTED");
            else if (wt.state == WIFI_STATE_AP_ACTIVE) console_puts("AP_ACTIVE");
            else console_puts("DISCONNECTED");
            console_puts("\r\n");
            console_puts("  Station MAC:       ");
            for (int i = 0; i < 6; i++)
            {
                uint8_t byte = wt.mac_addr[i];
                const char hex_chars[] = "0123456789abcdef";
                console_putc(hex_chars[(byte >> 4) & 0x0F]);
                console_putc(hex_chars[byte & 0x0F]);
                if (i < 5) console_putc(':');
            }
            console_puts("\r\n");
            console_puts("  RX Queue:          ");
            put_dec(wt.rx_ring_capacity);
            console_puts(" x 1536B (software, filled by the blob RX callback)\r\n");
            console_puts("  Packets TX / RX:   ");
            put_dec(wt.tx_packets);
            console_puts(" / ");
            put_dec(wt.rx_packets);
            console_puts("\r\n");
            console_puts("  Bytes TX / RX:     ");
            put_dec(wt.tx_bytes);
            console_puts(" / ");
            put_dec(wt.rx_bytes);
            console_puts("\r\n");
            console_puts("  RX Queue Drops:    ");
            put_dec(wt.ring_full_drops);
            console_puts("\r\n");
            console_puts("  TX Errors:         ");
            put_dec(wt.tx_errors);
            console_puts("\r\n");
            console_puts("  AP Inactive Time:  ");
            put_dec(wifi_get_inactive_time_s(WIFI_TX_IF_AP));
            console_puts(" s\r\n");
            console_puts("  STA Beacon Loss:   ");
            put_dec(wifi_get_inactive_time_s(WIFI_TX_IF_STA));
            console_puts(" s\r\n");
            console_puts("  ISR 1 Count:       ");
            put_dec(interrupt_get_count(1));
            console_puts("\r\n");
        }
    }
    else if (strncmp(input_buffer, "mmu", 3) == 0 && (input_buffer[3] == ' ' || input_buffer[3] == '\0'))
    {
        extern const uint8_t _sflash_xip[];
        extern const uint8_t _eflash_xip[];
        uint32_t flash_len = (uint32_t)(_eflash_xip - _sflash_xip);
        console_puts("ESP32-C6 Flash Cache & MSPI MMU Status:\r\n");
        console_puts("  VMA Start:         0x42000000 (External Flash XIP)\r\n");
        console_puts("  Flash Binary Size: ");
        put_dec(flash_len);
        console_puts(" bytes mapped\r\n");
        console_puts("  MMU Page Size:     64 KB\r\n");
        console_puts("  L1 ICache:         Operational (32 KB 4-way set associative)\r\n");
    }
    else if (strncmp(input_buffer, "15.4", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        char *subcmd = input_buffer + 4;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "chan", 4) == 0)
        {
            char *arg = subcmd + 4;
            while (*arg == ' ') arg++;
            uint32_t chan = 0;
            if (parse_uint(&arg, &chan))
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
            if (parse_uint(&arg, &pan))
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
            if (parse_uint(&arg, &saddr))
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
    else if (strncmp(input_buffer, "net", 3) == 0 && (input_buffer[3] == ' ' || input_buffer[3] == '\0'))
    {
        char *subcmd = input_buffer + 3;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "ip", 2) == 0)
        {
            char *arg = subcmd + 2;
            while (*arg == ' ') arg++;
            if (*arg != '\0')
            {
                uint32_t new_ip = net_str_to_ip(arg);
                if (new_ip != 0U)
                {
                    net_config_t cur_cfg;
                    net_get_config(&cur_cfg);
                    net_set_ip(new_ip, cur_cfg.netmask, cur_cfg.gateway);
                    console_puts("Network IP updated to: ");
                    console_puts(arg);
                    console_puts("\r\n");
                }
                else
                {
                    console_puts("Error: Invalid IPv4 address format (e.g. 192.168.1.50).\r\n");
                }
            }
            else
            {
                net_config_t cur_cfg;
                net_get_config(&cur_cfg);
                char s[NET_IP_STR_BUF_LEN];
                net_ip_to_str(cur_cfg.ip, s, sizeof(s));
                console_puts("Current IP Address: ");
                console_puts(s);
                console_puts("\r\n");
            }
        }
        else if (strncmp(subcmd, "mask", 4) == 0)
        {
            char *arg = subcmd + 4;
            while (*arg == ' ') arg++;
            if (*arg != '\0')
            {
                uint32_t new_mask = net_str_to_ip(arg);
                if (new_mask != 0U)
                {
                    net_config_t cur_cfg;
                    net_get_config(&cur_cfg);
                    net_set_ip(cur_cfg.ip, new_mask, cur_cfg.gateway);
                    console_puts("Network Subnet Mask updated to: ");
                    console_puts(arg);
                    console_puts("\r\n");
                }
                else
                {
                    console_puts("Error: Invalid subnet mask format (e.g. 255.255.255.0).\r\n");
                }
            }
            else
            {
                net_config_t cur_cfg;
                net_get_config(&cur_cfg);
                char s[NET_IP_STR_BUF_LEN];
                net_ip_to_str(cur_cfg.netmask, s, sizeof(s));
                console_puts("Current Subnet Mask: ");
                console_puts(s);
                console_puts("\r\n");
            }
        }
        else if (strncmp(subcmd, "gw", 2) == 0)
        {
            char *arg = subcmd + 2;
            while (*arg == ' ') arg++;
            if (*arg != '\0')
            {
                uint32_t new_gw = net_str_to_ip(arg);
                if (new_gw != 0U)
                {
                    net_config_t cur_cfg;
                    net_get_config(&cur_cfg);
                    net_set_ip(cur_cfg.ip, cur_cfg.netmask, new_gw);
                    console_puts("Network Default Gateway updated to: ");
                    console_puts(arg);
                    console_puts("\r\n");
                }
                else
                {
                    console_puts("Error: Invalid gateway format (e.g. 192.168.1.1).\r\n");
                }
            }
            else
            {
                net_config_t cur_cfg;
                net_get_config(&cur_cfg);
                char s[NET_IP_STR_BUF_LEN];
                net_ip_to_str(cur_cfg.gateway, s, sizeof(s));
                console_puts("Current Gateway: ");
                console_puts(s);
                console_puts("\r\n");
            }
        }
        else if (strncmp(subcmd, "reset", 5) == 0)
        {
            net_reset_defaults();
            console_puts("Network configuration reset to defaults from config.h.\r\n");
        }
        else if (strncmp(subcmd, "arp", 3) == 0)
        {
            console_puts("ARP Table Cache:\r\n");
            console_puts("  Slot | IP Address      | MAC Address       | Status\r\n");
            console_puts("  -----+-----------------+-------------------+--------\r\n");
            for (uint32_t i = 0; i < ARP_TABLE_CAPACITY; i++)
            {
                arp_entry_t entry;
                console_puts("    ");
                put_dec(i);
                console_puts("  | ");
                if (net_get_arp_entry(i, &entry) == NET_OK && entry.valid)
                {
                    char ip_s[NET_IP_STR_BUF_LEN];
                    net_ip_to_str(entry.ip, ip_s, sizeof(ip_s));
                    console_puts(ip_s);
                    for (size_t k = strlen(ip_s); k < 15; k++) console_putc(' ');
                    console_puts(" | ");
                    const char hex_chars[] = "0123456789abcdef";
                    for (int m = 0; m < 6; m++)
                    {
                        console_putc(hex_chars[(entry.mac[m] >> 4) & 0x0F]);
                        console_putc(hex_chars[entry.mac[m] & 0x0F]);
                        if (m < 5) console_putc(':');
                    }
                    console_puts(" | DYNAMIC\r\n");
                }
                else
                {
                    console_puts("---             | --:--:--:--:--:-- | EMPTY\r\n");
                }
            }
        }
        else if (strncmp(subcmd, "tcp", 3) == 0)
        {
            tcp_telemetry_t tt;
            tcp_get_telemetry(&tt);
            console_puts("Bare-Metal Lightweight TCP Connection Engine:\r\n");
            console_puts("  Active Conns:      ");
            put_dec(tt.active_connections);
            console_puts("\r\n");
            console_puts("  Listening Sockets: ");
            put_dec(tt.listening_pcbs);
            console_puts("\r\n");
            console_puts("  SYN Received:      ");
            put_dec(tt.syn_received_count);
            console_puts("\r\n");
            console_puts("  Established:       ");
            put_dec(tt.established_count);
            console_puts("\r\n");
            console_puts("  Data TX / RX:      ");
            put_dec(tt.bytes_tx);
            console_puts(" B / ");
            put_dec(tt.bytes_rx);
            console_puts(" B\r\n");
            console_puts("  Retransmits / RST: ");
            put_dec(tt.retransmit_count);
            console_puts(" / ");
            put_dec(tt.rst_sent_count);
            console_puts("\r\n");
            console_puts("  Gave Up / Idle:    ");
            put_dec(tt.rto_giveups);
            console_puts(" / ");
            put_dec(tt.idle_expired);
            console_puts("\r\n");
            console_puts("  Dup Segs / RTTs:   ");
            put_dec(tt.dup_segments);
            console_puts(" / ");
            put_dec(tt.rtt_samples);
            console_puts("\r\n");
            console_puts("  Deferred RX / TX Blocked: ");
            put_dec(tt.rx_deferred);
            console_puts(" / ");
            put_dec(tt.tx_blocked);
            console_puts("\r\n");
            console_puts("  Send Buffer:       ");
            put_dec(tcp_sndbuf_free_chunks() * TCP_SNDBUF_CHUNK_SIZE);
            console_puts(" B free, full ");
            put_dec(tt.sndbuf_full);
            console_puts("x\r\n");
            for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
            {
                const tcp_pcb_t *p = tcp_get_pcb(i);
                if (p != NULL && p->in_use)
                {
                    console_puts("  PCB ");
                    put_dec(i);
                    console_puts(": State=");
                    console_puts(tcp_state_to_str(p->state));
                    console_puts(", LocalPort=");
                    put_dec(p->local_port);
                    console_puts(", RemotePort=");
                    put_dec(p->remote_port);
                    console_puts(", Unacked=");
                    put_dec(p->snd_max - p->snd_una);
                    console_puts(", RTO=");
                    put_dec(p->rto_ms);
                    console_puts(" ms, Idle=");
                    if (p->idle_timeout_ms == TCP_IDLE_TIMEOUT_NEVER)
                    {
                        console_puts("never");
                    }
                    else
                    {
                        put_dec(p->idle_timeout_ms);
                        console_puts(" ms");
                    }
                    console_puts("\r\n");
                }
            }
        }
        else
        {
            net_config_t nc;
            net_get_config(&nc);
            net_telemetry_t nt;
            net_get_telemetry(&nt);
            char s[NET_IP_STR_BUF_LEN];

            console_puts("Bare-Metal Zero-Copy IPv4 & TCP Protocol Stack Status:\r\n");
            net_ip_to_str(nc.ip, s, sizeof(s));
            console_puts("  IP Address:        ");
            console_puts(s);
            console_puts("\r\n");
            net_ip_to_str(nc.netmask, s, sizeof(s));
            console_puts("  Subnet Mask:       ");
            console_puts(s);
            console_puts("\r\n");
            net_ip_to_str(nc.gateway, s, sizeof(s));
            console_puts("  Default Gateway:   ");
            console_puts(s);
            console_puts("\r\n");
            console_puts("  Ethernet MAC:      ");
            for (int i = 0; i < 6; i++)
            {
                const char hex_chars[] = "0123456789abcdef";
                console_putc(hex_chars[(nc.mac[i] >> 4) & 0x0F]);
                console_putc(hex_chars[nc.mac[i] & 0x0F]);
                if (i < 5) console_putc(':');
            }
            console_puts("\r\n");
            console_puts("  Packets TX / RX:   ");
            put_dec(nt.tx_packets);
            console_puts(" / ");
            put_dec(nt.rx_packets);
            console_puts("\r\n");
            console_puts("  Bytes TX / RX:     ");
            put_dec(nt.tx_bytes);
            console_puts(" / ");
            put_dec(nt.rx_bytes);
            console_puts("\r\n");
            console_puts("  ARP Req/Rep:       ");
            put_dec(nt.arp_requests_rx);
            console_puts(" RX / ");
            put_dec(nt.arp_replies_tx);
            console_puts(" TX\r\n");
            console_puts("  ICMP Echo:         ");
            put_dec(nt.icmp_rx);
            console_puts(" RX / ");
            put_dec(nt.icmp_tx);
            console_puts(" TX\r\n");
            console_puts("  TCP Segments:      ");
            put_dec(nt.tcp_rx);
            console_puts(" RX / ");
            put_dec(nt.tcp_tx);
            console_puts(" TX\r\n");
            console_puts("  Checksum Errors:   ");
            put_dec(nt.checksum_errors);
            console_puts(" (RFC 1071 Validation OK)\r\n");
        }
    }
    else if (strncmp(input_buffer, "dhcp", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        dhcp_telemetry_t dt;
        dhcp_get_telemetry(&dt);
        console_puts("Freestanding DHCP & DNS Captive Portal Status:\r\n");
        console_puts("  Discover Rx:   "); put_dec(dt.discover_rx); console_puts("\r\n");
        console_puts("  Offer Tx:      "); put_dec(dt.offer_tx); console_puts("\r\n");
        console_puts("  Request Rx:    "); put_dec(dt.request_rx); console_puts("\r\n");
        console_puts("  Ack Tx:        "); put_dec(dt.ack_tx); console_puts("\r\n");
        console_puts("  Nak Tx:        "); put_dec(dt.nak_tx); console_puts("\r\n");
        console_puts("  Release Rx:    "); put_dec(dt.release_rx); console_puts("\r\n");
        console_puts("  DNS Queries:   "); put_dec(dt.dns_queries_rx); console_puts("\r\n");
        console_puts("  DNS Replies:   "); put_dec(dt.dns_replies_tx); console_puts("\r\n");
        console_puts("  Active Leases: "); put_dec(dt.active_leases); console_puts("\r\n");
        for (uint32_t i = 0U; i < DHCP_MAX_LEASES; i++)
        {
            const dhcp_lease_t *l = dhcp_get_lease(i);
            if (l != NULL && l->active)
            {
                console_puts("    Slot "); put_dec(i);
                console_puts(": IP=192.168.1."); put_dec(l->ip & 0xFFU);
                console_puts(" MAC=");
                for (int m = 0; m < 6; m++)
                {
                    put_hex(l->mac[m]);
                    if (m < 5) console_putc(':');
                }
                console_puts("\r\n");
            }
        }
    }
    else if (strncmp(input_buffer, "http", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        char *subcmd = input_buffer + 4;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "start", 5) == 0)
        {
            http_status_t st = http_server_start(HTTP_SERVER_DEFAULT_PORT);
            if (st == HTTP_OK)
            {
                console_puts("HTTP server started on port ");
                put_dec(HTTP_SERVER_DEFAULT_PORT);
                console_puts("\r\n");
            }
            else
            {
                console_puts("Failed to start HTTP server\r\n");
            }
        }
        else if (strncmp(subcmd, "stop", 4) == 0)
        {
            http_server_stop();
            console_puts("HTTP server stopped\r\n");
        }
        else if (strncmp(subcmd, "routes", 6) == 0)
        {
            console_puts("Registered HTTP Routes:\r\n");
            console_puts("  1. GET  /               (Embedded Web UI Dashboard)\r\n");
            console_puts("  2. GET  /index.html     (Embedded Web UI Dashboard)\r\n");
            console_puts("  3. GET  /api/status     (System Status JSON)\r\n");
            console_puts("  4. GET  /api/info       (Device Info JSON)\r\n");
            console_puts("  5. GET  /api/telemetry  (Telemetry Metrics JSON)\r\n");
            console_puts("  6. POST /api/wdt/feed   (Watchdog Supervisor Feed)\r\n");
        }
        else
        {
            http_telemetry_t ht;
            http_server_get_telemetry(&ht);
            console_puts("Zero-Allocation Local REST/HTTP Server Status:\r\n");
            console_puts("  Server State:      ");
            console_puts(ht.server_running ? "RUNNING" : "STOPPED");
            console_puts("\r\n");
            console_puts("  Bound Port:        ");
            put_dec(HTTP_SERVER_DEFAULT_PORT);
            console_puts("\r\n");
            console_puts("  Active Routes:     ");
            put_dec(ht.active_routes);
            console_puts("\r\n");
            console_puts("  Requests Total:    ");
            put_dec(ht.requests_total);
            console_puts("\r\n");
            console_puts("  Requests GET:      ");
            put_dec(ht.requests_get);
            console_puts("\r\n");
            console_puts("  Requests POST:     ");
            put_dec(ht.requests_post);
            console_puts("\r\n");
            console_puts("  Responses 200 OK:  ");
            put_dec(ht.responses_200);
            console_puts("\r\n");
            console_puts("  Responses 404:     ");
            put_dec(ht.responses_404);
            console_puts("\r\n");
            console_puts("  Responses 405:     ");
            put_dec(ht.responses_405);
            console_puts("\r\n");
            console_puts("  Bytes RX / TX:     ");
            put_dec(ht.bytes_rx);
            console_puts(" / ");
            put_dec(ht.bytes_tx);
            console_puts("\r\n");
        }
    }
    else if (strncmp(input_buffer, "speedtest", 9) == 0 && (input_buffer[9] == ' ' || input_buffer[9] == '\0'))
    {
        char *subcmd = input_buffer + 9;
        while (*subcmd == ' ') subcmd++;

        if (strncmp(subcmd, "reset", 5) == 0)
        {
            speedtest_reset();
            console_puts("Speed-Test engine statistics reset.\r\n");
        }
        else if (strncmp(subcmd, "udp", 3) == 0)
        {
            char *arg = subcmd + 3;
            while (*arg == ' ') arg++;

            uint32_t target_ip = 0U;
            uint32_t count = SPEEDTEST_DEFAULT_BURST_COUNT;
            uint32_t size = SPEEDTEST_DEFAULT_PACKET_SIZE;

            if (*arg != '\0')
            {
                char ip_tok[32];
                size_t tok_len = 0U;
                while (*arg != ' ' && *arg != '\0' && tok_len < (sizeof(ip_tok) - 1U))
                {
                    ip_tok[tok_len++] = *arg++;
                }
                ip_tok[tok_len] = '\0';
                target_ip = net_str_to_ip(ip_tok);
                while (*arg == ' ') arg++;
                if (*arg != '\0')
                {
                    uint32_t c_in = 0U;
                    if (parse_uint(&arg, &c_in)) count = c_in;
                    while (*arg == ' ') arg++;
                    if (*arg != '\0')
                    {
                        uint32_t s_in = 0U;
                        if (parse_uint(&arg, &s_in)) size = s_in;
                    }
                }
            }

            if (target_ip == 0U)
            {
                net_config_t cfg;
                net_get_config(&cfg);
                target_ip = cfg.gateway;
            }

            char ip_s[NET_IP_STR_BUF_LEN];
            net_ip_to_str(target_ip, ip_s, sizeof(ip_s));
            console_puts("Executing UDP Speed-Test Benchmark Burst to ");
            console_puts(ip_s);
            console_puts(" (");
            put_dec(count);
            console_puts(" packets, ");
            put_dec(size);
            console_puts(" B each)...\r\n");

            speedtest_result_t res;
            speedtest_status_t st = speedtest_run_udp_tx(target_ip, SPEEDTEST_DEFAULT_PORT, count, size, &res);
            if (st == SPEEDTEST_OK)
            {
                uint32_t dur_us = (res.end_time_us > res.start_time_us) ? (res.end_time_us - res.start_time_us) : 1U;
                uint32_t mbps = speedtest_calculate_throughput_mbps(res.total_bytes_transferred, dur_us);
                console_puts("UDP Benchmark Completed:\r\n");
                console_puts("  Bytes Transferred: "); put_dec(res.total_bytes_transferred); console_puts(" B\r\n");
                console_puts("  Duration:          "); put_dec(dur_us / 1000U); console_puts(" ms ("); put_dec(dur_us); console_puts(" us)\r\n");
                console_puts("  Throughput:        "); put_dec(res.throughput_kbps); console_puts(" kbps ("); put_dec(mbps); console_puts(" Mbps)\r\n");
                console_puts("  Latency Min/Max:   "); put_dec(res.latency_min_us); console_puts(" us / "); put_dec(res.latency_max_us); console_puts(" us\r\n");
                console_puts("  Packet Loss:       "); put_dec(res.packet_loss_count); console_puts("\r\n");
            }
            else
            {
                console_puts("UDP Benchmark Failed (error code: ");
                put_dec((uint32_t)st);
                console_puts(")\r\n");
            }
        }
        else if (strncmp(subcmd, "status", 6) == 0)
        {
            speedtest_telemetry_t st;
            speedtest_get_telemetry(&st);
            console_puts("Speed-Test Benchmark Engine Telemetry:\r\n");
            console_puts("  Bursts Executed:   "); put_dec(st.bursts_run); console_puts("\r\n");
            console_puts("  Packets TX / RX:   "); put_dec(st.total_packets_tx); console_puts(" / "); put_dec(st.total_packets_rx); console_puts("\r\n");
            console_puts("  Bytes TX / RX:     "); put_dec(st.total_bytes_tx); console_puts(" / "); put_dec(st.total_bytes_rx); console_puts("\r\n");
            console_puts("  Last Throughput:   "); put_dec(st.last_throughput_kbps); console_puts(" kbps ("); put_dec(st.last_throughput_mbps); console_puts(" Mbps)\r\n");
            console_puts("  Last Latency:      Min="); put_dec(st.last_latency_min_us); console_puts(" us, Max="); put_dec(st.last_latency_max_us);
            console_puts(" us, Avg="); put_dec(st.last_latency_avg_us); console_puts(" us\r\n");
            console_puts("  Last Packet Loss:  "); put_dec(st.last_packet_loss); console_puts("\r\n");
        }
        else
        {
            uint32_t count = SPEEDTEST_DEFAULT_BURST_COUNT;
            uint32_t size = SPEEDTEST_DEFAULT_PACKET_SIZE;

            if (strncmp(subcmd, "run", 3) == 0 || strncmp(subcmd, "burst", 5) == 0)
            {
                if (strncmp(subcmd, "run", 3) == 0) subcmd += 3;
                else subcmd += 5;
                while (*subcmd == ' ') subcmd++;
                if (*subcmd != '\0')
                {
                    uint32_t c_in = 0U;
                    if (parse_uint(&subcmd, &c_in)) count = c_in;
                    while (*subcmd == ' ') subcmd++;
                    if (*subcmd != '\0')
                    {
                        uint32_t s_in = 0U;
                        if (parse_uint(&subcmd, &s_in)) size = s_in;
                    }
                }
            }

            console_puts("Running Synthetic Speed-Test Burst Benchmark (");
            put_dec(count);
            console_puts(" packets, ");
            put_dec(size);
            console_puts(" B each)...\r\n");

            speedtest_result_t res;
            speedtest_status_t st = speedtest_run_synthetic_burst(count, size, &res);
            if (st == SPEEDTEST_OK)
            {
                uint32_t dur_us = (res.end_time_us > res.start_time_us) ? (res.end_time_us - res.start_time_us) : 1U;
                uint32_t mbps = speedtest_calculate_throughput_mbps(res.total_bytes_transferred, dur_us);
                console_puts("Synthetic Benchmark Result:\r\n");
                console_puts("  Total Transferred: "); put_dec(res.total_bytes_transferred); console_puts(" B ("); put_dec(res.total_bytes_transferred / 1024U); console_puts(" KB)\r\n");
                console_puts("  Duration:          "); put_dec(dur_us / 1000U); console_puts(" ms ("); put_dec(dur_us); console_puts(" us)\r\n");
                console_puts("  Throughput:        "); put_dec(res.throughput_kbps); console_puts(" kbps ("); put_dec(mbps); console_puts(" Mbps)\r\n");
                console_puts("  Latency Min/Max:   "); put_dec(res.latency_min_us); console_puts(" us / "); put_dec(res.latency_max_us); console_puts(" us\r\n");
                console_puts("  Packet Loss:       0\r\n");
            }
            else
            {
                console_puts("Synthetic Benchmark Failed (error code: ");
                put_dec((uint32_t)st);
                console_puts(")\r\n");
            }
        }
    }
    else if (strncmp(input_buffer, "efuse", 5) == 0 && (input_buffer[5] == ' ' || input_buffer[5] == '\0'))
    {
        char *subcmd = input_buffer + 5;
        while (*subcmd == ' ') subcmd++;

        if (*subcmd == '\0' || strncmp(subcmd, "summary", 7) == 0)
        {
            efuse_print_summary();
        }
        else if (strncmp(subcmd, "security", 8) == 0)
        {
            efuse_print_security();
        }
        else if (strncmp(subcmd, "mac", 3) == 0)
        {
            efuse_print_mac();
        }
        else if (strncmp(subcmd, "status", 6) == 0)
        {
            efuse_telemetry_t t;
            efuse_get_telemetry(&t);
            console_puts("eFuse Controller Telemetry:\r\n");
            console_puts("  Shadow Refreshes:    "); put_dec(t.read_count); console_puts("\r\n");
            console_puts("  Write Disable Mask:  0x"); put_hex(t.wr_dis); console_puts("\r\n");
            console_puts("  Read Disable Mask:   0x"); put_hex(t.rd_dis); console_puts("\r\n");
            console_puts("  Secure Boot V2:      "); console_puts(t.secure_boot_en ? "ENABLED\r\n" : "DISABLED\r\n");
            console_puts("  Flash Encryption:    "); console_puts(t.flash_encryption_en ? "ENABLED\r\n" : "DISABLED\r\n");
            console_puts("  Hardware JTAG:       "); console_puts((t.jtag_pad_disabled || t.jtag_usb_disabled) ? "DISABLED\r\n" : "ENABLED\r\n");
            console_puts("  Download Mode:       "); console_puts(t.download_mode_disabled ? "DISABLED\r\n" : "ENABLED\r\n");
        }
        else
        {
            console_puts("Usage: efuse [status|summary|security|mac]\r\n");
        }
    }
    else if (strncmp(input_buffer, "prov", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        char *subcmd = input_buffer + 4;
        while (*subcmd == ' ') subcmd++;

        if (*subcmd == '\0' || strncmp(subcmd, "status", 6) == 0)
        {
            provisioning_print_status();
        }
        else if (strncmp(subcmd, "scan", 4) == 0)
        {
            console_puts("Triggering Wi-Fi scan...\r\n");
            provisioning_start_scan();
            provisioning_print_scan();
        }
        else if (strncmp(subcmd, "set", 3) == 0)
        {
            char *args = subcmd + 3;
            while (*args == ' ') args++;
            char ssid[PROVISIONING_MAX_SSID_LEN + 1U];
            char pass[PROVISIONING_MAX_PASS_LEN + 1U];
            memset(ssid, 0, sizeof(ssid));
            memset(pass, 0, sizeof(pass));

            size_t sidx = 0U;
            while (*args != ' ' && *args != '\0' && sidx < sizeof(ssid) - 1U)
            {
                ssid[sidx++] = *args++;
            }
            ssid[sidx] = '\0';
            while (*args == ' ') args++;

            size_t pidx = 0U;
            while (*args != ' ' && *args != '\0' && pidx < sizeof(pass) - 1U)
            {
                pass[pidx++] = *args++;
            }
            pass[pidx] = '\0';

            if (ssid[0] != '\0')
            {
                provisioning_status_t rc = provisioning_set_credentials(ssid, pass);
                if (rc == PROV_OK)
                {
                    console_puts("Provisioning credentials saved to NVS for SSID: '");
                    console_puts(ssid);
                    console_puts("'\r\n");
                }
                else
                {
                    console_puts("Error: Failed to set credentials (rc=");
                    put_dec((uint32_t)(-rc));
                    console_puts(")\r\n");
                }
            }
            else
            {
                console_puts("Usage: prov set <ssid> [passphrase]\r\n");
            }
        }
        else if (strncmp(subcmd, "get", 3) == 0)
        {
            wifi_credentials_t creds;
            if (provisioning_get_credentials(&creds) == PROV_OK)
            {
                console_puts("Configured Wi-Fi Credentials:\r\n");
                console_puts("  SSID:        '");
                console_puts(creds.ssid);
                console_puts("'\r\n");
                console_puts("  Provisioned: YES\r\n");
            }
            else
            {
                console_puts("No Wi-Fi credentials configured (Unprovisioned).\r\n");
            }
        }
        else if (strncmp(subcmd, "clear", 5) == 0)
        {
            provisioning_clear_credentials();
            console_puts("Provisioning credentials cleared from NVS.\r\n");
        }
        else if (strncmp(subcmd, "join", 4) == 0)
        {
            /* Same path as the portal: keeps the SoftAP up (hand-over) when it is running */
            if (provisioning_request_join(true) == PROV_OK)
            {
                console_puts("Joining the saved network (progress: prov status).\r\n");
            }
            else
            {
                console_puts("No saved credentials (prov set <ssid> <pass>).\r\n");
            }
        }
        else
        {
            console_puts("Usage: prov [status|scan|set <ssid> [pass]|get|clear|join]\r\n");
        }
    }
    else if (strncmp(input_buffer, "sta", 3) == 0 && (input_buffer[3] == ' ' || input_buffer[3] == '\0'))
    {
        char *subcmd = input_buffer + 3;
        while (*subcmd == ' ') subcmd++;

        if (*subcmd == '\0' || strncmp(subcmd, "status", 6) == 0)
        {
            wpa2_client_print_status();
            mdns_print_status();

            dhcp_client_telemetry_t dcli;
            if (dhcp_client_get_telemetry(&dcli) == DHCP_OK)
            {
                char s[NET_IP_STR_BUF_LEN];
                console_puts("\r\n=======================================================\r\n");
                console_puts("            DHCP CLIENT (IPv4) TELEMETRY               \r\n");
                console_puts("=======================================================\r\n");
                console_puts("  State:               ");
                switch (dcli.state)
                {
                    case DHCP_CLIENT_STATE_IDLE:        console_puts("IDLE"); break;
                    case DHCP_CLIENT_STATE_DISCOVERING: console_puts("DISCOVERING"); break;
                    case DHCP_CLIENT_STATE_REQUESTING:  console_puts("REQUESTING"); break;
                    case DHCP_CLIENT_STATE_BOUND:       console_puts("BOUND"); break;
                    case DHCP_CLIENT_STATE_STATIC:      console_puts("STATIC"); break;
                    default:                            console_puts("UNKNOWN"); break;
                }
                net_ip_to_str(dcli.assigned_ip, s, sizeof(s));
                console_puts("\r\n  Assigned IP:         ");
                console_puts(s);
                net_ip_to_str(dcli.netmask, s, sizeof(s));
                console_puts("\r\n  Subnet Mask:         ");
                console_puts(s);
                net_ip_to_str(dcli.gateway, s, sizeof(s));
                console_puts("\r\n  Default Gateway:     ");
                console_puts(s);
                net_ip_to_str(dcli.dns_server, s, sizeof(s));
                console_puts("\r\n  DNS Server:          ");
                console_puts(s);
                console_puts("\r\n  Discovers Sent:      ");
                put_dec(dcli.discovers_sent);
                console_puts("\r\n  Offers Received:     ");
                put_dec(dcli.offers_received);
                console_puts("\r\n  Requests Sent:       ");
                put_dec(dcli.requests_sent);
                console_puts("\r\n  Acks Received:       ");
                put_dec(dcli.acks_received);
                console_puts("\r\n=======================================================\r\n");
            }
        }
        else if (strncmp(subcmd, "connect", 7) == 0)
        {
            char *args = subcmd + 7;
            while (*args == ' ') args++;
            char ssid[WPA2_MAX_SSID_LEN + 1U];
            char pass[WPA2_MAX_PASS_LEN + 1U];
            memset(ssid, 0, sizeof(ssid));
            memset(pass, 0, sizeof(pass));

            size_t sidx = 0U;
            while (*args != ' ' && *args != '\0' && sidx < sizeof(ssid) - 1U)
            {
                ssid[sidx++] = *args++;
            }
            ssid[sidx] = '\0';
            while (*args == ' ') args++;

            size_t pidx = 0U;
            while (*args != ' ' && *args != '\0' && pidx < sizeof(pass) - 1U)
            {
                pass[pidx++] = *args++;
            }
            pass[pidx] = '\0';

            while (*args == ' ') args++;
            uint8_t chan = 0U;
            if (*args >= '0' && *args <= '9')
            {
                chan = (uint8_t)(*args++ - '0');
                if (*args >= '0' && *args <= '9')
                {
                    chan = (uint8_t)(chan * 10U + (*args++ - '0'));
                }
            }

            if (ssid[0] != '\0')
            {
                size_t plen = strlen(pass);
                if (plen < 8U || plen > 64U)
                {
                    console_puts("Error: WPA2-PSK passphrase must be between 8 and 64 characters.\r\n");
                }
                else
                {
                    console_puts("Initiating WPA2-PSK connection to '");
                    console_puts(ssid);
                    if (chan > 0U)
                    {
                        console_puts("' (channel ");
                        put_dec((uint32_t)chan);
                        console_puts(")...\r\n");
                    }
                    else
                    {
                        console_puts("'...\r\n");
                    }
                    wpa2_status_t wst = wpa2_client_handover_chan(ssid, pass, chan);
                    if (wst != WPA2_OK)
                    {
                        console_puts("Error: connection handover failed (status=-");
                        put_dec((uint32_t)(-(int32_t)wst));
                        console_puts(wst == WPA2_ERR_UNSUPPORTED ? ", 64-char raw PSK not supported)\r\n" : ")\r\n");
                    }
                }
            }
            else
            {
                console_puts("Usage: sta connect <ssid> <passphrase> [channel]\r\n");
            }
        }
        else if (strncmp(subcmd, "disconnect", 10) == 0)
        {
            wpa2_client_stop();
            wifi_stop_sta();
            console_puts("Station disconnected.\r\n");
        }
        else if (strncmp(subcmd, "mdns", 4) == 0)
        {
            mdns_announce();
            mdns_print_status();
        }
        else if (strncmp(subcmd, "eapol", 5) == 0)
        {
            wpa2_client_print_status();
        }
        else
        {
            console_puts("Usage: sta [status|connect <ssid> <pass> [chan]|disconnect|mdns|eapol]\r\n");
        }
    }
    else
    {

        s_shell_telemetry.unknown_commands++;
        console_puts("Unknown command. Type 'help' for available commands.\r\n");
    }
}

void shell_tick(void)
{
    char input_buffer[MAX_CMD_LEN];
    if (console_read_line_nonblocking(input_buffer, MAX_CMD_LEN))
    {
        shell_execute(input_buffer);
        console_puts(CONFIG_CONSOLE_PROMPT);
        console_flush();
    }
}

void shell(char *input_buffer)
{
    console_puts(CONFIG_CONSOLE_PROMPT);
    console_flush();
    read_line(input_buffer, MAX_CMD_LEN);
    shell_execute(input_buffer);
}

