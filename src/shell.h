/*
 * src/shell.h
 *
 * ESP32-C6 Extended Interactive Console Shell & 24/7 Health Monitoring
 *
 * Implements centralized shell command dispatch, real-time 24/7 health
 * telemetry aggregation, dual-console line buffering, and diagnostic reporting.
 */

#ifndef IRON_V_SHELL_H
#define IRON_V_SHELL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========================================================================= */
/* System Health Telemetry Data Structure (Roadmap Task 6.4 Specification)   */
/* ========================================================================= */
typedef struct {
    uint32_t uptime_seconds;     /* System uptime in seconds (from WDT/SYSTIMER) */
    uint32_t arena_bytes_used;   /* Active memory allocated in static arenas */
    uint32_t arena_bytes_free;   /* Unallocated capacity remaining in static arenas */
    uint32_t dpc_queue_drops;    /* Cumulative dropped deferred procedure calls */
    uint32_t wdt_feeds_total;    /* Total watchdog feeds delivered */
    uint32_t usb_active;         /* USB-Serial-JTAG CDC-ACM console active flag */
    uint32_t uart_active;        /* UART0 hardware console active flag */
    uint32_t wifi_packets_rx;    /* Total received Wi-Fi network packets */
    uint32_t wifi_packets_tx;    /* Total transmitted Wi-Fi network packets */
} system_health_telemetry_t;

/* ========================================================================= */
/* Interactive Shell Telemetry Structure                                     */
/* ========================================================================= */
typedef struct {
    uint32_t commands_processed;        /* Total valid commands executed */
    uint32_t unknown_commands;          /* Unrecognized command attempts */
    uint32_t empty_commands;            /* Empty enter presses */
    uint64_t last_command_timestamp_us; /* SYSTIMER timestamp of last command */
} shell_telemetry_t;

/* ========================================================================= */
/* Public Shell & Health Monitoring API                                      */
/* ========================================================================= */

/**
 * Initialize shell subsystem and telemetry tracking.
 */
/* Console commands of modules (REV-33). A module registers its commands with SHELL_COMMAND_DEFINE();
 * the shell looks the first word of a line up here after its built-in commands, and `help` lists
 * every entry with a help line. Section name: a C identifier (see module.h). */
#define SHELL_COMMAND_SECTION            "iron_shell_cmds"

typedef struct {
    const char *name;                    /* first word of the command line */
    const char *help;                    /* `help` line after the indent; NULL = alias, not listed */
    void (*run)(char *args);             /* rest of the line, leading spaces skipped ("" if none) */
} shell_command_t;

#define SHELL_COMMAND_DEFINE(ident, ...) \
    static const shell_command_t ident __attribute__((used, section(SHELL_COMMAND_SECTION), aligned(sizeof(void *)))) = __VA_ARGS__

uint32_t shell_command_count(void);
const shell_command_t *shell_command_at(uint32_t index);
const shell_command_t *shell_command_find(const char *name, size_t name_len);

/* Decimal or 0x-hex number after optional spaces; advances *str. 1 = parsed. */
int shell_parse_uint(char **str, uint32_t *out);

void shell_init(void);

/**
 * Non-blocking shell tick function called from main super-loop.
 */
void shell_tick(void);

/**
 * Execute a single shell command line.
 *
 * @param input_buffer Null-terminated command string.
 */
void shell_execute(char *input_buffer);

/**
 * Query current 24/7 system health telemetry across all kernel subsystems.
 *
 * @param out_telem Pointer to destination telemetry structure.
 */
void shell_get_health_telemetry(system_health_telemetry_t *out_telem);

/**
 * Query interactive shell execution telemetry.
 *
 * @param out_telem Pointer to destination telemetry structure.
 */
void shell_get_telemetry(shell_telemetry_t *out_telem);

/**
 * Reset shell execution statistics.
 */
void shell_reset_telemetry(void);

/**
 * Print formatted 24/7 system health monitoring report to active console.
 */
void shell_print_health(void);

/**
 * Print real-time system performance (top) report to active console.
 */
void shell_print_top(void);

/**
 * Print shell help and available command list.
 */
void shell_print_help(void);

/**
 * Print comprehensive system and hardware information banner.
 */
void shell_print_info(void);

/**
 * Get current system uptime in seconds.
 *
 * @return Uptime in seconds.
 */
uint32_t shell_get_uptime_seconds(void);

#endif /* IRON_V_SHELL_H */
