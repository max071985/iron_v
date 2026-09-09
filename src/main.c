#include <stdint.h>
#include "io_constants.h"
#include "utils.h"
#include "string.h"
#include "test.h"
#include "clock.h"
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

static void print_help(void)
{
    console_puts("Iron V Shell Commands:\r\n");
    console_puts("  help                - Show available commands\r\n");
    console_puts("  info                - Show system information\r\n");
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
    console_puts("  do-test             - Run full baseline validation test suite\r\n");
}

static void print_info(void)
{
    clock_config_t clk;
    clock_get_config(&clk);

    console_puts("========================================\r\n");
    console_puts(" Iron V Bare-Metal RISC-V Runtime\r\n");
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
    console_puts("========================================\r\n");
}

static void shell_execute(char *input_buffer)
{
    if (input_buffer[0] == '\0') return;

    if (strcmp(input_buffer, "help") == 0)
    {
        print_help();
    }
    else if (strcmp(input_buffer, "info") == 0)
    {
        print_info();
    }
    else if (strcmp(input_buffer, "do-test") == 0 || strcmp(input_buffer, "test") == 0)
    {
        run_validation_suite();
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
                uint32_t val = *(volatile uint32_t *)addr;
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
                *(volatile uint32_t *)addr = val;
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
        asm volatile("ecall");
        console_puts("Successfully resumed from ECALL trap! Total ECALLs: ");
        put_dec(trap_get_ecall_count());
        console_puts("\r\n");
    }
    else if (strcmp(input_buffer, "panic") == 0)
    {
        console_puts("Triggering illegal instruction (0x00000000) to demonstrate panic dump...\r\n");
        asm volatile(".word 0x00000000");
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
    else
    {
        console_puts("Unknown command. Type 'help' for available commands.\r\n");
    }
}

void shell_tick(void)
{
    char input_buffer[MAX_CMD_LEN];
    if (console_read_line_nonblocking(input_buffer, MAX_CMD_LEN))
    {
        shell_execute(input_buffer);
        console_puts("iron_v> ");
        console_flush();
    }
}

void shell(char *input_buffer)
{
    console_puts("iron_v> ");
    console_flush();
    read_line(input_buffer, MAX_CMD_LEN);
    shell_execute(input_buffer);
}

void main(void)
{
    /* Initialize PCR clock tree to 160 MHz CPU PLL and 40 MHz APB */
    clock_init();

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

    /* Initialize RISC-V Physical Memory Protection (PMP) & APM Fault Isolation */
    pmp_init();
    apm_init();

    /* Initialize Low-Power (LP) RISC-V Coprocessor Subsystem */
    lp_core_init();

    console_puts("\r\n");
    print_info();

    console_puts("Ready. Type 'do-test' for validation suite or 'help' for command list.\r\n");
    console_puts("iron_v> ");
    console_flush();

    while (1)
    {
        wdt_supervisor_tick();
        dpc_process_all();
        shell_tick();
        task_yield();
    }
}
