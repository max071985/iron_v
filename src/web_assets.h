/*
 * src/web_assets.h
 *
 * Static Embedded Web Assets Placed in Flash Read-Only Memory (.flash.rodata)
 *
 * Provides pre-formatted, self-contained HTML, CSS, and client-side JavaScript
 * for Iron V embedded HTTP server without filesystem or dynamic heap overhead.
 */

#ifndef IRON_V_WEB_ASSETS_H
#define IRON_V_WEB_ASSETS_H

#include <stdint.h>
#include <stddef.h>

#if defined(__riscv)
#define HTTP_FLASH_RODATA __attribute__((section(".flash.rodata")))
#else
#define HTTP_FLASH_RODATA
#endif

static const char g_index_html[] HTTP_FLASH_RODATA =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Iron V</title>"
"<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:15px}"
"h1{color:#38bdf8;font-size:18px}.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;margin:10px 0}"
".c{background:#1e293b;padding:8px;border-radius:4px}.c b{display:block;font-size:11px;color:#94a3b8;text-transform:uppercase}"
"button{background:#38bdf8;border:0;padding:6px 12px;border-radius:4px;font-weight:bold;cursor:pointer}</style></head>"
"<body><h1>Iron V Bare-Metal RISC-V</h1><div class=\"g\">"
"<div class=\"c\"><b>CPU</b><span id=\"c\">160 MHz</span></div>"
"<div class=\"c\"><b>Uptime</b><span id=\"u\">--</span></div>"
"<div class=\"c\"><b>Network</b><span>Port 80</span></div>"
"<div class=\"c\"><b>Wireless</b><span>Wi-Fi 6</span></div>"
"<div class=\"c\"><b>Arenas</b><span>Static</span></div>"
"<div class=\"c\"><b>WDT</b><span>5000 ms</span></div></div>"
"<button onclick=\"f()\">Refresh</button>"
"<script>function f(){fetch('/api/status').then(r=>r.json()).then(d=>{"
"if(d.uptime_ms)document.getElementById('u').innerText=Math.floor(d.uptime_ms/1000)+'s';"
"if(d.cpu_mhz)document.getElementById('c').innerText=d.cpu_mhz+' MHz';"
"}).catch(e=>{});}setInterval(f,5000);f();</script></body></html>";

#endif /* IRON_V_WEB_ASSETS_H */
