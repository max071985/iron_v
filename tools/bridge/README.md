# Iron-V bridge (REV-24)

Optional tool: only needed for Home Assistant and Google Home. The board needs an image with the MQTT module
(`MODULES_ADD=mqtt` in `.config`, see [src/modules/mqtt/README.md](../../src/modules/mqtt/README.md)); the default light
image is local only (its page and REST v1).

Mosquitto and Home Assistant in rootless podman containers on a PC in the LAN. The board's only network peer is
the broker (one MQTT connection, keepalive 180 s). HA picks the light up from the discovery message the board
publishes itself, so there is no per-device YAML. Google Assistant reaches HA through a manual Google Home project
and a public HTTPS URL from Tailscale Funnel.

```
board --MQTT 3.1.1, LAN :1883--> Mosquitto <--MQTT 127.0.0.1--> Home Assistant :8123
                                                                   ^  https://<pc>.<tailnet>.ts.net (Funnel)
                                                     Google Home --+  (fulfillment, account linking, Report State)
```

Topics, payloads and discovery: `ironv/<device_id>/{availability,light/state,light/set}` and
`homeassistant/light/<device_id>/config` (board spec v1, REV-22/23). HA entity: `light.iron_v`.

| Path | Tracked | Purpose |
|---|---|---|
| `compose.yaml` | yes | Pinned images, host networking, restart policy |
| `mosquitto/mosquitto.conf` | yes | Port 1883, login required, persistent retained messages |
| `homeassistant/configuration.yaml` | yes | HA base config, URLs from `secrets.yaml`, `packages/` |
| `ha_http.py` | yes | HA HTTP settings (trusted proxy for Funnel, login ban) over the websocket API |
| `homeassistant/google/google_assistant.yaml` | yes | Google package, active once linked into `packages/` |
| `homeassistant/secrets.example.yaml` | yes | Template for `secrets.yaml` |
| `setup.sh` | yes | Every step below that can be scripted |
| `local/mqtt.env` | no | Broker users and passwords (generated, mode 600) |
| `local/ha.token` | no | HA long-lived token for `setup.sh` (mode 600) |
| `mosquitto/passwd`, `mosquitto/data/` | no | Password hashes, retained messages |
| `homeassistant/*` (everything else) | no | HA state, `secrets.yaml`, `google_service_account.json` |

`setup.sh` never prints a password or token. Back up `local/`, `mosquitto/data/` and `homeassistant/`.

## 1. PC prerequisites (once, needs sudo)

```sh
sudo pacman -S --needed podman podman-compose tailscale
sudo ufw allow from <lan-subnet> to any port 1883 proto tcp comment 'iron-v mqtt'
sudo ufw allow from <lan-subnet> to any port 8123 proto tcp comment 'home assistant (LAN)'
sudo systemctl enable --now tailscaled
sudo tailscale up                       # log in in the browser
sudo tailscale set --operator="$USER"   # tailscale funnel without sudo
systemctl --user enable --now podman-restart.service   # start the containers at login
loginctl enable-linger "$USER"                         # ... and at boot without a login
```

`<lan-subnet>` is the LAN the board is in, e.g. `192.168.1.0/24`. Port 8123 from the LAN is only needed for the
HA app or browser on the LAN; Funnel connects to HA through localhost.

## 2. Broker users, HA secrets, start

```sh
./setup.sh                      # local/mqtt.env, mosquitto/passwd, homeassistant/secrets.yaml
podman compose up -d
podman logs -f ironv-homeassistant   # first start takes a few minutes
```

## 3. HA onboarding (browser)

Open `http://<pc-lan-ip>:8123` and create the owner account. Use a strong password: after step 6 the login page
is on the internet (failed logins ban the client address after 5 tries).

## 4. HA token and MQTT integration

Profile (bottom left) > Security > Long-lived access tokens > Create token "iron-v bridge". Save it without it
ending up in the shell history:

```sh
install -m 600 /dev/null local/ha.token && cat > local/ha.token   # paste, Enter, Ctrl-D
./setup.sh ha-mqtt              # adds the MQTT integration: broker 127.0.0.1:1883, user homeassistant
./setup.sh http                 # trusted proxy 127.0.0.1 + X-Forwarded-For + ban after 5 failed logins
```

`http`: since HA 2026.9 the HTTP settings live in HA's storage, not in YAML (an `http:` block is imported once
as a trial and ignored afterwards). The step applies them as a trial (HA restarts), checks that a forwarded
request from localhost is accepted (HA answers 400 for an untrusted proxy, which would break Funnel), then
promotes the trial. `./setup.sh http show` prints the stored settings.

HA discovers the PC's Bluetooth adapter but cannot use it (no D-Bus in a rootless container) and retries every
few seconds; disable that entry (Settings > Devices & services > Bluetooth > ... > Disable).

## 5. Board

Flash an image with the MQTT module first (from the repository root: `MODULES_ADD=mqtt` in `.config`, then
`make flash`).

```sh
./setup.sh board                # REST v1: broker = this PC, user ironv, MQTT enabled (or: ./setup.sh board <ip>)
./setup.sh check                # retained availability/discovery/state, HA light.iron_v state
```

HA lists the device `iron-v` under Settings > Devices & services > MQTT. The board's `mqtt` shell command shows
the session.

## 6. Public HTTPS (Tailscale Funnel)

Only after step 3: until the owner account exists, whoever opens HA first can create it.

Tailscale, once: HTTPS certificates and the `funnel` attribute. On the first run `./setup.sh funnel` prints a
one-click link that enables both and then continues (by hand: admin console DNS > HTTPS Certificates, and
Access controls `"nodeAttrs": [{"target": ["autogroup:member"], "attr": ["funnel"]}]`). Machine and tailnet
names become public in the certificate transparency log, so rename first if needed. Machines > this PC > "..."
> Disable key expiry (an expired key takes Funnel and Google control down).

```sh
./setup.sh funnel               # first run prints a link to enable HTTPS and Funnel for the tailnet
podman restart ironv-homeassistant
```

Check from a phone on mobile data: `https://<pc>.<tailnet>.ts.net` shows the HA login page. The certificate
is issued within a minute; the public name has several relay addresses and a new Funnel can take a few minutes
until every relay serves it. From the PC itself the name resolves to the tailnet address (MagicDNS), so test the
public path with `curl --resolve <name>:443:<public A record>`. Each request without a valid token counts
towards the login ban of your own public address.

## 7. Google Home project (browser, your Google account)

Google Home Developer Console, <https://console.home.google.com/projects>:

1. Create project, name `iron-v`. Note the project ID. "Resource has been exhausted (e.g. check quota)" here
   means the account's Google Cloud project quota is used up (projects pending deletion count for 30 days).
   An existing Home project without a cloud-to-cloud integration works too (one integration per project; the
   HomeGraph API and the service account must be in that same project).
2. Add a cloud-to-cloud integration > Develop > Setup:
   - Integration name `Iron-V` (the app lists it as `[test] Iron-V`), device type Light, app icon 144 x 144 PNG.
   - OAuth Client ID: `https://oauth-redirect.googleusercontent.com/r/<project-id>`
   - Client secret: any letters and digits (HA does not check it)
   - Authorization URL: `https://<pc>.<tailnet>.ts.net/auth/authorize`
   - Token URL: `https://<pc>.<tailnet>.ts.net/auth/token`
   - Cloud fulfillment URL: `https://<pc>.<tailnet>.ts.net/api/google_assistant`
   - Scopes: `email` and `name`
   - Leave "Google transmits client ID and secret via HTTP basic auth header" unchecked. Save (status Draft).

Google Cloud console, same project, <https://console.cloud.google.com/>:

3. APIs & Services > Library: enable **HomeGraph API**.
4. IAM & Admin > Service accounts > Create: name `ironv-report-state`, role **Service Account Token Creator**.
   Keys > Add key > JSON. Save the file as `homeassistant/google_service_account.json` (untracked).

Then on the PC:

```sh
sed -i 's|^google_project_id:.*|google_project_id: "<project-id>"|' homeassistant/secrets.yaml
./setup.sh google               # checks both, links the package into homeassistant/packages/
podman restart ironv-homeassistant
```

Only `light.iron_v` is exposed (`expose_by_default: false`), as "Iron-V light".

## 8. Link in the Google Home app (phone)

Close the HA app/home-screen shortcut first (it can swallow the login redirect). Google Home > Devices > Add >
Works with Google Home > `[test] Iron-V` > HA login page (Funnel URL) > log in > assign a room. If the light does
not show up: HA Developer tools > Actions > `google_assistant.request_sync`.

## Operations

- Update: change the image tag in `compose.yaml`, re-check the board's discovery keys against
  `homeassistant/components/mqtt/light/schema_json.py` of that HA version, `podman compose up -d`.
- Logs: `podman logs ironv-mosquitto`, `podman logs ironv-homeassistant`.
- New broker passwords: delete `local/mqtt.env`, `./setup.sh`, update the HA MQTT integration
  (Settings > Devices & services > MQTT > Reconfigure), `./setup.sh board`.
- Google "404" on request sync after a long pause: open the project in the Developer Console and save the
  integration again (test projects can expire).

## Security notes

- MQTT is plain TCP on the LAN (the board has no TLS); anyone on the LAN who has the board user's password can
  control the light. The board's REST v1 has no auth (accepted risk, property model spec section 5).
- HA is public through Funnel: owner password, IP ban, no other users. Google authenticates with tokens issued by
  HA's own OAuth endpoints.
- Never commit `local/`, `secrets.yaml` or the service account key; `.gitignore` keeps them out.
