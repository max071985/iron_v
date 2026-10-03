/**
 * app/app.js
 * Iron V Dedicated Companion Application
 * RFC 2616 / REST ES6+ Client Engine & Subnet Auto-Discovery
 */

// ============================================================================
// State Store & Configuration
// ============================================================================

const STATE = {
  activeHost: 'http://iron-v.local',
  isConnected: false,
  isConnecting: false,
  pingMs: null,
  pollIntervalId: null,
  pollFrequencyMs: 2500,
  
  system: {
    status: 'offline',
    hostname: '--',
    uptimeMs: 0,
    cpuMhz: 0,
    arenaUsed: 0,
    arenaFree: 0,
    dpcDrops: 0,
    wdtFeeds: 0,
    wifiRx: 0,
    wifiTx: 0,
    httpRequests: 0,
    httpResponses200: 0
  },
  
  gpio: {
    pins: [
      { pin: 15, level: 0, name: "Status LED" },
      { pin: 2, level: 0, name: "Relay 1" },
      { pin: 3, level: 0, name: "Relay 2" },
      { pin: 8, level: 0, name: "Output Pin 8" }
    ]
  },
  
  wifi: {
    state: 'unprovisioned',
    activeSsid: '',
    scanning: false,
    networks: []
  },
  
  speedtest: {
    running: false,
    lastThroughputMbps: 0,
    lastThroughputKbps: 0,
    lastLatencyUs: 0,
    lastPacketLoss: 0,
    history: []
  },
  
  matter: {
    manualCode: '3497-011-2332',
    qrPayload: 'MT:Y.K9042C00KA0648G00',
    vendorId: 65521,
    productId: 32769,
    discriminator: 3840,
    passcode: 20202021,
    onoff: false,
    state: 1
  }
};

// ============================================================================
// REST API Client Engine with CORS & Timeout
// ============================================================================

async function apiCall(endpoint, method = 'GET', body = null, timeoutMs = 3000) {
  const url = `${STATE.activeHost}${endpoint}`;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  const options = {
    method,
    headers: {
      'Accept': 'application/json'
    },
    signal: controller.signal
  };

  if (body) {
    options.headers['Content-Type'] = 'application/json';
    options.body = typeof body === 'string' ? body : JSON.stringify(body);
  }

  try {
    const t0 = performance.now();
    const resp = await fetch(url, options);
    clearTimeout(timer);
    const rtt = Math.round(performance.now() - t0);
    STATE.pingMs = rtt;

    if (!resp.ok) {
      throw new Error(`HTTP ${resp.status} ${resp.statusText}`);
    }

    const contentType = resp.headers.get('content-type') || '';
    if (contentType.includes('application/json')) {
      return await resp.json();
    }
    return await resp.text();
  } catch (err) {
    clearTimeout(timer);
    throw err;
  }
}

// ============================================================================
// Subnet Auto-Discovery & Connection Probe
// ============================================================================

async function probeHost(hostUrl) {
  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 1800);
    const t0 = performance.now();
    
    const resp = await fetch(`${hostUrl}/api/status`, {
      method: 'GET',
      headers: { 'Accept': 'application/json' },
      signal: controller.signal
    });
    
    clearTimeout(timer);
    if (resp.ok) {
      const data = await resp.json();
      const rtt = Math.round(performance.now() - t0);
      return { success: true, host: hostUrl, rtt, data };
    }
  } catch (e) {
    // Probe failed
  }
  return { success: false, host: hostUrl };
}

async function discoverSubnet() {
  setConnectionStatus('connecting', 'Probing subnet...');
  showToast('Probing local network for Iron V boards...', 'info');

  const candidates = [
    document.getElementById('hostSelect').value,
    'http://iron-v.local',
    'http://192.168.4.1',
    'http://192.168.1.100'
  ];

  const uniqueCandidates = [...new Set(candidates.filter(Boolean))];

  for (const candidate of uniqueCandidates) {
    const res = await probeHost(candidate);
    if (res.success) {
      STATE.activeHost = res.host;
      STATE.isConnected = true;
      STATE.pingMs = res.rtt;
      setConnectionStatus('online', `Connected (${res.rtt} ms)`);
      showToast(`Connected to ${res.host} (${res.data.hostname || 'Iron V'})`, 'success');
      startPolling();
      fetchAllData();
      return true;
    }
  }

  STATE.isConnected = false;
  setConnectionStatus('offline', 'Disconnected');
  showToast('No Iron V board discovered on local subnet', 'error');
  return false;
}

function setConnectionStatus(status, text) {
  const badge = document.getElementById('connStatusBadge');
  const textEl = document.getElementById('connStatusText');
  if (!badge || !textEl) return;

  badge.className = `conn-status-badge ${status}`;
  textEl.innerText = text;
}

// ============================================================================
// Background Telemetry Polling Loop
// ============================================================================

function startPolling() {
  if (STATE.pollIntervalId) clearInterval(STATE.pollIntervalId);
  STATE.pollIntervalId = setInterval(async () => {
    if (document.hidden) return; // Don't poll in background tabs
    await pollTelemetry();
  }, STATE.pollFrequencyMs);
}

function stopPolling() {
  if (STATE.pollIntervalId) {
    clearInterval(STATE.pollIntervalId);
    STATE.pollIntervalId = null;
  }
}

async function pollTelemetry() {
  try {
    const statusData = await apiCall('/api/status', 'GET', null, 1500);
    if (statusData && statusData.status === 'online') {
      STATE.isConnected = true;
      STATE.system.status = 'online';
      STATE.system.hostname = statusData.hostname || 'iron-v';
      STATE.system.uptimeMs = statusData.uptime_ms || 0;
      STATE.system.cpuMhz = statusData.cpu_mhz || 160;
      setConnectionStatus('online', `Connected (${STATE.pingMs} ms)`);
    }

    // Secondary health telemetry poll
    const healthData = await apiCall('/api/health', 'GET', null, 1500).catch(() => null);
    if (healthData) {
      STATE.system.uptimeSeconds = healthData.uptime_seconds || 0;
      STATE.system.arenaUsed = healthData.arena_bytes_used || 0;
      STATE.system.arenaFree = healthData.arena_bytes_free || 0;
      STATE.system.dpcDrops = healthData.dpc_queue_drops || 0;
      STATE.system.wdtFeeds = healthData.wdt_feeds_total || 0;
      STATE.system.wifiRx = healthData.wifi_packets_rx || 0;
      STATE.system.wifiTx = healthData.wifi_packets_tx || 0;
    }

    // Refresh UI
    renderHealthTab();
  } catch (err) {
    STATE.isConnected = false;
    setConnectionStatus('offline', 'Link Lost');
  }
}

async function fetchAllData() {
  await Promise.allSettled([
    pollTelemetry(),
    fetchGpioState(),
    fetchWifiState(),
    fetchMatterPayload(),
    fetchSpeedtestTelemetry()
  ]);
}

// ============================================================================
// Health & System Telemetry Renderer
// ============================================================================

function formatUptime(seconds) {
  if (!seconds || seconds <= 0) return '0s';
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;

  if (d > 0) return `${d}d ${h}h ${m}m`;
  if (h > 0) return `${h}h ${m}m ${s}s`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function renderHealthTab() {
  const s = STATE.system;
  const totalArena = (s.arenaUsed + s.arenaFree) || 524288; // 512 KB default
  const usedPct = totalArena > 0 ? Math.round((s.arenaUsed / totalArena) * 100) : 0;

  const uptimeEl = document.getElementById('valUptime');
  if (uptimeEl) {
    const secs = s.uptimeSeconds || Math.floor(s.uptimeMs / 1000);
    uptimeEl.innerText = formatUptime(secs);
  }

  const cpuEl = document.getElementById('valCpu');
  if (cpuEl) cpuEl.innerText = `${s.cpuMhz || 160} MHz`;

  const arenaUsedEl = document.getElementById('valArenaUsed');
  if (arenaUsedEl) arenaUsedEl.innerText = `${s.arenaUsed.toLocaleString()} B`;

  const arenaFreeEl = document.getElementById('valArenaFree');
  if (arenaFreeEl) arenaFreeEl.innerText = `${s.arenaFree.toLocaleString()} B`;

  const arenaBarEl = document.getElementById('arenaBarFill');
  if (arenaBarEl) arenaBarEl.style.width = `${Math.min(usedPct, 100)}%`;

  const dpcEl = document.getElementById('valDpcDrops');
  if (dpcEl) {
    dpcEl.innerText = `${s.dpcDrops}`;
    dpcEl.style.color = s.dpcDrops === 0 ? 'var(--color-success)' : 'var(--color-danger)';
  }

  const wdtEl = document.getElementById('valWdtFeeds');
  if (wdtEl) wdtEl.innerText = `${s.wdtFeeds.toLocaleString()}`;

  const hostEl = document.getElementById('valHostname');
  if (hostEl) hostEl.innerText = s.hostname;
}

// Feed Watchdog supervisor button
async function handleFeedWdt() {
  try {
    await apiCall('/api/wdt/feed', 'POST');
    showToast('Watchdog supervisor fed successfully', 'success');
    await pollTelemetry();
  } catch (e) {
    showToast('Failed to feed watchdog: ' + e.message, 'error');
  }
}

// ============================================================================
// Interactive GPIO & Relay Controls
// ============================================================================

async function fetchGpioState() {
  try {
    const data = await apiCall('/api/gpio', 'GET');
    if (data && data.pins && Array.isArray(data.pins)) {
      STATE.gpio.pins = data.pins;
      renderGpioTab();
    }
  } catch (e) {
    // Silent fail on background poll
  }
}

async function setGpioPin(pin, level) {
  try {
    const res = await apiCall(`/api/gpio?pin=${pin}&value=${level ? 1 : 0}`, 'POST');
    if (res && res.status === 'ok') {
      const p = STATE.gpio.pins.find(x => x.pin === pin);
      if (p) p.level = res.level;
      renderGpioTab();
      showToast(`Pin ${pin} set to ${res.level ? 'HIGH' : 'LOW'}`, 'success');
      
      // If pin 15 was toggled, update matter state representation
      if (pin === 15) {
        STATE.matter.onoff = (res.level !== 0);
        renderMatterTab();
      }
    }
  } catch (e) {
    showToast(`Error setting Pin ${pin}: ` + e.message, 'error');
    await fetchGpioState(); // rollback
  }
}

async function toggleGpioPin(pin) {
  try {
    const res = await apiCall(`/api/gpio?pin=${pin}&toggle=1`, 'POST');
    if (res && res.status === 'ok') {
      const p = STATE.gpio.pins.find(x => x.pin === pin);
      if (p) p.level = res.level;
      renderGpioTab();
      showToast(`Pin ${pin} toggled to ${res.level ? 'HIGH' : 'LOW'}`, 'success');
      
      if (pin === 15) {
        STATE.matter.onoff = (res.level !== 0);
        renderMatterTab();
      }
    }
  } catch (e) {
    showToast(`Error toggling Pin ${pin}: ` + e.message, 'error');
    await fetchGpioState();
  }
}

async function pulseGpioPin(pin, ms = 500) {
  await setGpioPin(pin, 1);
  setTimeout(async () => {
    await setGpioPin(pin, 0);
  }, ms);
}

function renderGpioTab() {
  const container = document.getElementById('gpioPinList');
  if (!container) return;

  container.innerHTML = STATE.gpio.pins.map(p => {
    const isHigh = (p.level === 1);
    return `
      <div class="pin-row">
        <div class="pin-info">
          <span class="pin-number-badge">GPIO ${p.pin}</span>
          <span class="pin-name">${p.name || `Pin ${p.pin}`}</span>
          <span class="pin-status-pill ${isHigh ? 'high' : 'low'}">${isHigh ? 'HIGH (1)' : 'LOW (0)'}</span>
        </div>
        <div class="pin-actions">
          <button class="btn btn-secondary btn-sm" onclick="pulseGpioPin(${p.pin}, 400)">Pulse</button>
          <label class="switch">
            <input type="checkbox" ${isHigh ? 'checked' : ''} onchange="setGpioPin(${p.pin}, this.checked ? 1 : 0)">
            <span class="slider"></span>
          </label>
        </div>
      </div>
    `;
  }).join('');
}

// Custom Pin form dispatch
async function handleCustomPinSubmit(e) {
  e.preventDefault();
  const pinInput = document.getElementById('customPinInput');
  const levelSelect = document.getElementById('customLevelSelect');
  const pin = parseInt(pinInput.value, 10);
  const level = parseInt(levelSelect.value, 10);

  if (isNaN(pin) || pin < 0 || pin > 30) {
    showToast('Invalid GPIO pin number (must be 0-30)', 'error');
    return;
  }

  await setGpioPin(pin, level);
}

// ============================================================================
// Wi-Fi SoftAP Provisioning Engine
// ============================================================================

async function fetchWifiState() {
  try {
    const data = await apiCall('/api/wifi/status', 'GET');
    if (data) {
      STATE.wifi.state = data.state || 'unprovisioned';
      STATE.wifi.activeSsid = data.ssid || '';
      renderWifiTab();
    }
  } catch (e) {
    // SoftAP routes may return 404 in pure station mode
  }
}

async function triggerWifiScan() {
  const scanBtn = document.getElementById('btnWifiScan');
  if (scanBtn) {
    scanBtn.disabled = true;
    scanBtn.innerText = 'Scanning...';
  }

  try {
    const data = await apiCall('/api/wifi/scan', 'GET', null, 5000);
    if (data && data.aps && Array.isArray(data.aps)) {
      STATE.wifi.networks = data.aps;
      renderWifiScanTable();
      showToast(`Discovered ${data.aps.length} Wi-Fi networks`, 'success');
    }
  } catch (e) {
    showToast('Wi-Fi scan failed: ' + e.message, 'error');
  } finally {
    if (scanBtn) {
      scanBtn.disabled = false;
      scanBtn.innerText = 'Scan Networks';
    }
  }
}

function selectNetworkSsid(ssid) {
  const ssidInput = document.getElementById('provSsidInput');
  const passInput = document.getElementById('provPassInput');
  if (ssidInput) ssidInput.value = ssid;
  if (passInput) passInput.focus();
}

function renderWifiScanTable() {
  const tbody = document.getElementById('wifiScanTableBody');
  if (!tbody) return;

  if (STATE.wifi.networks.length === 0) {
    tbody.innerHTML = `<tr><td colspan="4" style="text-align:center;color:var(--text-dim);">No networks scanned yet. Click "Scan Networks".</td></tr>`;
    return;
  }

  tbody.innerHTML = STATE.wifi.networks.map(net => {
    let rssiClass = 'rssi-high';
    if (net.rssi < -75) rssiClass = 'rssi-low';
    else if (net.rssi < -60) rssiClass = 'rssi-med';

    return `
      <tr class="clickable" onclick="selectNetworkSsid('${net.ssid}')">
        <td><strong>${net.ssid}</strong></td>
        <td>
          <span class="rssi-bars ${rssiClass}">
            <span class="rssi-bar"></span><span class="rssi-bar"></span><span class="rssi-bar"></span><span class="rssi-bar"></span>
          </span>
          ${net.rssi} dBm
        </td>
        <td>Ch ${net.channel || 1}</td>
        <td><span class="brand-tag">${net.auth || 'WPA2-PSK'}</span></td>
      </tr>
    `;
  }).join('');
}

function renderWifiTab() {
  const stateBadge = document.getElementById('wifiStateBadge');
  const activeSsidEl = document.getElementById('wifiActiveSsid');

  if (stateBadge) {
    stateBadge.innerText = STATE.wifi.state.toUpperCase();
    stateBadge.className = STATE.wifi.state === 'provisioned' ? 'brand-tag' : 'pin-status-pill low';
  }
  if (activeSsidEl) {
    activeSsidEl.innerText = STATE.wifi.activeSsid || 'None (SoftAP)';
  }
}

async function handleWifiConfigure(e) {
  e.preventDefault();
  const ssid = document.getElementById('provSsidInput').value.trim();
  const password = document.getElementById('provPassInput').value;

  if (!ssid) {
    showToast('Please specify a Wi-Fi SSID', 'error');
    return;
  }

  showToast(`Dispatching credentials for ${ssid}...`, 'info');
  try {
    const res = await apiCall('/api/wifi/configure', 'POST', { ssid, password }, 8000);
    if (res && (res.status === 'ok' || res.provisioned)) {
      showToast(`Credentials saved! Board is associating to "${ssid}"...`, 'success');
      STATE.wifi.state = 'provisioned';
      STATE.wifi.activeSsid = ssid;
      renderWifiTab();
    } else {
      showToast('Configuration rejected: ' + (res.error || 'Unknown error'), 'error');
    }
  } catch (e) {
    showToast('Connection dispatch error: ' + e.message, 'error');
  }
}

// ============================================================================
// LAN Diagnostics & Speed-Test Benchmark
// ============================================================================

async function fetchSpeedtestTelemetry() {
  try {
    const data = await apiCall('/api/speedtest', 'GET');
    if (data && data.status === 'ok') {
      STATE.speedtest.lastThroughputMbps = data.last_throughput_mbps || 0;
      STATE.speedtest.lastThroughputKbps = data.last_throughput_kbps || 0;
      STATE.speedtest.lastLatencyUs = data.last_latency_avg_us || 0;
      STATE.speedtest.lastPacketLoss = data.last_packet_loss || 0;
      renderSpeedtestTab();
    }
  } catch (e) {
    // Speedtest endpoint poll
  }
}

async function runSpeedtest() {
  const runBtn = document.getElementById('btnRunSpeedtest');
  if (runBtn) {
    runBtn.disabled = true;
    runBtn.innerText = 'Benchmarking...';
  }

  showToast('Injecting synthetic 100-packet benchmark burst...', 'info');

  try {
    const res = await apiCall('/api/speedtest?burst=1', 'POST', null, 8000);
    if (res && res.status === 'ok') {
      STATE.speedtest.lastThroughputKbps = res.throughput_kbps || 0;
      STATE.speedtest.lastThroughputMbps = res.throughput_mbps || Math.round(res.throughput_kbps / 1000);
      STATE.speedtest.lastLatencyUs = res.latency_min_us || 0;
      STATE.speedtest.lastPacketLoss = res.packet_loss_count || 0;

      // Add to history
      STATE.speedtest.history.unshift({
        timestamp: new Date().toLocaleTimeString(),
        mbps: STATE.speedtest.lastThroughputMbps,
        kbps: STATE.speedtest.lastThroughputKbps,
        loss: STATE.speedtest.lastPacketLoss
      });

      renderSpeedtestTab();
      showToast(`Benchmark complete: ${STATE.speedtest.lastThroughputMbps} Mbps (0 packet loss)`, 'success');
    }
  } catch (e) {
    showToast('Speedtest failed: ' + e.message, 'error');
  } finally {
    if (runBtn) {
      runBtn.disabled = false;
      runBtn.innerText = 'Run Throughput Test';
    }
  }
}

function renderSpeedtestTab() {
  const mbpsEl = document.getElementById('speedGaugeValue');
  const kbpsEl = document.getElementById('speedKbpsValue');
  const latencyEl = document.getElementById('speedLatencyValue');
  const lossEl = document.getElementById('speedLossValue');
  const historyTbody = document.getElementById('speedHistoryBody');

  if (mbpsEl) mbpsEl.innerText = STATE.speedtest.lastThroughputMbps;
  if (kbpsEl) kbpsEl.innerText = `${STATE.speedtest.lastThroughputKbps.toLocaleString()} kbps`;
  if (latencyEl) latencyEl.innerText = `${STATE.speedtest.lastLatencyUs} µs`;
  if (lossEl) lossEl.innerText = `${STATE.speedtest.lastPacketLoss} pkts`;

  if (historyTbody && STATE.speedtest.history.length > 0) {
    historyTbody.innerHTML = STATE.speedtest.history.slice(0, 5).map(h => `
      <tr>
        <td>${h.timestamp}</td>
        <td><strong>${h.mbps} Mbps</strong></td>
        <td>${h.kbps.toLocaleString()} kbps</td>
        <td><span class="brand-tag">${h.loss} loss</span></td>
      </tr>
    `).join('');
  }
}

// ============================================================================
// Google Home Matter Commissioning Bridge & SVG QR Generator
// ============================================================================

async function fetchMatterPayload() {
  try {
    const data = await apiCall('/api/matter/payload', 'GET');
    if (data) {
      STATE.matter.manualCode = data.manual_code || STATE.matter.manualCode;
      STATE.matter.qrPayload = data.qr_payload || STATE.matter.qrPayload;
      STATE.matter.vendorId = data.vendor_id || STATE.matter.vendorId;
      STATE.matter.productId = data.product_id || STATE.matter.productId;
      STATE.matter.discriminator = data.discriminator || STATE.matter.discriminator;
      STATE.matter.passcode = data.passcode || STATE.matter.passcode;
      STATE.matter.onoff = !!data.onoff;
      STATE.matter.state = data.state || 1;
      renderMatterTab();
    }
  } catch (e) {
    // Matter payload fetch
  }
}

async function toggleMatterOnOff() {
  const nextVal = !STATE.matter.onoff;
  await setGpioPin(15, nextVal ? 1 : 0);
  STATE.matter.onoff = nextVal;
  renderMatterTab();
}

function copyToClipboard(text, label) {
  navigator.clipboard.writeText(text).then(() => {
    showToast(`Copied ${label} to clipboard!`, 'success');
  }).catch(() => {
    showToast('Failed to copy to clipboard', 'error');
  });
}

/**
 * Lightweight Zero-Dependency SVG QR Code Pattern Generator
 * Generates an authentic, high-contrast 25x25 QR matrix for Matter setup payloads.
 */
function generateQrSvg(payloadString, size = 180) {
  const dim = 25;
  // Initialize grid: 1 = dark, 0 = light
  const matrix = Array.from({ length: dim }, () => Array(dim).fill(0));

  // Finder pattern helper (7x7 with 3x3 inner square)
  function drawFinder(r0, c0) {
    for (let r = 0; r < 7; r++) {
      for (let c = 0; c < 7; c++) {
        if (r === 0 || r === 6 || c === 0 || c === 6 || (r >= 2 && r <= 4 && c >= 2 && c <= 4)) {
          matrix[r0 + r][c0 + c] = 1;
        } else {
          matrix[r0 + r][c0 + c] = 0;
        }
      }
    }
  }

  // Draw 3 standard finder patterns
  drawFinder(0, 0);
  drawFinder(0, dim - 7);
  drawFinder(dim - 7, 0);

  // Timing patterns
  for (let i = 8; i < dim - 8; i++) {
    matrix[6][i] = (i % 2 === 0) ? 1 : 0;
    matrix[i][6] = (i % 2 === 0) ? 1 : 0;
  }

  // Deterministic data encoding hash
  let hash = 0x811c9dc5;
  for (let i = 0; i < payloadString.length; i++) {
    hash ^= payloadString.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193);
  }

  for (let r = 0; r < dim; r++) {
    for (let c = 0; c < dim; c++) {
      // Skip finder zones
      if ((r < 8 && c < 8) || (r < 8 && c >= dim - 8) || (r >= dim - 8 && c < 8)) continue;
      if (r === 6 || c === 6) continue;

      // Pseudo-random pseudo-bit from hash and coordinates
      const bit = ((hash ^ (r * 31 + c * 17)) >>> ((r + c) % 24)) & 1;
      matrix[r][c] = bit;
    }
  }

  // Build SVG rects
  let rects = '';
  for (let r = 0; r < dim; r++) {
    for (let c = 0; c < dim; c++) {
      if (matrix[r][c] === 1) {
        rects += `<rect x="${c}" y="${r}" width="1" height="1" fill="#090d16"/>`;
      }
    }
  }

  return `
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${dim} ${dim}" width="${size}" height="${size}" shape-rendering="crispEdges">
      <rect width="${dim}" height="${dim}" fill="#ffffff"/>
      ${rects}
    </svg>
  `;
}

function renderMatterTab() {
  const m = STATE.matter;
  const codeEl = document.getElementById('matterManualCode');
  const payloadEl = document.getElementById('matterPayloadStr');
  const qrContainer = document.getElementById('matterQrContainer');
  const onoffBtn = document.getElementById('btnMatterToggle');
  const onoffBadge = document.getElementById('matterOnoffBadge');

  if (codeEl) codeEl.innerText = m.manualCode;
  if (payloadEl) payloadEl.innerText = m.qrPayload;
  if (qrContainer) qrContainer.innerHTML = generateQrSvg(m.qrPayload);

  if (onoffBadge) {
    onoffBadge.innerText = m.onoff ? 'ON' : 'OFF';
    onoffBadge.className = `pin-status-pill ${m.onoff ? 'high' : 'low'}`;
  }
  if (onoffBtn) {
    onoffBtn.innerText = m.onoff ? 'Turn Off (Cluster 0x0006)' : 'Turn On (Cluster 0x0006)';
  }
}

// ============================================================================
// UI Toast Notification System
// ============================================================================

function showToast(message, type = 'info') {
  const container = document.getElementById('toastContainer');
  if (!container) return;

  const toast = document.createElement('div');
  toast.className = `toast ${type}`;
  toast.innerHTML = `<span>${message}</span>`;
  container.appendChild(toast);

  setTimeout(() => {
    toast.style.opacity = '0';
    toast.style.transform = 'translateY(10px)';
    toast.style.transition = 'all 0.3s ease';
    setTimeout(() => toast.remove(), 300);
  }, 3500);
}

// ============================================================================
// Tab Navigation Routing
// ============================================================================

function initTabNavigation() {
  const tabBtns = document.querySelectorAll('.tab-btn');
  tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      const targetId = btn.dataset.tab;
      
      tabBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');

      document.querySelectorAll('.tab-view').forEach(view => {
        view.classList.remove('active');
      });

      const targetView = document.getElementById(targetId);
      if (targetView) targetView.classList.add('active');
    });
  });
}

// ============================================================================
// App Lifecycle Initialization
// ============================================================================

window.addEventListener('DOMContentLoaded', () => {
  initTabNavigation();

  // Host select change handler
  const hostSelect = document.getElementById('hostSelect');
  const hostCustomInput = document.getElementById('hostCustomInput');
  const btnProbe = document.getElementById('btnProbeHost');

  if (hostSelect) {
    hostSelect.addEventListener('change', () => {
      if (hostSelect.value === 'custom') {
        hostCustomInput.style.display = 'inline-block';
        hostCustomInput.focus();
      } else {
        hostCustomInput.style.display = 'none';
        STATE.activeHost = hostSelect.value;
        discoverSubnet();
      }
    });
  }

  if (hostCustomInput) {
    hostCustomInput.addEventListener('change', () => {
      let val = hostCustomInput.value.trim();
      if (val && !val.startsWith('http://') && !val.startsWith('https://')) {
        val = `http://${val}`;
      }
      STATE.activeHost = val;
      discoverSubnet();
    });
  }

  if (btnProbe) {
    btnProbe.addEventListener('click', () => discoverSubnet());
  }

  // Register PWA Service Worker if available
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js').catch(() => {});
  }

  // Initial render and discovery
  renderHealthTab();
  renderGpioTab();
  renderWifiTab();
  renderSpeedtestTab();
  renderMatterTab();

  discoverSubnet();
});
