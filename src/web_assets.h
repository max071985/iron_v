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
#include "section.h"
#include <stddef.h>

/* Dashboard: refreshed by the button only, no timer (REV-15: no traffic the user did not ask
 * for). The whole response must fit one TCP segment (host test, O-44). */
static const char g_index_html[] FLASH_RODATA_ATTR =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Iron V</title>"
"<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:15px}"
"h1{color:#38bdf8;font-size:18px}.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;margin:10px 0}"
".c{background:#1e293b;padding:8px;border-radius:4px}.c b{display:block;font-size:11px;color:#94a3b8;text-transform:uppercase}"
"button{background:#38bdf8;border:0;padding:6px 12px;border-radius:4px;font-weight:bold}</style></head>"
"<body><h1>Iron V</h1><div class=\"g\">"
"<div class=\"c\"><b>CPU</b><span id=\"c\">--</span></div>"
"<div class=\"c\"><b>Uptime</b><span id=\"u\">--</span></div></div>"
"<button onclick=\"f()\">Refresh</button> <a href=\"/setup\" style=\"color:#38bdf8\">Wi-Fi setup</a>"
"<script>function f(){fetch('/api/status').then(r=>r.json()).then(d=>{"
"if(d.uptime_ms)document.getElementById('u').innerText=Math.floor(d.uptime_ms/1000)+'s';"
"if(d.cpu_mhz)document.getElementById('c').innerText=d.cpu_mhz+' MHz';"
"}).catch(e=>{});}f();</script></body></html>";

/* Wi-Fi setup portal (REV-29): real scan, save, then polls /api/wifi/status for the join result.
 * Must fit HTTP_BODY_MAX_LEN (checked by a host test). */
static const char g_setup_html[] FLASH_RODATA_ATTR =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Iron V Wi-Fi Setup</title>"
"<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:20px;max-width:380px;margin:auto}"
"h1{color:#38bdf8;font-size:18px}"
"label{display:block;font-size:11px;color:#94a3b8;margin:10px 0 4px;text-transform:uppercase}"
"input,select{width:100%;box-sizing:border-box;background:#1e293b;border:1px solid #334155;color:#f8fafc;padding:8px;border-radius:4px;font-size:14px}"
"button{background:#38bdf8;color:#0f172a;border:0;width:100%;padding:10px;border-radius:4px;font-weight:bold;margin-top:12px;font-size:14px}"
"button.s{background:#334155;color:#f8fafc;margin-top:6px}button:disabled{opacity:.5}"
"#m{margin-top:12px;padding:10px;border-radius:4px;font-size:13px;display:none;line-height:1.4}"
".i,.k,.e{display:block!important}.i{background:#1e3a5f}.k{background:#065f46}.e{background:#7f1d1d}</style></head>"
"<body><h1>Iron V Wi-Fi Setup</h1>"
"<label>Network</label><select id=\"s\"><option value=\"\">Scanning...</option></select>"
"<button class=\"s\" id=\"r\" onclick=\"sc(1)\">Rescan</button>"
"<label>Password</label><input id=\"p\" type=\"password\">"
"<button id=\"b\" onclick=\"sv()\">Save &amp; Connect</button><div id=\"m\"></div>"
"<script>"
"var $=function(i){return document.getElementById(i)},n='',t;"
"function msg(c,x){var m=$('m');m.className=c;m.textContent=x}"
"function sc(f){var s=$('s');$('r').disabled=1;if(f)s.innerHTML='<option value=\"\">Scanning...</option>';"
"fetch('/api/wifi/scan'+(f?'?refresh=1':'')).then(function(r){return r.json()}).then(function(d){"
"if(d.scanning){setTimeout(function(){sc(0)},2500);return}s.innerHTML='';"
"(d.aps||[]).forEach(function(a){var o=new Option(a.ssid+' ('+a.rssi+' dBm)'+(a.supported?'':' - not supported'),a.ssid);o.disabled=!a.supported;s.add(o)});"
"if(!s.options.length)s.add(new Option('No networks found',''));$('r').disabled=0}).catch(function(){setTimeout(function(){sc(0)},2500)})}"
"function sv(){n=$('s').value;if(!n){msg('e','Pick a network');return}$('b').disabled=1;msg('i','Saving...');"
"fetch('/api/wifi/configure',{method:'POST',body:JSON.stringify({ssid:n,password:$('p').value})}).then(function(r){return r.json()})"
".then(function(d){if(d.status=='ok'){msg('i','Connecting to '+n+'...');t=setTimeout(po,1500)}else{msg('e',d.error||'Save failed');$('b').disabled=0}})"
".catch(function(){msg('i','Connecting to '+n+'...');t=setTimeout(po,3000)})}"
"function po(){fetch('/api/wifi/status').then(function(r){return r.json()}).then(function(d){var j=d.join||{};"
"if(j.state=='connected'){msg('k','Connected to '+n+'. This setup network closes in '+j.handover_delay_s+' s. Then join '+n+' with this phone and open http://'+d.hostname);return}"
"if(j.state=='failed'){msg('e','Could not connect to '+n+': '+j.message+'.');$('b').disabled=0;return}"
"if(j.state=='idle'&&!d.provisioned){msg('e','Not saved, please try again');$('b').disabled=0;return}"
"t=setTimeout(po,1500)}).catch(function(){t=setTimeout(po,3000)})}"
"sc(0)</script></body></html>";

#endif /* IRON_V_WEB_ASSETS_H */

