#!/usr/bin/env python3
"""xenia-thor MCP server: control the emulator on the AYN Thor over adb.

Runs on the PC as a stdio MCP server. Every tool wraps adb, the build
script, or the device log, and applies the device rules from CLAUDE.md:
pre-flight before a launch, force-stop after a run, Turnip on every launch,
and the shipping cvar set for a GUI-parity launch.

Register in .mcp.json (repo root) and it appears as tools named
xenia_* in Claude Code.
"""
from __future__ import annotations

import datetime
import json
import os
import re
import shlex
import urllib.parse
import subprocess
import sys
import time
from typing import Optional

from mcp.server.fastmcp import FastMCP

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SERIAL = os.environ.get('XE_THOR_SERIAL', 'c3ca0370')
PKG = 'jp.xenia.emulator.github.debug'
LAUNCHER = f'{PKG}/jp.xenia.emulator.LauncherActivity'
OTHER_EMULATORS = ['net.rpcsx.easy', 'org.azahar_emu.azahar.debug', 'org.azahar_emu.azahar']
SCRATCH = os.environ.get('XE_THOR_SCRATCH') or os.path.join(REPO, 'scratch', 'mcp')
BUILD_SCRIPT = os.path.join(REPO, 'tools', 'thor', 'thor_build.ps1')
APK = os.path.join(REPO, 'android', 'android_studio_project', 'app', 'build',
                   'outputs', 'apk', 'github', 'debug', 'app-github-debug.apk')

mcp = FastMCP('xenia-thor')


def _run(cmd: list[str], timeout: int = 60) -> tuple[int, str]:
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                          encoding='utf-8', errors='replace')
    return proc.returncode, (proc.stdout + proc.stderr).strip()


def _adb(*args: str, timeout: int = 60) -> str:
    code, out = _run(['adb', '-s', SERIAL, *args], timeout=timeout)
    return out


def _shell(cmd: str, timeout: int = 60) -> str:
    return _adb('shell', cmd, timeout=timeout)


def _run_as(cmd: str, timeout: int = 60) -> str:
    return _shell(f'run-as {PKG} {cmd}', timeout=timeout)


def _pid(package: str) -> Optional[int]:
    out = _shell(f'pidof {package}')
    return int(out.split()[0]) if out.strip() and out.split()[0].isdigit() else None


def _foreground() -> str:
    # Grep on the device: the full dump is large and adb sometimes cuts it
    # ("Broken pipe"), which returned '' mid-game on 2026-09-20.
    out = _shell('dumpsys activity activities 2>/dev/null | grep -m1 topResumedActivity')
    m = re.search(r'topResumedActivity=ActivityRecord\{\S+ u0 (\S+)', out)
    if m:
        return m.group(1).rstrip('}')
    out = _shell('dumpsys window 2>/dev/null | grep -m1 mCurrentFocus')
    m = re.search(r'Window\{\S+ u0 (\S+)\}', out)
    return m.group(1) if m else ''


def _temps() -> dict:
    temps: dict = {}
    gpu = _shell('cat /sys/class/kgsl/kgsl-3d0/temp').strip()
    if gpu.isdigit():
        temps['gpu_c'] = int(gpu) / 1000.0
    zones = _shell('for z in /sys/class/thermal/thermal_zone*; do echo "$(cat $z/type 2>/dev/null) $(cat $z/temp 2>/dev/null)"; done')
    hottest = 0.0
    for line in zones.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip('-').isdigit():
            t = int(parts[1]) / 1000.0
            if re.match(r'(cpu|gpu|cpuss)', parts[0]) and t > hottest:
                hottest = t
    if hottest:
        temps['hottest_cpu_gpu_zone_c'] = hottest
    busy = _shell('cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage').strip()
    if busy:
        temps['gpu_busy'] = busy
    return temps


def _battery() -> dict:
    out = _shell('dumpsys battery')
    level = re.search(r'level: (\d+)', out)
    status = re.search(r'status: (\d+)', out)
    usb = re.search(r'USB powered: (\w+)', out)
    return {
        'level': int(level.group(1)) if level else None,
        'charging': status is not None and status.group(1) in ('2', '5'),
        'usb_powered': usb.group(1) == 'true' if usb else None,
    }


def _stamp() -> str:
    return datetime.datetime.now().strftime('%Y%m%d-%H%M%S')


@mcp.tool()
def xenia_device_status() -> str:
    """Connection, running emulators, foreground app, temperatures, battery."""
    devices = _run(['adb', 'devices'])[1]
    connected = any(SERIAL in line and 'device' in line for line in devices.splitlines())
    status = {
        'serial': SERIAL,
        'connected': connected,
        'xenia_pid': _pid(PKG) if connected else None,
        'other_emulators': {p: _pid(p) for p in OTHER_EMULATORS} if connected else {},
        'foreground': _foreground() if connected else '',
        'temps': _temps() if connected else {},
        'battery': _battery() if connected else {},
    }
    return json.dumps(status, indent=2)


@mcp.tool()
def xenia_preflight(max_temp_c: float = 55.0, min_battery: int = 30) -> str:
    """Apply the device rules before a launch. Returns ok=false with reasons if
    the device is busy, hot, or low on battery. The device is shared with other
    sessions; never launch when another emulator runs."""
    reasons = []
    if not any(SERIAL in line and 'device' in line for line in _run(['adb', 'devices'])[1].splitlines()):
        return json.dumps({'ok': False, 'reasons': ['device not connected']})
    if _pid(PKG):
        reasons.append('xenia is already running')
    for p in OTHER_EMULATORS:
        if _pid(p):
            reasons.append(f'another emulator is running: {p}')
    fg = _foreground()
    if fg and not fg.startswith('com.android.launcher') and PKG not in fg:
        reasons.append(f'foreground app is {fg}; someone may be using the device')
    temps = _temps()
    # The device rule is the kgsl GPU temperature. The CPU zones idle near
    # 57 C on this SoC, so they are reported but only gate when kgsl is absent.
    hot = temps.get('gpu_c', temps.get('hottest_cpu_gpu_zone_c', 0.0))
    if hot > max_temp_c:
        reasons.append(f'gpu {hot:.1f} C is above {max_temp_c} C')
    bat = _battery()
    if bat['level'] is not None and bat['level'] < min_battery and not bat['charging']:
        reasons.append(f'battery {bat["level"]}% and not charging')
    # A sleeping panel looks like a render bug and slows the load (57 s instead
    # of 15 s on 2026-09-20). The rules forbid adb keyevents, so the user wakes it.
    wake = _shell('dumpsys power | grep -m1 mWakefulness=').strip()
    if wake and 'Awake' not in wake:
        reasons.append(f'screen is asleep ({wake}); press the power button')
    return json.dumps({'ok': not reasons, 'reasons': reasons, 'temps': temps,
                       'battery': bat, 'foreground': fg}, indent=2)


@mcp.tool()
def xenia_launch(target: str, force_stop_first: bool = True,
                 skip_preflight: bool = False) -> str:
    """Launch a title exactly as the play button does.

    Sends the launcher's ACTION_VIEW intent with the title path, so the app
    applies its own game profile, optimizations, and the Turnip driver. No cvar
    extras are passed (CLAUDE.md directive 17): what this runs is what the user
    gets from the app. target: absolute path of the ISO, XEX, or content dir on
    the device."""
    if not skip_preflight:
        pre = json.loads(xenia_preflight())
        if not pre['ok']:
            return json.dumps({'launched': False, 'preflight': pre}, indent=2)
    if force_stop_first:
        _shell(f'am force-stop {PKG}')
        time.sleep(1)
    uri = 'file://' + urllib.parse.quote(target)
    cmd = ' '.join(shlex.quote(a) for a in
                   ['am', 'start', '-n', LAUNCHER, '-a', 'android.intent.action.VIEW', '-d', uri])
    out = _shell(cmd)
    deadline = time.time() + 15
    pid = None
    while time.time() < deadline and pid is None:
        time.sleep(1)
        pid = _pid(PKG)
    return json.dumps({'launched': pid is not None, 'pid': pid, 'am_start': out,
                       'command': cmd, 'battery': _battery()}, indent=2)


BUTTON_KEYCODES = {
    'A': 96, 'B': 97, 'X': 99, 'Y': 100, 'DPAD_UP': 19, 'DPAD_DOWN': 20,
    'DPAD_LEFT': 21, 'DPAD_RIGHT': 22, 'START': 108, 'BACK': 109, 'LB': 102,
    'RB': 103, 'LT': 104, 'RT': 105, 'LS': 106, 'RS': 107, 'GUIDE': 110,
}


def _press(button: str, hold_ms: int) -> str:
    """Send one debug gamepad key through the app's exported broadcast
    receiver (EmulatorActivity.ACTION_DEBUG_GAMEPAD_KEY). The key goes through
    nativeOnAndroidGamepadKey like a real pad, so it works on a play-button
    launch with no extras. Refuses unless our EmulatorActivity is in front, so
    it can never reach another session's app (the adb keyevent mistake)."""
    code = BUTTON_KEYCODES.get(button.upper())
    if code is None:
        return f'unknown button {button}; use one of {sorted(BUTTON_KEYCODES)}'
    fg = _foreground()
    if 'jp.xenia.emulator.EmulatorActivity' not in fg:
        return f'refused: foreground is {fg}, not the emulator'
    hold = max(1, min(2000, int(hold_ms)))
    out = _shell(f'am broadcast --receiver-foreground -a {PKG}.DEBUG_GAMEPAD_KEY -p {PKG} '
                 f'--ei key_code {code} --ei hold_ms {hold} --ez already_mapped true')
    ok = 'Broadcast completed' in out
    return f'{button.upper()} ({code}) held {hold} ms: {"sent" if ok else out.strip()[:120]}'


@mcp.tool()
def xenia_press(button: str, hold_ms: int = 120) -> str:
    """Press one gamepad button in the running game: A, B, X, Y, DPAD_UP/DOWN/
    LEFT/RIGHT, START, BACK, LB, RB, LT, RT, LS, RS, GUIDE. Goes through the app's
    debug gamepad receiver, never through adb input keyevent."""
    return _press(button, hold_ms)


@mcp.tool()
def xenia_route(sequence: str, default_wait_ms: int = 800) -> str:
    """Replay a button route in the running game. sequence is space separated:
    a button name presses it (default hold 120 ms), NAME:hold_ms sets the hold,
    and a bare number waits that many milliseconds. Example:
    "START wait 3000 A A:400 2000 DPAD_DOWN A". After each press the default
    wait applies unless a number follows. Stops at the first refused press."""
    log = []
    tokens = sequence.split()
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        i += 1
        if tok.lower() == 'wait':
            continue
        if tok.isdigit():
            time.sleep(int(tok) / 1000.0)
            log.append(f'wait {tok}')
            continue
        name, _, hold = tok.partition(':')
        result = _press(name, int(hold) if hold.isdigit() else 120)
        log.append(result)
        if 'refused' in result or 'unknown' in result:
            break
        explicit_wait = i < len(tokens) and tokens[i].isdigit()
        if not explicit_wait:
            time.sleep(default_wait_ms / 1000.0)
    return '\n'.join(log)


@mcp.tool()
def xenia_force_stop(disconnect_wifi_adb: bool = False) -> str:
    """Force-stop the emulator and verify it is gone. Use after every run."""
    _shell(f'am force-stop {PKG}')
    time.sleep(1)
    result = {'stopped': _pid(PKG) is None}
    if disconnect_wifi_adb:
        result['disconnect'] = _run(['adb', 'disconnect'])[1]
    return json.dumps(result)


@mcp.tool()
def xenia_logcat(pattern: str = '', lines: int = 200, xenia_only: bool = True,
                 save: bool = True) -> str:
    """Dump the device log. pattern is a regex filter; xenia_only restricts to
    the xenia tag. The full dump is saved under scratch/mcp when save is true."""
    args = ['logcat', '-d', '-v', 'time']
    if xenia_only:
        args += ['-s', 'xenia:*', 'xenia-aot:*', 'xenia-uiwatchdog:*', 'DEBUG:*']
    out = _adb(*args, timeout=120)
    if save:
        os.makedirs(SCRATCH, exist_ok=True)
        path = os.path.join(SCRATCH, f'logcat-{_stamp()}.txt')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(out)
    else:
        path = ''
    rows = out.splitlines()
    if pattern:
        rx = re.compile(pattern)
        rows = [r for r in rows if rx.search(r)]
    tail = rows[-lines:]
    header = f'{len(rows)} matching lines, showing last {len(tail)}' + (f'; full dump: {path}' if path else '')
    return header + '\n' + '\n'.join(tail)


@mcp.tool()
def xenia_logcat_clear() -> str:
    """Clear the device log before a run so the next dump starts clean."""
    return _adb('logcat', '-c') or 'cleared'


@mcp.tool()
def xenia_aot_progress() -> str:
    """Precompile state from the log: the last progress line, the pre-warm
    marker, and the done marker. Also the process state."""
    out = _adb('logcat', '-d', '-s', 'xenia:*', 'xenia-aot:*', timeout=120)
    progress = [l for l in out.splitlines() if 'AOT precompile progress' in l]
    prewarm = [l for l in out.splitlines() if 'load-window pre-warm on' in l]
    done = [l for l in out.splitlines() if 'pre-warmed' in l and 'function(s)' in l]
    overlay = [l for l in out.splitlines() if 'xenia-aot' in l]
    return json.dumps({
        'pid': _pid(PKG),
        'progress_lines': len(progress),
        'last_progress': progress[-1] if progress else None,
        'prewarm_markers': prewarm[-2:],
        'done_markers': done[-2:],
        'overlay_log': overlay[-4:],
    }, indent=2)


@mcp.tool()
def xenia_fps(window_lines: int = 400) -> str:
    """Recent GPU pass timing lines (gpu_frame_us, gpu_pass_us, between_us) and
    the title name. Gate on verts > 50000 before quoting a Blue Dragon number."""
    out = _adb('logcat', '-d', '-s', 'xenia:*', timeout=120)
    rows = out.splitlines()
    title = [l for l in rows if 'Title name:' in l]
    timing = [l for l in rows if 'GPU pass timing' in l][-window_lines:]
    frames = []
    for l in timing:
        m = re.search(r'gpu_frame_us=(\d+) gpu_pass_us=(\d+) between_us=(\d+)', l)
        if m:
            frames.append(tuple(int(x) for x in m.groups()))
    summary = {}
    if frames:
        fr = sorted(f[0] for f in frames)
        med = fr[len(fr) // 2]
        summary = {'samples': len(frames), 'median_gpu_frame_us': med,
                   'median_fps_from_gpu_frame': round(1e6 / med, 1) if med else None}
    return json.dumps({'title': title[-1] if title else None, 'summary': summary,
                       'last_lines': timing[-5:]}, indent=2)


@mcp.tool()
def xenia_screenshot(name: str = '') -> str:
    """Capture the screen to scratch/mcp and report the foreground package, so
    a capture of another session's app is never mistaken for ours."""
    os.makedirs(SCRATCH, exist_ok=True)
    fg = _foreground()
    path = os.path.join(SCRATCH, f'{name or "screen"}-{_stamp()}.png')
    _shell('screencap -p /data/local/tmp/xe_shot.png')
    _adb('pull', '/data/local/tmp/xe_shot.png', path)
    _shell('rm -f /data/local/tmp/xe_shot.png')
    return json.dumps({'path': path, 'foreground': fg, 'ours': PKG in fg})


@mcp.tool()
def xenia_backtrace(pid: Optional[int] = None) -> str:
    """Native backtrace of every thread in the emulator process (debuggerd -b).
    Use it when the UI watchdog reports the main thread wedged.
    debuggerd needs root, which the adb shell user lacks on this device
    (2026-09-20). When it refuses, this returns the per-thread table from
    /proc instead: state, CPU ticks, voluntary context switches, wait channel.
    A main thread with few ticks and few switches while workers run is a
    blocked main thread."""
    pid = pid or _pid(PKG)
    if not pid:
        return 'xenia is not running'
    out = _shell(f'debuggerd -b {pid}', timeout=120)
    if 'root is required' in out or not out.strip():
        script = (
            f'for t in /proc/{pid}/task/*; do '
            'tid=${t##*/}; '
            'st=$(sed "s/.*) //" $t/stat | cut -d" " -f1,12,13); '
            'vs=$(grep voluntary_ctxt $t/status | head -1 | cut -f2); '
            'wc=$(cat $t/wchan 2>/dev/null); '
            'echo "$tid $(cat $t/comm) $st $vs $wc"; done')
        table = _shell(script, timeout=60)
        rows = [r for r in table.splitlines() if r.strip()]
        header = 'tid comm state utime_ticks stime_ticks vol_switches wchan'
        return ('debuggerd refused: root is required. /proc thread table '
                f'(main thread is tid {pid}):\n{header}\n' + '\n'.join(rows))
    os.makedirs(SCRATCH, exist_ok=True)
    path = os.path.join(SCRATCH, f'backtrace-{pid}-{_stamp()}.txt')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(out)
    main = out.split('\n\n')[0] if out else ''
    return f'saved {path}\n{main[:4000]}'


@mcp.tool()
def xenia_threads(pid: Optional[int] = None, top: int = 20) -> str:
    """Per-thread CPU use, nice value and name of the emulator process."""
    pid = pid or _pid(PKG)
    if not pid:
        return 'xenia is not running'
    out = _shell(f'ps -T -p {pid} -o TID,NI,PCPU,PRI,NAME')
    rows = out.splitlines()
    body = sorted(rows[1:], key=lambda r: -float(r.split()[2]) if len(r.split()) > 2 and r.split()[2].replace('.', '').isdigit() else 0)
    return '\n'.join(rows[:1] + body[:top])


@mcp.tool()
def xenia_memory(pid: Optional[int] = None) -> str:
    """Where the memory goes: RSS, PSS, native heap, and the size of the object
    cache and code caches under the app files directory."""
    pid = pid or _pid(PKG)
    result: dict = {}
    if pid:
        mem = _shell(f'dumpsys meminfo {pid}')
        for key in ('TOTAL PSS', 'TOTAL RSS', 'Native Heap', 'Graphics', 'Code', 'Stack', 'Unknown'):
            m = re.search(rf'^\s*{re.escape(key)}[:\s]+(\d+)', mem, re.M)
            if m:
                result[key] = f'{int(m.group(1)) / 1024:.1f} MB'
        statm = _shell(f'cat /proc/{pid}/statm').split()
        if len(statm) >= 2:
            result['rss_mb_statm'] = round(int(statm[1]) * 4096 / 1048576, 1)
    du = _run_as('du -sk files/* 2>/dev/null')
    sizes = {}
    for line in du.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0].isdigit():
            sizes[parts[1]] = f'{int(parts[0]) / 1024:.1f} MB'
    result['files_dir'] = sizes
    return json.dumps(result, indent=2)


PREFS_FILE = 'shared_prefs/xenia_android_settings.xml'
OPTIMIZATIONS_JAVA = os.path.join(REPO, 'android', 'android_studio_project', 'app', 'src', 'main',
                                  'java', 'jp', 'xenia', 'emulator', 'XeniaOptimizations.java')


def _toggle_catalog() -> list[dict]:
    """The app's optimization toggles: key, default, category, and the cvars each sets.
    Parsed from XeniaOptimizations.java so the list never drifts from the app."""
    text = open(OPTIMIZATIONS_JAVA, encoding='utf-8').read()
    out = []
    for m in re.finditer(r'new Optimization\(\s*"([^"]+)",\s*"([^"]*)",(.*?)\)\);', text, re.S):
        key, title, body = m.group(1), m.group(2), m.group(3)
        flags = re.findall(r',\s*(true|false)\s*,\s*(true|false)\s*,', body)
        default = flags[0][0] == 'true' if flags else None
        bools = re.findall(r'new BoolCvar\("([^"]+)"', body)
        ints = re.findall(r'new IntCvar\("([^"]+)",\s*(-?\d+)\)', body)
        out.append({'key': key, 'title': title, 'default': default,
                    'cvars': bools + [f'{k}={v}' for k, v in ints]})
    return out


def _device_prefs() -> str:
    return _run_as(f'cat {PREFS_FILE}')


def _pref_bools(xml: str) -> dict:
    return {k: v == 'true' for k, v in re.findall(r'<boolean name="([^"]+)" value="(true|false)"', xml)}


@mcp.tool()
def xenia_toggles() -> str:
    """Every optimization toggle in the app menu: key, title, default, the value
    set on the device, and the cvars it drives. This is the control surface
    (CLAUDE.md directive 17): set these, never cvars."""
    prefs = _pref_bools(_device_prefs())
    rows = []
    for t in _toggle_catalog():
        rows.append({**t, 'device': prefs.get(t['key'], t['default'])})
    return json.dumps(rows, indent=1)


@mcp.tool()
def xenia_toggle_set(key: str, enabled: bool) -> str:
    """Set one app menu toggle on the device, exactly as tapping it in Settings.
    Refuses while the app runs (the app caches preferences in memory). The
    next launch from the play button or xenia_launch picks it up."""
    keys = {t['key'] for t in _toggle_catalog()}
    if key not in keys:
        return json.dumps({'ok': False, 'reason': f'{key} is not an app toggle', 'known': sorted(keys)})
    if _pid(PKG):
        return json.dumps({'ok': False, 'reason': 'xenia is running; force-stop first'})
    xml = _device_prefs()
    if '<map' not in xml:
        return json.dumps({'ok': False, 'reason': 'could not read the preferences file'})
    value = 'true' if enabled else 'false'
    rx = re.compile(r'<boolean name="' + re.escape(key) + r'" value="(true|false)" />')
    if rx.search(xml):
        xml = rx.sub(f'<boolean name="{key}" value="{value}" />', xml)
    else:
        xml = xml.replace('</map>', f'    <boolean name="{key}" value="{value}" />' + chr(10) + '</map>')
    os.makedirs(SCRATCH, exist_ok=True)
    local = os.path.join(SCRATCH, 'xenia_android_settings.xml')
    with open(local, 'w', encoding='utf-8', newline='') as f:
        f.write(xml)
    tmp = '/data/local/tmp/xe_prefs.xml'
    _adb('push', local, tmp)
    _shell(f'run-as {PKG} cp {tmp} {PREFS_FILE}')
    _shell(f'rm -f {tmp}')
    now = _pref_bools(_device_prefs()).get(key)
    return json.dumps({'ok': now == enabled, 'key': key, 'device': now})


@mcp.tool()
def xenia_config_get(key: str = '') -> str:
    """Read the persisted device config (files/xenia.config.toml). It overrides
    compiled defaults; only intent extras beat it. Empty key returns all lines."""
    out = _run_as('cat files/xenia.config.toml')
    if not key:
        return out
    rows = [l for l in out.splitlines() if l.startswith(key)]
    return '\n'.join(rows) or f'{key} not in config'


@mcp.tool()
def xenia_config_set(key: str, value: str) -> str:
    """Set one key in the persisted device config. value is written as given,
    so quote strings ("\"x\"") and use true/false for booleans."""
    current = _run_as('cat files/xenia.config.toml')
    rows = current.splitlines()
    found = False
    for i, l in enumerate(rows):
        if re.match(rf'^{re.escape(key)}\s*=', l):
            rest = l.split('#', 1)
            comment = ('  #' + rest[1]) if len(rest) > 1 else ''
            rows[i] = f'{key} = {value}{comment}'
            found = True
    if not found:
        return f'{key} not in config; refusing to add an unknown key'
    tmp = '/data/local/tmp/xenia.config.toml'
    os.makedirs(SCRATCH, exist_ok=True)
    local = os.path.join(SCRATCH, 'xenia.config.toml')
    with open(local, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(rows) + '\n')
    _adb('push', local, tmp)
    _shell(f'run-as {PKG} cp {tmp} files/xenia.config.toml')
    _shell(f'rm -f {tmp}')
    return xenia_config_get(key)


@mcp.tool()
def xenia_build(mode: str = 'NativeCore') -> str:
    """Run tools/thor/thor_build.ps1. Modes: NativeCore (NDK, ~7-12 min),
    ApkShell (package, seconds), FullApk, Install, FullDeploy, ApkShellDeploy."""
    os.makedirs(SCRATCH, exist_ok=True)
    log = os.path.join(SCRATCH, f'build-{mode}-{_stamp()}.log')
    cmd = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', BUILD_SCRIPT, '-Mode', mode]
    if mode.endswith('Deploy') or mode == 'Install':
        cmd += ['-DeviceSerial', SERIAL]
    t = time.time()
    with open(log, 'w', encoding='utf-8') as f:
        proc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=REPO, timeout=3600)
    with open(log, encoding='utf-8', errors='replace') as f:
        text = f.read()
    hits = [l for l in text.splitlines() if re.search(r' error:|BUILD SUCCESSFUL|BUILD FAILED|What went wrong|Success|Failure', l)]
    return json.dumps({'mode': mode, 'exit': proc.returncode, 'seconds': int(time.time() - t),
                       'log': log, 'lines': hits[-10:]}, indent=2)


@mcp.tool()
def xenia_install(verify: bool = True) -> str:
    """Install the last built APK (ApkShellDeploy) and verify the hash on device.
    Refuses while xenia or another emulator runs."""
    pre = json.loads(xenia_preflight(max_temp_c=200, min_battery=0))
    busy = [r for r in pre['reasons'] if 'running' in r or 'using the device' in r]
    if busy:
        return json.dumps({'installed': False, 'reasons': busy})
    result = json.loads(xenia_build('ApkShellDeploy'))
    if result['exit'] != 0 or not verify:
        return json.dumps(result, indent=2)
    # sha256sum on Git Bash prefixes a backslash when the path had one.
    local = _run(['sha256sum', APK])[1].split()[0].lstrip('\\')[:16] if os.path.exists(APK) else ''
    apk_dev = _shell(f'pm path {PKG}').strip().replace('package:', '')
    dev = _shell(f'sha256sum {apk_dev}').split()[0][:16]
    result['sha256_local'] = local
    result['sha256_device'] = dev
    result['match'] = local == dev
    return json.dumps(result, indent=2)


@mcp.tool()
def xenia_git_head() -> str:
    """Commit the working tree is at, for citing builds."""
    return _run(['git', '-C', REPO, 'log', '-1', '--format=%h %cd %s', '--date=short'])[1]


if __name__ == '__main__':
    mcp.run()
