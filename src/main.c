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
#include "ble.h"
#include "ble_gatt.h"
#include "ble_npl.h"
#include "wifi.h"
#include "wifi_os_adapter.h"
#include "ieee802154.h"
#include "net.h"
#include "tcp.h"
#include "http_server.h"
#include "dhcp.h"
#include "wifi_vendor_types.h"
#include "speedtest.h"
#include "matter.h"
#include "shell.h"
#include "efuse.h"
#include "soak.h"
#include "ota.h"
#include "nvs.h"

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

    /* Initialize Bluetooth 5 (LE) Controller Driver & Minimal GATT Server */
    ble_init();

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

    /* Initialize LAN Network Diagnostics & Wi-Fi Speed-Test Benchmark Engine */
    speedtest_init();

    /* Initialize Google Home Matter Commissioning Bridge & Hardware Crypto */
    matter_init(NULL);

    /* Initialize Interactive Console Shell & 24/7 Health Telemetry */
    shell_init();

#if CONFIG_BLE_AUTO_START_ADV
    ble_gap_start_advertising();
#endif

#if CONFIG_WIFI_AUTO_START_AP
    wifi_start_ap(CONFIG_WIFI_SSID, NULL, CONFIG_WIFI_CHANNEL);
#endif

    console_puts("\r\n");
    shell_print_info();

    console_puts("Ready. Type 'do-test' for validation suite or 'help' for command list.\r\n");
    console_puts(CONFIG_CONSOLE_PROMPT);
    console_flush();

    while (1)
    {
        wdt_supervisor_tick();
        dpc_process_all();
        tcp_tick();
        wifi_os_adapter_poll();
        wifi_poll_rx_traffic();
        ble_npl_service_background();
        shell_tick();
        task_yield();
    }
}
