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
#include "shell.h"
#include "efuse.h"
#include "soak.h"
#include "ota.h"
#include "nvs.h"
#include "provisioning.h"
#include "wpa2_client.h"
#include "mdns.h"
#include "button.h"

void main(void)
{
    /* Initialize PCR clock tree to 160 MHz CPU PLL and 40 MHz APB */
    clock_init();

    /* Initialize Flash Cache & MSPI MMU (Maps Flash XIP 0x42000000) */
    mmu_init();

    /* Initialize eFuse Controller & Hardware Security Seals */
    efuse_init();

    /* Initialize high-resolution 64-bit hardware system timer (SYSTIMER 16 MHz) */
    systimer_init();

    /* Initialize active multi-tier watchdog supervisor */
    wdt_init(WDT_DEFAULT_TIMEOUT_MS);

    /* Initialize RISC-V machine-mode trap vector table and handler */
    trap_init();

    /* Initialize Interrupt Matrix (INTMTX) and Core Interrupt Controller (PLIC_MX) */
    interrupt_init();

    /* Initialize Lock-Free SPSC DPC Queue Engine */
    dpc_init();

    /* Initialize Unified Dual-Console Layer (UART0 interrupt-driven & USB CDC-ACM) */
    console_init();

    /* Initialize Deterministic Static Arena Memory Allocator */
    arena_init();

    /* Initialize Hardware Periodic Timer (TIMG0 Timer 0 @ 10s period) */
    timer_init(TIMER_DEFAULT_INTERVAL_SEC);

    /* Initialize Cooperative Coroutine Scheduler */
    task_init();

    /* Initialize 24/7 Stability Soak, Memory Leak & Anti-Starvation Subsystem */
    soak_init();

    /* Initialize Dual-Slot Flash OTA Firmware Upgrade & Rollback Subsystem */
    ota_init();

    /* Initialize Non-Volatile Storage (NVS) & Golden Master Seal Subsystem */
    nvs_init();

    /* Initialize RISC-V Physical Memory Protection (PMP) & APM Fault Isolation */
    pmp_init();
    apm_init();

    /* Initialize Low-Power (LP) RISC-V Coprocessor Subsystem */
    lp_core_init();

    /* Initialize Power Management & Retained Shared Mailbox Subsystem */
    power_init();

    /* Initialize GPIO Matrix & IO_MUX Multi-Function Pin Subsystem */
    gpio_init();

    /* Initialize GDMA Multi-Channel Engine & Circular Buffer Descriptor Rings */
    gdma_init();

    /* Initialize Modem Clock & Power Control (MODEM_SYSCON / MODEM_LPCON) */
    modem_init();

    /* Initialize 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring */
    wifi_init();

    /* Initialize IEEE 802.15.4 Radio Transceiver Driver */
    ieee802154_init();

    /* Initialize Bare-Metal Zero-Copy IPv4, ARP & ICMP Network Stack */
    net_init();

    /* Initialize Lightweight Bare-Metal TCP State Machine */
    tcp_init();

    /* Initialize Zero-Allocation Local REST/HTTP Server Engine */
    http_server_init();
    http_server_start(HTTP_SERVER_DEFAULT_PORT);

    /* Initialize Freestanding DHCP Server & DNS Captive Portal */
    dhcp_init();

    /* Initialize SoftAP Captive Portal Wi-Fi Provisioning Engine */
    provisioning_init();

    /* Initialize Bare-Metal WPA2-PSK Station Client & Multicast DNS Responder */
    wpa2_client_init();
    mdns_init();

    /* Initialize LAN Network Diagnostics & Wi-Fi Speed-Test Benchmark Engine */
    speedtest_init();

    /* Initialize Interactive Console Shell & 24/7 Health Telemetry */
    shell_init();

    /* Setup button (BOOT, GPIO9): long press opens the setup SoftAP (REV-15) */
    button_setup_init();

    /* Saved credentials: join as a station; none: open the setup SoftAP */
    provisioning_boot();

    console_puts("\r\n");
    shell_print_info();

    console_puts("Ready. Type 'do-test' for validation suite or 'help' for command list.\r\n");
    console_puts(CONFIG_CONSOLE_PROMPT);
    console_flush();

    while (1)
    {
        uint64_t now_us = systimer_get_us();
        wdt_supervisor_tick();
        dpc_process_all();
        tcp_tick();
        dhcp_client_tick();
        if (button_setup_poll(now_us) == BUTTON_EVENT_LONG_PRESS)
        {
            provisioning_on_setup_button(now_us);
        }
        provisioning_tick(now_us);
        mdns_tick(now_us);
        wifi_os_adapter_poll();
        wifi_poll_rx_traffic();
        shell_tick();
        task_yield();
    }
}
