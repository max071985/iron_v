#!/usr/bin/env python3
"""Turns a build profile (profiles/<name>.config) plus the local .config (KEY=VALUE, see
config.example) into a C header that overrides the defaults in src/config.h and a make
fragment with the module selection and the main-stack budget.

The profile sets the build keys (MODULES, MAIN_STACK_MIN, POOL_*); the local .config sets
the device keys (hostname, Wi-Fi) and may add modules with MODULES_ADD. .config applies to
every profile, so it cannot replace a profile's module list or sizes (that would also change
the dev test build); a different combination is a profile of its own.

usage: gen_config.py --profile NAME <output header> <output .mk> <profile file> [<local .config>]
       gen_config.py --self-test

Both outputs are rewritten only when their content changes, so an unchanged configuration
does not trigger a rebuild. Values are validated; an invalid file stops the build.
"""
import os
import re
import sys
import tempfile

# KEY -> (C macro, kind, check)
KEYS = {
    'HOSTNAME':           ('CONFIG_DEVICE_HOSTNAME',     'str', 'hostname'),
    'AP_SSID':            ('CONFIG_WIFI_AP_SSID',        'str', 'ssid'),
    'AP_CHANNEL':         ('CONFIG_WIFI_AP_CHANNEL',     'int', 'channel'),
    'AP_AUTO_START':      ('CONFIG_WIFI_AUTO_START_AP',  'int', 'bool'),
    'STA_SSID':           ('CONFIG_WIFI_STA_SSID',       'str', 'ssid_or_empty'),
    'STA_PASSPHRASE':     ('CONFIG_WIFI_STA_PASSPHRASE', 'str', 'passphrase'),
    # Build profile keys (REV-33)
    'MODULES':            ('CONFIG_MODULES',             'list', 'modules'),
    'MAIN_STACK_MIN':     ('CONFIG_MAIN_STACK_MIN',      'int', 'stack'),
    'POOL_SMALL_BLOCKS':  ('CONFIG_POOL_SMALL_BLOCKS',   'int', 'pool'),
    'POOL_MEDIUM_BLOCKS': ('CONFIG_POOL_MEDIUM_BLOCKS',  'int', 'pool'),
    # Local only: modules added to whatever the profile selects
    'MODULES_ADD':        (None,                         'list', 'modules'),
}
PROFILE_ONLY = ('MODULES', 'MAIN_STACK_MIN', 'POOL_SMALL_BLOCKS', 'POOL_MEDIUM_BLOCKS')
LOCAL_ONLY = ('MODULES_ADD', 'STA_SSID', 'STA_PASSPHRASE')   # profiles are tracked: no credentials
REQUIRED = ('MODULES', 'MAIN_STACK_MIN')   # the Makefile needs both; every profile sets them

SSID_MAX = 32
PASS_MIN, PASS_MAX = 8, 63          # WPA2 passphrase; 64 hex chars (raw PSK) not supported
CHANNEL_MIN, CHANNEL_MAX = 1, 13
HOSTNAME_MAX = 32
HOSTNAME_RE = re.compile(r'^[a-z0-9]([a-z0-9-]*[a-z0-9])?$')
MODULE_RE = re.compile(r'^[a-z][a-z0-9_]*$')
STACK_MIN, STACK_MAX, STACK_ALIGN = 4096, 131072, 16
POOL_BLOCKS_MAX = 32                # src/arena.c tracks a pool's blocks in one 32-bit mask
PROFILE_RE = re.compile(r'^[a-z][a-z0-9_-]*$')

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULES_DIR = os.path.join(REPO, 'src', 'modules')


class ConfigError(Exception):
    pass


def parse(text, origin='.config', local=True):
    """Returns {KEY: value}; blank lines and # comments are skipped, values are taken as-is."""
    values = {}
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if '=' not in line:
            raise ConfigError('%s line %d: expected KEY=VALUE' % (origin, n))
        key, value = line.split('=', 1)
        key = key.strip()
        if key not in KEYS:
            raise ConfigError('%s line %d: unknown key %s (see config.example)' % (origin, n, key))
        if local and key in PROFILE_ONLY:
            raise ConfigError('%s line %d: %s is a profile key (profiles/*.config); to add modules '
                              'for your builds use MODULES_ADD' % (origin, n, key))
        if not local and key in LOCAL_ONLY:
            raise ConfigError('%s line %d: %s belongs in .config only (profiles are tracked)' % (origin, n, key))
        if key in values:
            raise ConfigError('%s line %d: %s set twice' % (origin, n, key))
        values[key] = value.strip()
    return values


def merge(profile, local):
    """Profile keys, then the local device keys; MODULES_ADD appends to the profile's modules."""
    out = dict(profile)
    for key, value in local.items():
        if key == 'MODULES_ADD':
            names = out.get('MODULES', '').split()
            names += [m for m in value.split() if m not in names]
            out['MODULES'] = ' '.join(names)
        else:
            out[key] = value
    return out


def check(key, value, modules_dir=MODULES_DIR):
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
    elif kind == 'stack':
        if not value.isdigit() or not (STACK_MIN <= int(value) <= STACK_MAX) or int(value) % STACK_ALIGN:
            raise ConfigError('%s: %d to %d bytes, a multiple of %d' % (key, STACK_MIN, STACK_MAX, STACK_ALIGN))
    elif kind == 'pool':
        if not value.isdigit() or int(value) > POOL_BLOCKS_MAX:
            raise ConfigError('%s: 0 to %d blocks' % (key, POOL_BLOCKS_MAX))
    elif kind == 'modules':
        names = value.split()
        for name in names:
            if not MODULE_RE.match(name):
                raise ConfigError('%s: bad module name %r' % (key, name))
            if not os.path.isfile(os.path.join(modules_dir, name, 'module.mk')):
                raise ConfigError('%s: unknown module %s (no src/modules/%s/module.mk)' % (key, name, name))
        if len(set(names)) != len(names):
            raise ConfigError('%s: a module is listed twice' % key)


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


def render(profile_values, local_values, profile=None, modules_dir=MODULES_DIR):
    """Returns (header, make fragment)."""
    for layer in (profile_values, local_values):
        for key, value in layer.items():
            check(key, value, modules_dir)
    values = merge(profile_values, local_values)
    if values.get('STA_PASSPHRASE') and not values.get('STA_SSID'):
        raise ConfigError('STA_PASSPHRASE needs STA_SSID')
    for key in REQUIRED:
        if key not in values:
            raise ConfigError('%s missing: every profile sets it (see profiles/)' % key)
    lines = ['/* Generated from the build profile and .config by scripts/gen_config.py - do not edit, never commit */',
             '#ifndef IRON_V_CONFIG_GEN_H', '#define IRON_V_CONFIG_GEN_H']
    if profile is not None:
        lines.append('#define %-28s %s' % ('CONFIG_PROFILE', c_string(profile)))
    for key in KEYS:
        macro, kind, _ = KEYS[key]
        if key not in values or macro is None:
            continue
        value = values[key]
        if kind == 'list':
            names = value.split()
            lines.append('#define %-28s %s' % (macro, c_string(' '.join(names))))
            for name in names:
                lines.append('#define %-28s 1U' % ('CONFIG_MODULE_' + name.upper()))
        else:
            lines.append('#define %-28s %s' % (macro, c_string(value) if kind == 'str' else value + 'U'))
    lines.append('#endif')
    mk = ['# Generated by scripts/gen_config.py - do not edit',
          'PROFILE_MODULES := %s' % ' '.join(values['MODULES'].split()),
          'PROFILE_MAIN_STACK_MIN := %s' % values['MAIN_STACK_MIN']]
    return '\n'.join(lines) + '\n', '\n'.join(mk) + '\n'


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
    light = 'MODULES=light\nMAIN_STACK_MIN=8192\n'
    with tempfile.TemporaryDirectory() as mods:
        for name in ('light', 'mqtt', 'dev'):
            os.makedirs(os.path.join(mods, name))
            open(os.path.join(mods, name, 'module.mk'), 'w').close()

        def gen(profile_text, local_text='', profile_name='light'):
            return render(parse(profile_text, 'profile', local=False), parse(local_text, '.config'), profile_name, mods)

        ok, mk = gen(light, '# comment\n\nHOSTNAME=lamp-1\nAP_CHANNEL=6\nSTA_SSID=My "Net"\\x\nSTA_PASSPHRASE=pass word99\n')
        assert '#define CONFIG_DEVICE_HOSTNAME       "lamp-1"' in ok, ok
        assert '#define CONFIG_WIFI_AP_CHANNEL       6U' in ok, ok
        assert '#define CONFIG_WIFI_STA_SSID         "My \\"Net\\"\\\\x"' in ok, ok
        assert '#define CONFIG_WIFI_STA_PASSPHRASE   "pass word99"' in ok, ok
        assert '#define CONFIG_PROFILE               "light"' in ok, ok
        assert '#define CONFIG_MODULE_LIGHT          1U' in ok, ok
        assert '#define CONFIG_MAIN_STACK_MIN        8192U' in ok, ok
        assert 'CONFIG_WIFI_AP_SSID' not in ok, 'unset keys keep the defaults'
        assert 'PROFILE_MODULES := light\n' in mk and 'PROFILE_MAIN_STACK_MIN := 8192\n' in mk, mk

        # MODULES_ADD appends to the profile's list, skips modules it already has
        ok, mk = gen('MODULES=light\nMAIN_STACK_MIN=8192\nPOOL_SMALL_BLOCKS=4\n', 'MODULES_ADD=mqtt\n')
        assert 'PROFILE_MODULES := light mqtt\n' in mk, mk
        assert '#define CONFIG_MODULES               "light mqtt"' in ok, ok
        assert 'CONFIG_MODULE_MQTT' in ok and 'CONFIG_MODULE_DEV' not in ok, ok
        assert '#define CONFIG_POOL_SMALL_BLOCKS     4U' in ok and 'MODULES_ADD' not in ok, ok
        _, mk = gen('MODULES=light mqtt dev\nMAIN_STACK_MIN=32768\n', 'MODULES_ADD=mqtt\n', 'dev')
        assert 'PROFILE_MODULES := light mqtt dev\n' in mk, 'the dev build keeps its modules'
        assert gen('MODULES=\nMAIN_STACK_MIN=4096\n')[1].startswith('# Generated'), 'empty module list = core only'

        for bad in ('NOPE=1', 'AP_CHANNEL=14', 'AP_CHANNEL=x', 'STA_PASSPHRASE=short', 'HOSTNAME=Bad_Name',
                    'AP_SSID=', 'STA_SSID=' + 'x' * 33, 'AP_AUTO_START=2', 'STA_PASSPHRASE=longenough1',
                    'HOSTNAME=a\nHOSTNAME=b', 'garbage', 'MODULES_ADD=nosuch', 'MODULES_ADD=mqtt mqtt',
                    'MODULES_ADD=Mqtt',
                    # profile keys cannot come from .config (it applies to every profile, dev included)
                    'MODULES=light mqtt', 'MAIN_STACK_MIN=16384', 'POOL_SMALL_BLOCKS=0'):
            try:
                gen(light, bad)
            except ConfigError:
                continue
            raise AssertionError('accepted invalid .config: %r' % bad)
        for bad in ('MODULES=light nosuch', 'MODULES=light light', 'MODULES=Light', 'MAIN_STACK_MIN=4000\nMODULES=light',
                    'MAIN_STACK_MIN=1024\nMODULES=light', 'MAIN_STACK_MIN=8k\nMODULES=light',
                    light + 'POOL_SMALL_BLOCKS=33', light + 'POOL_MEDIUM_BLOCKS=-1', light + 'MODULES_ADD=mqtt',
                    light + 'STA_SSID=net', light + 'STA_PASSPHRASE=secret123',
                    'MODULES=light\n', 'MAIN_STACK_MIN=8192\n', ''):
            try:
                gen(bad)
            except ConfigError:
                continue
            raise AssertionError('accepted invalid profile: %r' % bad)

    # The tracked profiles themselves must be valid
    profiles_dir = os.path.join(REPO, 'profiles')
    names = sorted(f[:-len('.config')] for f in os.listdir(profiles_dir) if f.endswith('.config'))
    assert names, 'no profiles in ' + profiles_dir
    for name in names:
        assert PROFILE_RE.match(name), 'bad profile name ' + name
        with open(os.path.join(profiles_dir, name + '.config')) as f:
            render(parse(f.read(), name + '.config', local=False), {}, name)
    print('gen_config self-test: OK (profiles: %s)' % ', '.join(names))


def main(argv):
    if argv[1:] == ['--self-test']:
        self_test()
        return 0
    args = argv[1:]
    if len(args) not in (5, 6) or args[0] != '--profile' or not PROFILE_RE.match(args[1]):
        print(__doc__, file=sys.stderr)
        return 2
    profile, header, makefrag, profile_file = args[1:5]
    local_file = args[5] if len(args) == 6 else None
    try:
        with open(profile_file) as f:
            profile_values = parse(f.read(), profile_file, local=False)
        local_values = {}
        if local_file is not None:
            with open(local_file) as f:
                local_values = parse(f.read(), local_file)
        h, mk = render(profile_values, local_values, profile)
    except (ConfigError, OSError) as e:
        print('gen_config: %s' % e, file=sys.stderr)
        return 1
    if write_if_changed(header, h) and local_values.get('STA_PASSPHRASE'):
        print('gen_config: STA credentials from %s are compiled into this image; '
              'do not share its firmware.bin' % local_file, file=sys.stderr)
    write_if_changed(makefrag, mk)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
