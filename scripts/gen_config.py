#!/usr/bin/env python3
"""Turns the local .config (KEY=VALUE, see config.example) into a C header that overrides
the defaults in src/config.h. Without a .config the header is empty and the defaults apply.

usage: gen_config.py <.config path> <output header>
       gen_config.py --self-test

The output is rewritten only when its content changes, so an unchanged .config does not
trigger a rebuild. Values are validated; an invalid .config stops the build.
"""
import os
import re
import sys

# KEY -> (C macro, kind, check)
KEYS = {
    'HOSTNAME':       ('CONFIG_DEVICE_HOSTNAME',     'str', 'hostname'),
    'AP_SSID':        ('CONFIG_WIFI_AP_SSID',        'str', 'ssid'),
    'AP_CHANNEL':     ('CONFIG_WIFI_AP_CHANNEL',     'int', 'channel'),
    'AP_AUTO_START':  ('CONFIG_WIFI_AUTO_START_AP',  'int', 'bool'),
    'STA_SSID':       ('CONFIG_WIFI_STA_SSID',       'str', 'ssid_or_empty'),
    'STA_PASSPHRASE': ('CONFIG_WIFI_STA_PASSPHRASE', 'str', 'passphrase'),
}

SSID_MAX = 32
PASS_MIN, PASS_MAX = 8, 63          # WPA2 passphrase; 64 hex chars (raw PSK) not supported
CHANNEL_MIN, CHANNEL_MAX = 1, 13
HOSTNAME_MAX = 32
HOSTNAME_RE = re.compile(r'^[a-z0-9]([a-z0-9-]*[a-z0-9])?$')


class ConfigError(Exception):
    pass


def parse(text):
    """Returns {KEY: value}; blank lines and # comments are skipped, values are taken as-is."""
    values = {}
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if '=' not in line:
            raise ConfigError('line %d: expected KEY=VALUE' % n)
        key, value = line.split('=', 1)
        key = key.strip()
        if key not in KEYS:
            raise ConfigError('line %d: unknown key %s (see config.example)' % (n, key))
        if key in values:
            raise ConfigError('line %d: %s set twice' % (n, key))
        values[key] = value.strip()
    return values


def check(key, value):
    kind = KEYS[key][2]
    if kind == 'hostname':
        if len(value) > HOSTNAME_MAX or not HOSTNAME_RE.match(value):
            raise ConfigError('%s: lowercase letters, digits and inner hyphens, up to %d characters' % (key, HOSTNAME_MAX))
    elif kind in ('ssid', 'ssid_or_empty'):
        if (kind == 'ssid' and not value) or len(value.encode()) > SSID_MAX:
            raise ConfigError('%s: 1 to %d bytes' % (key, SSID_MAX) if kind == 'ssid' else '%s: up to %d bytes' % (key, SSID_MAX))
    elif kind == 'passphrase':
        if value and not (PASS_MIN <= len(value) <= PASS_MAX):
            raise ConfigError('%s: %d to %d characters (or empty)' % (key, PASS_MIN, PASS_MAX))
    elif kind == 'channel':
        if not value.isdigit() or not (CHANNEL_MIN <= int(value) <= CHANNEL_MAX):
            raise ConfigError('%s: %d to %d' % (key, CHANNEL_MIN, CHANNEL_MAX))
    elif kind == 'bool':
        if value not in ('0', '1'):
            raise ConfigError('%s: 0 or 1' % key)


def c_string(value):
    out = []
    for ch in value.encode():
        if ch in (0x22, 0x5C):                      # " and backslash
            out.append('\\' + chr(ch))
        elif 0x20 <= ch < 0x7F:
            out.append(chr(ch))
        else:
            out.append('\\%03o' % ch)               # octal: cannot run into the next character
    return '"' + ''.join(out) + '"'


def render(values):
    for key, value in values.items():
        check(key, value)
    if values.get('STA_PASSPHRASE') and not values.get('STA_SSID'):
        raise ConfigError('STA_PASSPHRASE needs STA_SSID')
    lines = ['/* Generated from .config by scripts/gen_config.py - do not edit, never commit */',
             '#ifndef IRON_V_CONFIG_GEN_H', '#define IRON_V_CONFIG_GEN_H']
    for key in KEYS:
        if key in values:
            macro, kind, _ = KEYS[key]
            value = values[key]
            lines.append('#define %-28s %s' % (macro, c_string(value) if kind == 'str' else value + 'U'))
    lines.append('#endif')
    return '\n'.join(lines) + '\n'


def write_if_changed(path, content):
    try:
        with open(path) as f:
            if f.read() == content:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'w') as f:
        f.write(content)
    return True


def self_test():
    ok = render(parse('# comment\n\nHOSTNAME=lamp-1\nAP_CHANNEL=6\nSTA_SSID=My "Net"\\x\nSTA_PASSPHRASE=pass word99\n'))
    assert '#define CONFIG_DEVICE_HOSTNAME       "lamp-1"' in ok, ok
    assert '#define CONFIG_WIFI_AP_CHANNEL       6U' in ok, ok
    assert '#define CONFIG_WIFI_STA_SSID         "My \\"Net\\"\\\\x"' in ok, ok
    assert '#define CONFIG_WIFI_STA_PASSPHRASE   "pass word99"' in ok, ok
    assert 'CONFIG_WIFI_AP_SSID' not in ok, 'unset keys keep the defaults'
    assert render({}).count('#define') == 1, 'empty .config: guard only'
    for bad in ('NOPE=1', 'AP_CHANNEL=14', 'AP_CHANNEL=x', 'STA_PASSPHRASE=short', 'HOSTNAME=Bad_Name',
                'AP_SSID=', 'STA_SSID=' + 'x' * 33, 'AP_AUTO_START=2', 'STA_PASSPHRASE=longenough1',
                'HOSTNAME=a\nHOSTNAME=b', 'garbage'):
        try:
            render(parse(bad))
        except ConfigError:
            continue
        raise AssertionError('accepted invalid .config: %r' % bad)
    print('gen_config self-test: OK')


def main(argv):
    if argv[1:] == ['--self-test']:
        self_test()
        return 0
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, out = argv[1], argv[2]
    try:
        values = parse(open(src).read()) if os.path.exists(src) else {}
        content = render(values)
    except ConfigError as e:
        print('%s: %s' % (src, e), file=sys.stderr)
        return 1
    if write_if_changed(out, content) and values.get('STA_PASSPHRASE'):
        print('gen_config: STA credentials from %s are compiled into this image; '
              'do not share build/firmware.bin' % src, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
