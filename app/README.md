# Iron V Dedicated Companion Application

The **Iron V Companion Application** is a lightweight, zero-external-dependency Progressive Web Application (PWA) and responsive Single-Page Application (SPA) designed to monitor and manage the Iron V bare-metal ESP32-C6 operating system across mobile (iOS/Android), desktop (Chrome, Firefox, Safari, Edge), and embedded contexts.

---

## Architectural Features

1. **Subnet Auto-Discovery & Zero-Conf Probe:**
   - Automatically probes `http://iron-v.local` via mDNS on startup.
   - Falls back gracefully to `http://192.168.4.1` (SoftAP provisioning gateway) or home LAN IP (`192.168.1.x`).
   - Dynamic round-trip ping time (RTT) tracking with live status indicators.

2. **Real-Time System Health Telemetry Dashboard:**
   - **System Uptime:** Real-time uptime calculation from hardware timers.
   - **CPU Clock Frequency:** Active operating frequency (160 MHz).
   - **Deterministic Memory Arenas:** Live visualization of allocated bytes versus unallocated capacity across the 512 KB pool with zero-heap-leak validation.
   - **Watchdog Supervisor:** Live monitoring of MWDT0 & LP_WDT feeds and interactive REST feed button.
   - **Deferred Procedure Calls (DPC):** Real-time drop counter tracking lock-free SPSC event queues.

3. **Interactive Hardware GPIO & Relay Controls:**
   - Direct control over onboard GPIO pins (Status LED on Pin 15, Relay 1 on Pin 2, Relay 2 on Pin 3, Pin 8).
   - Instant state toggle switches, level readouts, and 500ms pulse trigger.
   - Arbitrary pin dispatcher for GPIO pins 0 through 30 via `POST /api/gpio`.

4. **Wi-Fi SoftAP Provisioning Engine:**
   - Dispatches scan requests (`GET /api/wifi/scan`) populating available 2.4 GHz home networks with signal strength (RSSI dBm with 4-bar visual indicators), channel, and security type.
   - Dispatches home Wi-Fi credentials (`POST /api/wifi/configure`) and monitors state handover.

5. **LAN Network Diagnostics & Speed-Test Benchmark:**
   - Triggers synthetic 100-packet throughput bursts via `POST /api/speedtest?burst=1`.
   - Displays real-time bandwidth gauge (Mbps and kbps), microsecond latency, and packet loss counters.
   - Logs previous test runs in a local history table.

6. **"Works with Google Home" Matter Commissioning Bridge:**
   - Formatted 11-digit manual pairing code (`3497-011-2332`) with one-click copy.
   - Base38 setup payload (`MT:Y.K9042C00KA0648G00`) with one-click copy.
   - Embedded zero-dependency SVG vector QR code generator for direct scanning in the Google Home / Matter commissioning app.
   - Matter On/Off Cluster (0x0006) toggle synchronized with GPIO 15.

---

## Running the Companion App

### Method 1: Direct Browser Launch
Open `app/index.html` directly in any modern web browser:
```bash
# Linux / macOS
google-chrome app/index.html
# or
xdg-open app/index.html
```

### Method 2: Local HTTP Server
Run a zero-configuration Python web server from the project directory:
```bash
python3 -m http.server 8080 --directory app
```
Navigate to `http://localhost:8080`.

### Method 3: Direct from Iron V Board
When connected to the board via Wi-Fi (`iron-v.local` or `192.168.4.1`), all REST endpoints serve CORS headers (`Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: GET, POST, OPTIONS`), allowing the app to run seamlessly from any local origin.

---

## REST API Specification Reference

| Endpoint | Method | Description | Sample Response |
| :--- | :--- | :--- | :--- |
| `/api/status` | `GET` | System uptime & CPU clock | `{"status":"online","hostname":"iron-v","uptime_ms":123450,"cpu_mhz":160}` |
| `/api/health` | `GET` | 24/7 kernel health metrics | `{"status":"healthy","uptime_seconds":123,"arena_bytes_used":0,"arena_bytes_free":524288,"dpc_queue_drops":0,"wdt_feeds_total":42}` |
| `/api/wdt/feed` | `POST` | Feed hardware watchdog supervisor | `{"status":"ok","fed":true,"message":"watchdog fed"}` |
| `/api/gpio` | `GET` | Query state of onboard GPIO pins | `{"pins":[{"pin":15,"level":0,"name":"Status LED"}, ...]}` |
| `/api/gpio` | `POST` | Set or toggle GPIO output level | `{"status":"ok","pin":15,"level":1}` |
| `/api/wifi/scan` | `GET` | Scan available Wi-Fi access points | `{"count":4,"aps":[{"ssid":"HomeNetwork","rssi":-45,"channel":1,"auth":"WPA2-PSK"}]}` |
| `/api/wifi/configure` | `POST` | Provision credentials for home LAN | `{"status":"ok","provisioned":true,"message":"Connecting"}` |
| `/api/speedtest` | `GET` / `POST` | Query or trigger throughput burst | `{"status":"ok","throughput_kbps":83600,"throughput_mbps":83,"latency_min_us":120}` |
| `/api/matter/payload` | `GET` | Matter commissioning setup codes | `{"manual_code":"3497-011-2332","qr_payload":"MT:Y.K9042C00KA0648G00"}` |
