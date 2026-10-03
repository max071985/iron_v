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

static const char g_setup_html[] HTTP_FLASH_RODATA =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Iron V Wi-Fi Setup</title>"
"<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:20px;max-width:380px;margin:auto}"
"h1{color:#38bdf8;font-size:18px;margin-bottom:12px}"
"label{display:block;font-size:11px;color:#94a3b8;margin:10px 0 4px;text-transform:uppercase}"
"input,select{width:100%;box-sizing:border-box;background:#1e293b;border:1px solid #334155;color:#f8fafc;padding:8px;border-radius:4px;font-size:13px}"
"button{background:#38bdf8;color:#0f172a;border:0;width:100%;padding:10px;border-radius:4px;font-weight:bold;cursor:pointer;margin-top:12px;font-size:13px}"
"button.sec{background:#334155;color:#f8fafc;margin-top:6px}"
"#m{margin-top:10px;padding:8px;border-radius:4px;font-size:12px;display:none}"
".ok{background:#065f46;color:#34d399;display:block!important}"
".er{background:#7f1d1d;color:#f87171;display:block!important}</style></head>"
"<body><h1>Iron V Wi-Fi Setup</h1>"
"<label>Wi-Fi Network (SSID)</label>"
"<select id=\"s\"><option value=\"\">-- Scanning networks... --</option></select>"
"<button class=\"sec\" type=\"button\" onclick=\"sc()\">Rescan Networks</button>"
"<label>Password</label>"
"<input id=\"p\" type=\"password\" placeholder=\"Enter Wi-Fi password\">"
"<button type=\"button\" onclick=\"sv()\">Save &amp; Connect</button>"
"<div id=\"m\"></div>"
"<script>"
"function sc(){fetch('/api/wifi/scan').then(r=>r.json()).then(d=>{"
"let s=document.getElementById('s');s.innerHTML='';"
"if(d.aps&&d.aps.length){d.aps.forEach(a=>{let o=document.createElement('option');o.value=a.ssid;o.text=a.ssid+' ('+a.rssi+' dBm)';s.add(o);});}"
"else{let o=document.createElement('option');o.text='No networks found';s.add(o);}"
"}).catch(e=>{});}"
"function sv(){let s=document.getElementById('s').value,p=document.getElementById('p').value,m=document.getElementById('m');"
"if(!s){m.className='er';m.innerText='Please select or enter an SSID';return;}"
"fetch('/api/wifi/configure',{method:'POST',body:JSON.stringify({ssid:s,password:p})})"
".then(r=>r.json()).then(d=>{"
"if(d.status==='ok'||d.provisioned){m.className='ok';m.innerText='Saved! Connecting to '+s+'...';}"
"else{m.className='er';m.innerText=d.error||'Configuration failed';}"
"}).catch(e=>{m.className='er';m.innerText='Network request failed';});}"
"sc();</script></body></html>";

#endif /* IRON_V_WEB_ASSETS_H */

