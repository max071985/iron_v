#!/usr/bin/env bash
# Iron-V bridge setup (REV-24). Idempotent; never prints passwords or tokens. Steps: tools/bridge/README.md.
#   ./setup.sh               broker users + password file, HA secrets.yaml (before the first start)
#   ./setup.sh ha-mqtt       add the MQTT integration to HA (needs local/ha.token)
#   ./setup.sh http [show]   trusted proxy + login ban for Funnel: trial, check, promote (ha_http.py)
#   ./setup.sh board [addr]  point the board at this broker over REST v1 (default iron-v.local)
#   ./setup.sh funnel        publish HA over Tailscale Funnel, set external_url
#   ./setup.sh google        enable the Google Assistant package (project ID + service account key)
#   ./setup.sh check         retained topics on the broker and the HA light state
set -euo pipefail
cd "$(dirname "$0")"

RUNTIME="${RUNTIME:-podman}"
HA_URL="${HA_URL:-http://127.0.0.1:8123}"
BROKER_PORT=1883
HA_PORT=8123
CRED=local/mqtt.env
TOKEN=local/ha.token
SECRETS=homeassistant/secrets.yaml
SA_KEY=homeassistant/google_service_account.json
GOOGLE_LINK=homeassistant/packages/google_assistant.yaml
ENTITY=light.iron_v
IMAGE="$(sed -n 's/^ *image: *\(.*eclipse-mosquitto.*\)$/\1/p' compose.yaml)"

die() { echo "error: $*" >&2; exit 1; }

load_cred() {
    [ -f "$CRED" ] || die "$CRED missing; run ./setup.sh first"
    # shellcheck disable=SC1090
    . "./$CRED"
}

# Source address this PC uses towards $1 (default: the internet), i.e. its LAN address.
local_ip() { ip -4 route get "${1:-1.1.1.1}" | sed -n 's/.* src \([0-9.]*\).*/\1/p'; }

# Token header for the HA API as a curl header file (keeps the token out of the process list).
auth_header() { printf 'Authorization: Bearer %s\n' "$(cat "$TOKEN")"; }

ha_api() { # method path [curl args]
    local method="$1" path="$2"; shift 2
    curl -sS --fail-with-body --max-time 15 -X "$method" -H @<(auth_header) -H 'Content-Type: application/json' \
        "$@" "$HA_URL$path"
}

# ha_http.py inside the HA container; the token travels in the environment, not on a command line.
ha_ws() { HA_TOKEN="$(cat "$TOKEN")" "$RUNTIME" exec -i -e HA_TOKEN ironv-homeassistant python3 - "$@" < ha_http.py; }

set_secret() { # key value
    sed -i "s|^$1:.*|$1: \"$2\"|" "$SECRETS"
}

cmd_init() {
    mkdir -p local mosquitto/data homeassistant/packages
    chmod 700 local

    if [ ! -f "$CRED" ]; then
        (
            umask 077
            {
                echo "MQTT_BOARD_USER=ironv"
                echo "MQTT_BOARD_PASS=$(head -c 18 /dev/urandom | base64 | tr -d '/+=')"
                echo "MQTT_HA_USER=homeassistant"
                echo "MQTT_HA_PASS=$(head -c 18 /dev/urandom | base64 | tr -d '/+=')"
            } > "$CRED"
        )
        echo "created $CRED"
    fi
    load_cred

    # Password file (hashes only), rebuilt when missing or older than the credentials. Written inside the
    # container: after the first start the broker owns mosquitto/ (rootless podman maps it to a sub-UID).
    if [ ! -s mosquitto/passwd ] || [ "$CRED" -nt mosquitto/passwd ]; then
        "$RUNTIME" run --rm --env-file "$CRED" -v "$PWD/mosquitto:/m:Z" "$IMAGE" sh -c '
            rm -f /m/passwd && touch /m/passwd && chmod 0600 /m/passwd &&
            mosquitto_passwd -b /m/passwd "$MQTT_BOARD_USER" "$MQTT_BOARD_PASS" &&
            mosquitto_passwd -b /m/passwd "$MQTT_HA_USER" "$MQTT_HA_PASS" &&
            chown mosquitto:mosquitto /m/passwd' >/dev/null
        echo "mosquitto/passwd: users $MQTT_BOARD_USER, $MQTT_HA_USER"
        if "$RUNTIME" container exists ironv-mosquitto 2>/dev/null; then
            "$RUNTIME" restart ironv-mosquitto >/dev/null
            echo "restarted ironv-mosquitto (new password file)"
        fi
    fi

    if [ ! -f "$SECRETS" ]; then
        (umask 077; cp homeassistant/secrets.example.yaml "$SECRETS")
        local url
        url="http://$(local_ip):$HA_PORT"
        set_secret internal_url "$url"
        set_secret external_url "$url"   # replaced by ./setup.sh funnel
        echo "created $SECRETS (internal_url $url)"
    fi
}

cmd_ha_mqtt() {
    load_cred
    [ -s "$TOKEN" ] || die "save a long-lived HA token in $TOKEN (README step 4)"
    if ha_api GET "/api/config/config_entries/entry?domain=mqtt" | grep -q '"entry_id"'; then
        echo "HA: MQTT integration already configured"
        return
    fi
    local flow_id result
    flow_id="$(ha_api POST /api/config/config_entries/flow -d '{"handler":"mqtt"}' |
        python3 -I -c 'import json,sys; print(json.load(sys.stdin)["flow_id"])')"
    # Broker step of HA's MQTT config flow (fields as in HA 2026.9 mqtt/config_flow.py).
    result="$(PORT="$BROKER_PORT" MQTT_HA_USER="$MQTT_HA_USER" MQTT_HA_PASS="$MQTT_HA_PASS" python3 -I -c '
import json, os
print(json.dumps({"broker": "127.0.0.1", "port": int(os.environ["PORT"]),
                  "username": os.environ["MQTT_HA_USER"], "password": os.environ["MQTT_HA_PASS"],
                  "other_settings": {"set_client_cert": False, "set_ca_cert": "off"}}))' |
        ha_api POST "/api/config/config_entries/flow/$flow_id" --data-binary @-)" ||
        die "HA refused the MQTT flow: $result"
    # Print only the outcome (type, title = broker address, errors); never the entry data.
    printf '%s' "$result" | python3 -I -c '
import json, sys
d = json.load(sys.stdin)
print("HA: MQTT integration", d.get("type"), d.get("title", ""), d.get("errors") or "")'
}

cmd_board() {
    load_cred
    local addr="${1:-iron-v.local}" board_ip host_ip
    board_ip="$(getent ahostsv4 "$addr" | awk 'NR == 1 { print $1 }')"
    [ -n "$board_ip" ] || die "cannot resolve $addr"
    host_ip="$(local_ip "$board_ip")"
    # The MQTT client is an optional firmware module (src/modules/mqtt/README.md): no module, no /api/v1/mqtt
    if [ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 "http://$board_ip/api/v1/mqtt")" = 404 ]; then
        die "the board's image has no MQTT module; build it with MODULES_ADD=mqtt in .config (src/modules/mqtt/README.md)"
    fi
    # Body built by the printf builtin and sent on stdin: the password never appears in a process list.
    # The board answers with its GET view, which has no password (has_password only).
    printf '{"enabled":true,"host":"%s","port":%d,"user":"%s","password":"%s"}' \
        "$host_ip" "$BROKER_PORT" "$MQTT_BOARD_USER" "$MQTT_BOARD_PASS" |
        curl -sS --fail-with-body --max-time 10 -H 'Content-Type: application/json' --data-binary @- \
            "http://$board_ip/api/v1/mqtt"
    echo
}

cmd_http() {
    [ -s "$TOKEN" ] || die "save a long-lived HA token in $TOKEN (README step 4)"
    if [ "${1:-}" = show ]; then ha_ws show; return; fi
    ha_ws apply
    echo "waiting for HA to restart with the trial config (promote within 5 min or HA reverts it)"
    local deadline=$((SECONDS + 240)) code
    until ha_ws show 2>/dev/null | grep -q '^active  *pending'; do
        [ "$SECONDS" -lt "$deadline" ] || die "HA did not come back with the trial config"
        sleep 3
    done
    # A forwarded request from localhost (as Funnel sends it) must be accepted; HA answers 400 for an untrusted proxy.
    code="$(curl -s -o /dev/null -w '%{http_code}' -H @<(auth_header) -H 'X-Forwarded-For: 203.0.113.9' "$HA_URL/api/")"
    [ "$code" = 200 ] || die "forwarded request got HTTP $code; trial not promoted, HA reverts it in 5 min"
    echo "forwarded request from localhost: HTTP $code"
    ha_ws promote
    ha_ws show
}

cmd_funnel() {
    command -v tailscale >/dev/null || die "tailscale not installed (README step 1)"
    [ -f "$SECRETS" ] || die "$SECRETS missing; run ./setup.sh first"
    # Public https://<pc>.<tailnet>.ts.net:443 -> HA on this PC (trusted proxy 127.0.0.1: ./setup.sh http).
    # On a tailnet without HTTPS/Funnel this prints a one-click enable link and waits for it.
    tailscale funnel --bg "http://127.0.0.1:$HA_PORT"
    local name
    name="$(tailscale status --json | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["Self"]["DNSName"].rstrip("."))')"
    set_secret external_url "https://$name"
    echo "external_url https://$name; restart HA: $RUNTIME restart ironv-homeassistant"
}

cmd_google() {
    [ -f "$SECRETS" ] || die "$SECRETS missing; run ./setup.sh first"
    grep -q '^google_project_id: *"[a-z0-9-]*"$' "$SECRETS" ||
        die "set google_project_id in $SECRETS (README step 7)"
    [ -s "$SA_KEY" ] || die "save the service account key as $SA_KEY (README step 7)"
    chmod 600 "$SA_KEY" "$SECRETS"
    mkdir -p homeassistant/packages
    ln -sfn ../google/google_assistant.yaml "$GOOGLE_LINK"
    echo "Google package enabled; restart HA: $RUNTIME restart ironv-homeassistant"
}

cmd_check() {
    load_cred
    echo "== retained on the broker (ironv/#, homeassistant/#)"
    "$RUNTIME" exec ironv-mosquitto mosquitto_sub -h 127.0.0.1 -p "$BROKER_PORT" \
        -u "$MQTT_HA_USER" -P "$MQTT_HA_PASS" -t 'ironv/#' -t 'homeassistant/#' -v --retained-only -W 3 || true
    [ -s "$TOKEN" ] || { echo "(no $TOKEN: HA state skipped)"; return; }
    echo "== HA $ENTITY"
    ha_api GET "/api/states/$ENTITY" | python3 -I -c '
import json, sys
d = json.load(sys.stdin); a = d["attributes"]
print(d["state"], "brightness", a.get("brightness"), "rgb", a.get("rgb_color"), "mode", a.get("color_mode"),
      "updated", d["last_updated"])'
}

case "${1:-init}" in
    init)    cmd_init ;;
    ha-mqtt) cmd_ha_mqtt ;;
    http)    shift; cmd_http "$@" ;;
    board)   shift; cmd_board "$@" ;;
    funnel)  cmd_funnel ;;
    google)  cmd_google ;;
    check)   cmd_check ;;
    *)       sed -n '2,9p' "$0"; exit 1 ;;
esac
