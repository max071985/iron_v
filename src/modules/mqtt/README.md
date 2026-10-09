# MQTT module

Optional module (off in the default `light` profile). It connects the board to an MQTT broker on the LAN and
publishes the light to Home Assistant through MQTT discovery; Home Assistant can then pass it on to Google Home.
Without this module the board is controlled locally only: its page at `/` and REST v1 (`/api/v1/light`).

| File | Content |
|---|---|
| `mqtt.c`, `mqtt.h` | MQTT 3.1.1 client: session, reconnect backoff, discovery, light state and commands |
| `mqtt_module.c` | Registration with the core: init/tick, `/api/v1/mqtt`, the `mqtt` fields of `/api/v1/device`, the `mqtt` command |
| `module.mk` | Sources; requires the `light` module |

Cost when selected: about 10 KB of flash and 1.1 KB of RAM.

## Enable

1. Add the module to the build. Either for your local builds, in `.config` (see `config.example`):

   ```
   MODULES_ADD=mqtt
   ```

   or as a profile of its own, e.g. `profiles/light-mqtt.config` (copy `profiles/light.config`, set
   `MODULES=light mqtt`), built with `make PROFILE=light-mqtt`. The `dev` test build always includes it.
2. Build and flash: `make flash` (or `make PROFILE=<profile> flash`). `info` on the console lists the modules of
   the running image; `/api/v1/device` names its profile.
3. Point the board at the broker, over USB:

   ```
   mqtt set host 192.168.1.10
   mqtt set user <user>
   mqtt set pass <password>
   mqtt on
   ```

   or over the LAN: `POST /api/v1/mqtt` with `{"enabled":true,"host":"192.168.1.10","port":1883,"user":"...",
   "password":"..."}` (any subset; the password is never returned). The settings live in NVS and survive
   reboots and new images. `mqtt` shows the session; `mqtt off` disconnects and stops reconnecting.

The broker address is an IPv4 address: the board has no DNS client.

## Protocol

MQTT 3.1.1 over plain TCP (port 1883 by default), clean session, keepalive 180 s. The broker publishes the Will
when the board has been silent for 270 s, so Home Assistant shows the light unavailable within about 4.5 minutes.
Reconnects back off from 1 s to 5 min with jitter and only start while the board has its Wi-Fi lease.

`<id>` is the device id from `/api/v1/device`: `ironv-` plus the last three bytes of the board's MAC.

| Topic | Direction | QoS | Retain | Payload |
|---|---|---|---|---|
| `ironv/<id>/availability` | board to broker | 1 | yes | `online` after connecting; `offline` (Will, or before `mqtt off`) |
| `ironv/<id>/light/state` | board to broker | 1 | yes | state JSON, on every change (at most every 250 ms) and after each connect |
| `ironv/<id>/light/set` | broker to board | 1 | no | command JSON |
| `homeassistant/light/<id>/config` | board to broker | 1 | yes | Home Assistant discovery for an MQTT `json` light, once per session |

State, always complete:

```json
{"state":"ON","brightness":40,"color_mode":"rgb","color":{"r":255,"g":120,"b":0}}
```

Commands carry the wanted state (any subset of the keys, never a toggle):

```json
{"state":"ON","brightness":75}
{"color":{"r":0,"g":0,"b":255}}
```

A command that arrives with the retain flag set is dropped, so a stale command never replays on a reconnect.
Nothing else is published: no telemetry or heartbeat topics.

## Bridge

[`tools/bridge/`](../../../tools/bridge/README.md) sets up Mosquitto and Home Assistant on a PC in the LAN
(rootless podman), adds the MQTT integration, points the board at the broker (`./setup.sh board`) and, optionally,
links Google Home through a manual Google Home project and Tailscale Funnel.
