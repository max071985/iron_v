"""Home Assistant HTTP settings over the websocket API (REV-24).

Since HA 2026.9 the `http:` YAML block is imported once into storage as an unconfirmed trial and then ignored;
a trial that is not promoted within 5 minutes is reverted to the stored "stable" config. This applies the
settings below as a trial (HA restarts) or promotes the running trial.

Runs inside the HA container (its Python has aiohttp); setup.sh pipes it in:
    podman exec -i -e HA_TOKEN ironv-homeassistant python3 - show|apply|promote < ha_http.py
"""

import asyncio
import json
import os
import sys

import aiohttp

WS_URL = "http://127.0.0.1:8123/api/websocket"

# Tailscale Funnel connects from localhost; failed logins from the internet ban the client address.
CONFIG = {
    "server_port": 8123,
    "use_x_forwarded_for": True,
    "trusted_proxies": ["127.0.0.1/32", "::1/128"],
    "ip_ban_enabled": True,
    "login_attempts_threshold": 5,
}
SHOWN_KEYS = ("use_x_forwarded_for", "trusted_proxies", "ip_ban_enabled", "login_attempts_threshold", "error")


async def call(ws: aiohttp.ClientWebSocketResponse, msg_id: int, msg: dict) -> dict:
    await ws.send_json({"id": msg_id, **msg})
    while True:
        reply = await ws.receive_json()
        if reply.get("id") == msg_id:
            return reply


def summary(conf: dict | None) -> str:
    if conf is None:
        return "none"
    return json.dumps({k: conf.get(k) for k in SHOWN_KEYS})


async def main(action: str) -> int:
    async with aiohttp.ClientSession() as session, session.ws_connect(WS_URL) as ws:
        await ws.receive_json()  # auth_required
        await ws.send_json({"type": "auth", "access_token": os.environ["HA_TOKEN"]})
        if (await ws.receive_json()).get("type") != "auth_ok":
            print("HA: token rejected")
            return 1

        if action == "show":
            res = (await call(ws, 1, {"type": "http/config"}))["result"]
            print("active ", res["active_config_type"], "revert_at", res["revert_at"])
            print("stable ", summary(res["stable"]))
            print("pending", summary(res["pending"]))
            return 0
        if action == "apply":
            reply = await call(ws, 1, {"type": "http/config/configure", "config": CONFIG})
        elif action == "promote":
            reply = await call(ws, 1, {"type": "http/config/promote"})
        else:
            print("usage: show|apply|promote")
            return 1
        print("HA http", action, "ok" if reply.get("success") else reply.get("error"), reply.get("result") or "")
        return 0 if reply.get("success") else 1


sys.exit(asyncio.run(main(sys.argv[1] if len(sys.argv) > 1 else "show")))
