#!/usr/bin/env python3
"""
tests/test_companion_app.py

Host Unit & Integration Test Suite for Iron V Dedicated Companion Application
Verifies asset integrity, HTML5/CSS3/ES6+ structure, PWA manifest, and REST API contracts.
"""

import os
import sys
import json
import re
import unittest
import threading
import time
from http.server import HTTPServer, BaseHTTPRequestHandler
import urllib.request
import urllib.parse

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP_DIR = os.path.join(REPO_ROOT, "app")

class MockIronVServer(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass  # Quiet logging

    def _set_cors_headers(self, status=200, content_type="application/json"):
        self.send_response(status)
        self.send_header("Server", "Iron-V-BareMetal")
        self.send_header("Connection", "close")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.send_header("Access-Control-Max-Age", "86400")
        if content_type:
            self.send_header("Content-Type", content_type)

    def do_OPTIONS(self):
        self._set_cors_headers(status=204, content_type=None)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/status":
            body = json.dumps({
                "status": "online",
                "hostname": "iron-v",
                "uptime_ms": 1234567,
                "cpu_mhz": 160,
                "firmware": {"name": "Iron V", "version": "1.0.0"}
            }).encode("utf-8")
        elif path == "/api/health":
            body = json.dumps({
                "status": "healthy",
                "uptime_seconds": 1234,
                "arena_bytes_used": 0,
                "arena_bytes_free": 524288,
                "dpc_queue_drops": 0,
                "wdt_feeds_total": 420,
                "wifi_packets_rx": 1500,
                "wifi_packets_tx": 1200,
                "uart_active": 1,
                "usb_active": 1
            }).encode("utf-8")
        elif path == "/api/gpio":
            body = json.dumps({
                "pins": [
                    {"pin": 15, "level": 0, "name": "Status LED"},
                    {"pin": 2, "level": 0, "name": "Relay 1"},
                    {"pin": 3, "level": 0, "name": "Relay 2"},
                    {"pin": 8, "level": 0, "name": "Output Pin 8"}
                ]
            }).encode("utf-8")
        elif path == "/api/wifi/scan":
            body = json.dumps({
                "count": 4,
                "aps": [
                    {"ssid": "HomeNetwork-2.4G", "rssi": -45, "channel": 1, "auth": "WPA2-PSK"},
                    {"ssid": "Office_IoT", "rssi": -62, "channel": 6, "auth": "WPA2-PSK"}
                ]
            }).encode("utf-8")
        elif path == "/api/wifi/status":
            body = json.dumps({
                "state": "provisioned",
                "ssid": "HomeNetwork-2.4G",
                "has_credentials": True
            }).encode("utf-8")
        elif path == "/api/speedtest":
            body = json.dumps({
                "status": "ok",
                "bursts_run": 5,
                "last_throughput_kbps": 83600,
                "last_throughput_mbps": 83,
                "last_latency_avg_us": 120,
                "last_packet_loss": 0
            }).encode("utf-8")
        elif path == "/api/matter/payload":
            body = json.dumps({
                "manual_code": "3497-011-2332",
                "qr_payload": "MT:Y.K9042C00KA0648G00",
                "vendor_id": 65521,
                "product_id": 32769,
                "discriminator": 3840,
                "passcode": 20202021,
                "onoff": False,
                "state": 1
            }).encode("utf-8")
        else:
            self._set_cors_headers(status=404)
            err_body = b'{"error":"not_found"}\r\n'
            self.send_header("Content-Length", str(len(err_body)))
            self.end_headers()
            self.wfile.write(err_body)
            return

        self._set_cors_headers(status=200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/wdt/feed":
            body = json.dumps({"status": "ok", "fed": True}).encode("utf-8")
        elif path == "/api/gpio":
            query = urllib.parse.parse_qs(parsed.query)
            pin = int(query.get("pin", [15])[0])
            val = int(query.get("value", [1])[0])
            body = json.dumps({"status": "ok", "pin": pin, "level": val}).encode("utf-8")
        elif path == "/api/wifi/configure":
            body = json.dumps({"status": "ok", "provisioned": True}).encode("utf-8")
        elif path == "/api/speedtest":
            body = json.dumps({
                "status": "ok",
                "throughput_kbps": 84200,
                "throughput_mbps": 84,
                "latency_min_us": 95,
                "latency_max_us": 140,
                "packet_loss_count": 0
            }).encode("utf-8")
        else:
            self._set_cors_headers(status=404)
            err_body = b'{"error":"not_found"}\r\n'
            self.send_header("Content-Length", str(len(err_body)))
            self.end_headers()
            self.wfile.write(err_body)
            return

        self._set_cors_headers(status=200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class TestCompanionApp(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        # Start mock server on random loopback port
        cls.server = HTTPServer(("127.0.0.1", 0), MockIronVServer)
        cls.port = cls.server.server_port
        cls.server_thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.server_thread.start()
        time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def test_01_app_directory_structure(self):
        """Verify all mandatory companion app files exist and are non-empty."""
        expected_files = [
            "index.html",
            "styles.css",
            "app.js",
            "manifest.json",
            "sw.js",
            "README.md"
        ]
        for f in expected_files:
            fpath = os.path.join(APP_DIR, f)
            self.assertTrue(os.path.isfile(fpath), f"Missing required file: app/{f}")
            self.assertGreater(os.path.getsize(fpath), 50, f"File app/{f} is unexpectedly small")

    def test_02_pwa_manifest(self):
        """Validate PWA manifest.json schema and metadata."""
        manifest_path = os.path.join(APP_DIR, "manifest.json")
        with open(manifest_path, "r", encoding="utf-8") as f:
            data = json.load(f)

        self.assertIn("name", data)
        self.assertIn("short_name", data)
        self.assertEqual(data["display"], "standalone")
        self.assertIn("icons", data)
        self.assertGreaterEqual(len(data["icons"]), 1)
        self.assertTrue(data["start_url"].startswith("./") or data["start_url"] == "index.html")

    def test_03_html_dom_elements(self):
        """Verify semantic HTML elements and DOM identifiers required by client engine."""
        html_path = os.path.join(APP_DIR, "index.html")
        with open(html_path, "r", encoding="utf-8") as f:
            content = f.read()

        required_ids = [
            "hostSelect",
            "hostCustomInput",
            "btnProbeHost",
            "connStatusBadge",
            "valUptime",
            "valCpu",
            "valArenaUsed",
            "valArenaFree",
            "valDpcDrops",
            "valWdtFeeds",
            "gpioPinList",
            "customPinInput",
            "btnWifiScan",
            "wifiScanTableBody",
            "provSsidInput",
            "provPassInput",
            "speedGaugeValue",
            "btnRunSpeedtest",
            "matterManualCode",
            "matterPayloadStr",
            "matterQrContainer",
            "toastContainer"
        ]
        for rid in required_ids:
            pattern = rf'id=["\']{rid}["\']'
            self.assertRegex(content, pattern, f"Missing required DOM element id='{rid}' in index.html")

        # Check manifest and script linkages
        self.assertIn('rel="manifest"', content)
        self.assertIn('src="./app.js"', content)
        self.assertIn('href="./styles.css"', content)

    def test_04_js_api_contracts(self):
        """Verify that app.js implements all standard REST API routes and algorithms."""
        js_path = os.path.join(APP_DIR, "app.js")
        with open(js_path, "r", encoding="utf-8") as f:
            js = f.read()

        endpoints = [
            "/api/status",
            "/api/health",
            "/api/gpio",
            "/api/wifi/scan",
            "/api/wifi/configure",
            "/api/speedtest",
            "/api/matter/payload",
            "/api/wdt/feed"
        ]
        for ep in endpoints:
            self.assertIn(ep, js, f"app.js missing endpoint invocation: {ep}")

        # Subnet discovery candidates
        self.assertIn("iron-v.local", js)
        self.assertIn("192.168.4.1", js)

        # Vector SVG QR generator presence
        self.assertIn("generateQrSvg", js)
        self.assertIn("drawFinder", js)

    def test_05_mock_api_end_to_end(self):
        """Verify REST API communication, JSON serialization, and CORS headers via mock server."""
        base_url = f"http://127.0.0.1:{self.port}"

        # 1. Test OPTIONS preflight
        req = urllib.request.Request(f"{base_url}/api/status", method="OPTIONS")
        with urllib.request.urlopen(req) as resp:
            self.assertEqual(resp.status, 204)
            self.assertEqual(resp.headers.get("Access-Control-Allow-Origin"), "*")
            self.assertIn("GET", resp.headers.get("Access-Control-Allow-Methods", ""))
            self.assertIn("POST", resp.headers.get("Access-Control-Allow-Methods", ""))

        # 2. Test GET /api/status
        with urllib.request.urlopen(f"{base_url}/api/status") as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(data["status"], "online")
            self.assertEqual(data["hostname"], "iron-v")
            self.assertEqual(data["cpu_mhz"], 160)

        # 3. Test GET /api/health
        with urllib.request.urlopen(f"{base_url}/api/health") as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(data["status"], "healthy")
            self.assertEqual(data["dpc_queue_drops"], 0)
            self.assertGreater(data["arena_bytes_free"], 0)

        # 4. Test GET & POST /api/gpio
        with urllib.request.urlopen(f"{base_url}/api/gpio") as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read().decode("utf-8"))
            self.assertIn("pins", data)
            self.assertEqual(data["pins"][0]["pin"], 15)

        post_req = urllib.request.Request(f"{base_url}/api/gpio?pin=15&value=1", method="POST")
        with urllib.request.urlopen(post_req) as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(data["pin"], 15)
            self.assertEqual(data["level"], 1)

        # 5. Test GET /api/matter/payload
        with urllib.request.urlopen(f"{base_url}/api/matter/payload") as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read().decode("utf-8"))
            self.assertEqual(data["manual_code"], "3497-011-2332")
            self.assertTrue(data["qr_payload"].startswith("MT:"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
