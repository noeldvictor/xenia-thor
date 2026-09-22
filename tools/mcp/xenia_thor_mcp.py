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
            if parts[0] == 'xo-therm':
                # The case sensor: the shell heats over several runs and a
                # warm case reaches the 70 C GPU abort within 45 s of play.
                temps['case_c'] = t
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


# ---- the MCP inside the emulator (DebugServer.java, port 41337) -------------
# The emulator process is itself an MCP server (http://127.0.0.1:41337/mcp,
# registered in .mcp.json as "xenia-thor-device", reached through
# `adb forward tcp:41337 tcp:41337` or wifi). Claude Code calls its tools
# directly: status, fps, threads, log, memory, disasm, gpu, stall,
# screenshot, toggles, toggle_set, cvar_get, cvar_set, press, route, pause,
# stop. This PC server keeps only what the app cannot do to itself: device
# status and preflight, the launch intent, force-stop, build and install,
# simpleperf, the tombstones after a death, the probe workflow. The helpers
# below let tools/thor scripts call the same endpoints from Python.
API_PORT = 41337
PERF_MAP_DEVICE_PATH = f'/data/data/{PKG}/files/perf.map'
API_HOST = os.environ.get('XE_THOR_API_HOST', '127.0.0.1')
_api_forwarded = False


def _api_url(path: str) -> str:
    return f'http://{API_HOST}:{API_PORT}{path}'


def _api(path: str, method: str = 'GET', timeout: int = 20, raw: bool = False):
    """One call to the in-app debug API. Returns parsed JSON (or bytes when
    raw). Raises RuntimeError with the reason when the app is not reachable."""
    import urllib.request
    import urllib.error
    global _api_forwarded
    if API_HOST == '127.0.0.1' and not _api_forwarded:
        _run(['adb', '-s', SERIAL, 'forward', f'tcp:{API_PORT}', f'tcp:{API_PORT}'])
        _api_forwarded = True
    # Quote the query values (a grep with a space broke the probe once).
    if '?' in path:
        base, _, qs = path.partition('?')
        pairs = [(k, v) for k, _, v in (p.partition('=') for p in qs.split('&')) if k]
        path = base + '?' + urllib.parse.urlencode(pairs)
    req = urllib.request.Request(_api_url(path), method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body = r.read()
    except (urllib.error.URLError, ConnectionError, OSError) as e:
        raise RuntimeError(f'debug API not reachable at {_api_url(path)}: {e}; '
                           'is the emulator running (xenia_launch)?')
    if raw:
        return body
    text = body.decode('utf-8', errors='replace')
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return {'text': text[-4000:]}


def _api_up() -> bool:
    try:
        _api('/', timeout=3)
        return True
    except RuntimeError:
        return False


@mcp.tool()
def xenia_api(path: str, method: str = 'GET') -> str:
    """Any endpoint of the in-app debug server: /status /fps /threads
    /log?lines=&grep= /memory?addr=&len= /disasm?addr=&count= /gpu /stall
    /toggles /cvar?name= (POST /cvar?name=&value=) POST /toggle?key=&enabled=1
    POST /press?button=&hold_ms= POST /route?seq=START:150,wait:800,A
    POST /pause?on=1 POST /stop. The server runs inside the emulator process;
    this reaches it through adb forward or wifi (XE_THOR_API_HOST)."""
    try:
        r = _api(path, method)
    except RuntimeError as e:
        return json.dumps({'error': str(e)})
    return json.dumps(r, indent=2)[:12000]


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
def xenia_preflight(max_temp_c: float = 55.0, min_battery: int = 30, max_case_c: float = 41.0) -> str:
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
    case = temps.get('case_c', 0.0)
    if case > max_case_c:
        reasons.append(f'case {case:.1f} C is above {max_case_c} C; the run would hit the 70 C abort within a minute')
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
    _run(['adb', '-s', SERIAL, 'forward', f'tcp:{API_PORT}', f'tcp:{API_PORT}'])
    if not skip_preflight:
        pre = json.loads(xenia_preflight())
        if not pre['ok']:
            return json.dumps({'launched': False, 'preflight': pre}, indent=2)
    if force_stop_first:
        _shell(f'am force-stop {PKG}')
        time.sleep(1)
    # A short title key (bd, banjo) resolves to its device path; a bare key
    # passed as a path once started the emulator with no game (2026-09-20).
    target = TITLES.get(target, target)
    uri = 'file://' + urllib.parse.quote(target)
    cmd = ' '.join(shlex.quote(a) for a in
                   ['am', 'start', '-n', LAUNCHER, '-a', 'android.intent.action.VIEW', '-d', uri])
    out = _shell(cmd)
    deadline = time.time() + 15
    pid = None
    while time.time() < deadline and pid is None:
        time.sleep(1)
        pid = _pid(PKG)
    # Diagnostic, not a lever: the a64 backend appends a perf-map line per
    # function it emits from now on, so simpleperf samples in a64 code resolve
    # to guest functions (LLVM functions come from the log). Set through the
    # in-app server as soon as it answers; the launch itself is unchanged.
    perf_map = ''
    if pid:
        for _ in range(20):
            try:
                _api(f'/cvar?name=cpu_perf_map_path&value={PERF_MAP_DEVICE_PATH}', 'POST', timeout=2)
                perf_map = PERF_MAP_DEVICE_PATH
                break
            except RuntimeError:
                time.sleep(0.5)
    return json.dumps({'launched': pid is not None, 'pid': pid, 'am_start': out,
                       'command': cmd, 'battery': _battery(), 'perf_map': perf_map}, indent=2)


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


# device MCP serves this; kept for tools/thor scripts
def xenia_press(button: str, hold_ms: int = 120) -> str:
    """Press one gamepad button in the running game: A, B, X, Y, DPAD_UP/DOWN/
    LEFT/RIGHT, START, BACK, LB, RB, LT, RT, LS, RS, GUIDE. Goes through the app's
    debug gamepad receiver, never through adb input keyevent."""
    try:
        return json.dumps(_api(f'/press?button={button}&hold_ms={hold_ms}', 'POST'))
    except RuntimeError:
        return _press(button, hold_ms)


# device MCP serves this; kept for tools/thor scripts
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


TITLES = {
    'bd': ('/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/'
           'Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'),
    'banjo': ('/storage/2664-21DE/Roms/xbox360/Banjo-Kazooie - Nuts & Bolts (USA) '
              '(En,Ja,Fr,De,Es,It,Nl,Sv,No,Zh,Ko,Pl,Ru,Cs).iso'),
}
NDK = os.environ.get('XE_NDK') or os.path.expanduser(
    '~/AppData/Local/Android/Sdk/ndk/25.0.8775105')
PPC_OBJDUMP = os.path.join(REPO, 'third_party', 'binutils-ppc-cygwin',
                           'powerpc-none-elf-objdump.exe')
HOST_SIMPLEPERF = os.path.join(NDK, 'simpleperf', 'bin', 'windows', 'x86_64', 'simpleperf.exe')
DEVICE_SIMPLEPERF = os.path.join(NDK, 'simpleperf', 'bin', 'android', 'arm64', 'simpleperf')


def _title_path(title: str) -> str:
    return TITLES.get(title, title)


@mcp.tool()
def xenia_crash(pid: Optional[int] = None) -> str:
    """The crash picture of the current or last run: the a64 crash diagnostic
    ("A64 CRASH DIAG": guest function, nearest function, guest lr and r3-r6),
    unhandled host faults and fault storms, guest crash dialogs, the last
    tombstone from the crash buffer, and the newest app crash report."""
    out = {}
    log = _adb('logcat', '-d', '-s', 'xenia:*', 'xenia-fault:*', timeout=120)
    keys = ('A64 CRASH DIAG', 'UNHANDLED host fault', 'UNHANDLED fault STORM',
            'guest crash', 'Fatal', 'GPU is hung', 'has waited 60s')
    hits = [l[l.find(' I ') + 3:].strip() if ' I ' in l else l.strip()
            for l in log.splitlines() if any(k in l for k in keys)]
    out['log_lines'] = hits[:3] + (['...'] if len(hits) > 6 else []) + hits[-3:] if len(hits) > 6 else hits
    crash = _adb('logcat', '-b', 'crash', '-d', timeout=60)
    frames = [l[l.find('F DEBUG'):].strip() for l in crash.splitlines()
              if 'F DEBUG' in l and ('signal ' in l or ' pc ' in l or 'pid:' in l)]
    out['tombstone'] = frames[-14:]
    newest = _run_as('sh -c "ls -t files/crash_logs 2>/dev/null | head -1"').strip()
    if newest:
        out['crash_report'] = newest
        out['crash_report_head'] = _run_as(f'head -c 600 files/crash_logs/{newest}')[:600]
    return json.dumps(out, indent=2)


def _patch_files(title_id: str):
    names = _run_as('ls files/patches').split()
    return [n for n in names if n.lower().startswith(title_id.lower()) and n.endswith('.patch.toml')]


@mcp.tool()
def xenia_patches(title_id: str) -> str:
    """List the game patch files on the device for a title id (8 hex) and each
    [[patch]] with its enabled state. The same view as the Game Patches screen."""
    result = {}
    for f in _patch_files(title_id):
        text = _run_as(f'cat files/patches/{f}')
        entries = []
        for block in text.split('[[patch]]')[1:]:
            name = re.search(r'name = "([^"]+)"', block)
            enabled = re.search(r'is_enabled = (true|false)', block)
            entries.append({'name': name.group(1) if name else '?',
                            'enabled': enabled.group(1) == 'true' if enabled else None})
        result[f] = entries
    return json.dumps(result, indent=2)


@mcp.tool()
def xenia_patch_set(title_id: str, name: str, enabled: bool) -> str:
    """Enable or disable one [[patch]] by name in the title's patch files on the
    device, exactly as the Game Patches screen does (rewrites is_enabled)."""
    for f in _patch_files(title_id):
        text = _run_as(f'cat files/patches/{f}')
        blocks = text.split('[[patch]]')
        changed = False
        for i in range(1, len(blocks)):
            m = re.search(r'name = "([^"]+)"', blocks[i])
            if m and m.group(1) == name:
                blocks[i] = re.sub(r'is_enabled = (true|false)',
                                   f'is_enabled = {"true" if enabled else "false"}', blocks[i], count=1)
                changed = True
        if changed:
            local = os.path.join(SCRATCH, 'patch_set.toml')
            os.makedirs(SCRATCH, exist_ok=True)
            with open(local, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write('[[patch]]'.join(blocks))
            _adb('push', local, '/data/local/tmp/patch_set.toml')
            _run_as(f'cp /data/local/tmp/patch_set.toml files/patches/{f}')
            _shell('rm -f /data/local/tmp/patch_set.toml')
            return f'{f}: "{name}" -> {"enabled" if enabled else "disabled"}'
    return f'no patch named "{name}" for {title_id}'


# device MCP serves this; kept for tools/thor scripts
def xenia_guest_dump(title: str = 'bd', base: int = 0x82000000, size_mb: int = 8,
                     at_guest_ms: int = 3000, out_name: str = '') -> str:
    """Dump guest memory of a title to a local file: sets the diagnostic
    dump_guest_mem cvars as launch cvars (the persisted config is never read
    on Android), launches through the play-button path, waits for the dump
    line, pulls the file to scratch/mcp/, clears the launch cvars,
    force-stops. The dump is game content: it stays under scratch/. title:
    bd, banjo, or a device path. base 0x821E0000 size 12 (MB) is Banjo's
    whole code section for tools/thor/guest_disasm_offline.py."""
    keys = {'dump_guest_mem_at_ms': str(at_guest_ms), 'dump_guest_mem_size_mb': str(size_mb),
            'dump_guest_mem_base': str(base),
            'dump_guest_mem_path': f'/data/data/{PKG}/files/gm.bin'}
    xenia_launch_cvars(clear=True)
    xenia_launch_cvars(set=','.join(f'{k}={v}' for k, v in keys.items()))
    result = 'time limit'
    try:
        xenia_logcat_clear()
        launch = json.loads(xenia_launch(_title_path(title)))
        if not launch.get('launched'):
            return json.dumps(launch)
        pid = launch['pid']
        t0 = time.time()
        while time.time() - t0 < 900:
            time.sleep(5)
            if _pid(PKG) != pid:
                result = 'died'; break
            log = _shell(f'logcat -d --pid={pid} -s xenia', timeout=120)
            if 'guest-mem dump' in log:
                result = 'dumped'; break
    finally:
        xenia_force_stop()
        xenia_launch_cvars(clear=True)
    if result != 'dumped':
        return json.dumps({'result': result})
    os.makedirs(SCRATCH, exist_ok=True)
    path = os.path.join(SCRATCH, out_name or f'guest-{title}-{base:08X}-{_stamp()}.bin')
    data = subprocess.run(['adb', '-s', SERIAL, 'exec-out', f'run-as {PKG} cat files/gm.bin'],
                          capture_output=True, timeout=300).stdout
    with open(path, 'wb') as fh:
        fh.write(data)
    _run_as('rm -f files/gm.bin')
    return json.dumps({'result': result, 'path': path, 'bytes': len(data)})


# device MCP serves this; kept for tools/thor scripts
def xenia_disasm(dump_path: str, address: int, count: int = 24, base: int = 0x82000000) -> str:
    """Disassemble PowerPC guest code from a guest memory dump (xenia_guest_dump)
    around a guest address: count instructions from address. Uses the bundled
    powerpc-none-elf-objdump."""
    start = address
    stop = address + count * 4
    cmd = [PPC_OBJDUMP, '-D', '-b', 'binary', '-m', 'powerpc:common', '-EB',
           f'--adjust-vma={base:#x}', f'--start-address={start:#x}',
           f'--stop-address={stop:#x}', dump_path]
    code, out = _run(cmd, timeout=120)
    rows = [l for l in out.splitlines() if re.match(r'\s*[0-9a-f]{8}:', l)]
    return '\n'.join(rows) or out[-500:]


@mcp.tool()
def xenia_probe(title: str = 'banjo', seconds: int = 120, shot_every: int = 30,
                route: str = '') -> str:
    """Launch a title through the play-button path, wait for the load, then a
    screenshot timeline with fps per interval and an optional button route
    ("<s after load>:<BUTTON>[:hold_ms] ..."), then the crash picture.
    Force-stops at the end. Wraps tools/thor/title_probe.py."""
    cmd = [sys.executable, '-u', os.path.join(REPO, 'tools', 'thor', 'title_probe.py'),
           '--title', title, '--seconds', str(seconds), '--shot-every', str(shot_every)]
    if route:
        cmd += ['--route', route]
    code, out = _run(cmd, timeout=seconds + 1200)
    return out[-6000:]


def _guest_code_map(pid: int) -> list[tuple[int, int]]:
    """Host start addresses of the LLVM-compiled guest functions of this run,
    from the log lines "LLVMobjload guest=0x.. host=0x.." (warm cache) and
    "LLVMmap guest=0x.. host=0x.." (fresh compile). Sorted by host address; a
    function ends where the next one starts."""
    log = _shell(f'logcat -d --pid={pid} -s xenia', timeout=180)
    rows = []
    for mm in re.finditer(r'LLVM(?:objload|map) guest=0x([0-9A-Fa-f]{8}) host=0x([0-9A-Fa-f]{16})', log):
        rows.append((int(mm.group(2), 16), int(mm.group(1), 16)))
    # a64 functions: the perf map the backend writes when cpu_perf_map_path is
    # set (xenia_launch sets it): "<start> <size> guest_<addr>".
    for mm in re.finditer(r'^([0-9a-f]+) [0-9a-f]+ guest_([0-9A-Fa-f]{8})', _run_as('cat files/perf.map 2>/dev/null'), re.M):
        rows.append((int(mm.group(1), 16), int(mm.group(2), 16)))
    rows.sort()
    return rows


def _guest_hot(perf_data: str, pid: int, top: int = 20) -> list[dict]:
    """Samples in anonymous JIT code ("unknown" dso), attributed to guest
    functions through _guest_code_map. Returns the hottest guest functions
    with their share of ALL samples."""
    import bisect
    code, rep = _run([HOST_SIMPLEPERF, 'report', '-i', perf_data, '--sort', 'dso,vaddr_in_file',
                      '-n', '--percent-limit', '0.05'], timeout=300)
    fmap = _guest_code_map(pid)
    starts = [h for h, _ in fmap]
    per_fn: dict[int, float] = {}
    per_fn_samples: dict[int, int] = {}
    unknown_pct = 0.0
    for l in rep.splitlines():
        f = l.split()
        if len(f) < 4 or not f[0].endswith('%') or f[2] != 'unknown':
            continue
        pct = float(f[0][:-1]); samples = int(f[1]); addr = int(f[3], 16)
        unknown_pct += pct
        i = bisect.bisect_right(starts, addr) - 1
        guest = fmap[i][1] if i >= 0 else 0
        per_fn[guest] = per_fn.get(guest, 0.0) + pct
        per_fn_samples[guest] = per_fn_samples.get(guest, 0) + samples
    rows = sorted(per_fn.items(), key=lambda kv: -kv[1])[:top]
    out = [{'guest': f'{g:08X}' if g else 'a64 or unmapped', 'pct': round(v, 2),
            'samples': per_fn_samples[g]} for g, v in rows]
    return [{'jit_total_pct': round(unknown_pct, 2), 'mapped_functions': len(fmap)}] + out


@mcp.tool()
def xenia_profile(seconds: int = 15, simpleperf: bool = True, callgraph: bool = False) -> str:
    """Profile the running emulator for `seconds`: presented fps (xenia-fps),
    GPU busy, temperatures, and per-thread CPU at the start and the end; with
    simpleperf, a CPU sample of the app (debuggable, no root) and the top
    symbols by dso from the host report. JIT'd guest code shows as unnamed
    addresses unless cpu_perf_map_path is set (a64 functions only)."""
    pid = _pid(PKG)
    if not pid:
        return 'xenia is not running'
    os.makedirs(SCRATCH, exist_ok=True)
    result = {'pid': pid, 'seconds': seconds}
    result['threads_start'] = xenia_threads(pid, top=8)
    t0 = time.time()
    perf_proc = None
    if simpleperf:
        if not _shell('ls /data/local/tmp/simpleperf 2>/dev/null').strip():
            _adb('push', DEVICE_SIMPLEPERF, '/data/local/tmp/simpleperf')
            _shell('chmod 755 /data/local/tmp/simpleperf')
        args = ['adb', '-s', SERIAL, 'shell', '/data/local/tmp/simpleperf', 'record',
                '--app', PKG, '--duration', str(seconds), '-f', '2000',
                '-o', '/data/local/tmp/xe_perf.data']
        if callgraph:
            args += ['-g']
        perf_proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, encoding='utf-8', errors='replace')
    samples = []
    while time.time() - t0 < seconds:
        time.sleep(3)
        busy = _shell('cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage').strip()
        temp = _shell('cat /sys/class/kgsl/kgsl-3d0/temp').strip()
        samples.append({'t': round(time.time() - t0), 'gpu_busy': busy,
                        'gpu_c': int(temp) / 1000 if temp.isdigit() else None})
    result['samples'] = samples
    result['threads_end'] = xenia_threads(pid, top=8)
    fps = json.loads(xenia_fps(window_lines=seconds * 2 + 4)).get('presented')
    result['presented_fps'] = fps
    if perf_proc:
        try:
            perf_out = perf_proc.communicate(timeout=seconds + 60)[0]
        except subprocess.TimeoutExpired:
            perf_proc.kill()
            perf_out = 'simpleperf timed out'
        result['simpleperf_record'] = perf_out.strip()[-300:]
        local = os.path.join(SCRATCH, f'perf-{pid}-{_stamp()}.data')
        _adb('pull', '/data/local/tmp/xe_perf.data', local, timeout=300)
        if os.path.exists(local) and os.path.getsize(local) > 0:
            code, rep = _run([HOST_SIMPLEPERF, 'report', '-i', local, '--sort', 'dso,symbol',
                              '-n', '--percent-limit', '0.8'], timeout=300)
            rows = [l for l in rep.splitlines() if l.strip() and not l.startswith('Cmdline')]
            result['perf_data'] = local
            # The device .so is stripped: simpleperf prints libxenia-app.so[+off].
            # Name those offsets with llvm-symbolizer against the unstripped
            # build (the same way xenia_backtrace does).
            try:
                rows = _symbolize_report_rows(rows)
            except Exception as e:  # noqa: BLE001
                result['symbolize'] = f'unavailable: {e}'
            result['top'] = rows[:40]
            code, rep2 = _run([HOST_SIMPLEPERF, 'report', '-i', local, '--sort', 'dso', '-n'],
                              timeout=300)
            result['by_dso'] = [l for l in rep2.splitlines() if '%' in l][:12]
            # Guest functions behind the anonymous JIT samples (LLVM functions
            # by the log's host addresses; a64 functions stay unmapped).
            try:
                result['guest_hot'] = _guest_hot(local, pid)
            except Exception as e:  # a missing report is not a failed profile
                result['guest_hot'] = f'unavailable: {e}'
    return json.dumps(result, indent=2)


def _symbolize_report_rows(rows: list) -> list:
    """Replace libxenia-app.so[+offset] in simpleperf report rows with the
    function and line from the unstripped .so of the last build."""
    import re
    so = os.path.join(REPO, 'android', 'android_studio_project', 'app', 'build', 'intermediates',
                      'ndkBuild', 'githubDebug', 'obj', 'local', 'arm64-v8a', 'libxenia-app.so')
    ndk = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk', 'ndk', '25.0.8775105',
                       'toolchains', 'llvm', 'prebuilt', 'windows-x86_64', 'bin', 'llvm-symbolizer.exe')
    pat = re.compile(r'libxenia-app\.so\[\+([0-9a-f]+)\]')
    offsets = sorted({m.group(1) for row in rows for m in pat.finditer(row)})
    if not offsets or not os.path.exists(so) or not os.path.exists(ndk):
        return rows
    proc = subprocess.run([ndk, '--obj=' + so, '--functions=short', '--inlining=false', '--demangle',
                           '--basenames'] + ['0x' + o for o in offsets],
                          capture_output=True, text=True, timeout=300)
    names = {}
    for o, block in zip(offsets, proc.stdout.strip().split('\n\n')):
        lines = block.strip().split('\n')
        if lines and lines[0] and lines[0] != '??':
            names[o] = lines[0] + (' (' + lines[1] + ')' if len(lines) > 1 and not lines[1].startswith('??') else '')
    out = []
    for row in rows:
        out.append(pat.sub(lambda m: 'libxenia-app.so ' + names.get(m.group(1), '[+' + m.group(1) + ']'), row))
    return out


@mcp.tool()
def xenia_force_stop(disconnect_wifi_adb: bool = False) -> str:
    """Force-stop the emulator and verify it is gone. Use after every run."""
    _shell(f'am force-stop {PKG}')
    time.sleep(1)
    result = {'stopped': _pid(PKG) is None}
    if disconnect_wifi_adb:
        result['disconnect'] = _run(['adb', 'disconnect'])[1]
    return json.dumps(result)


# device MCP serves this; kept for tools/thor scripts
def xenia_log(lines: int = 200, grep: str = '') -> str:
    """The emulator's own log ring (last 8,192 lines, no logcat rotation),
    from the in-app debug server. grep filters by substring."""
    try:
        rows = _api(f'/log?lines={lines}&grep={grep}', timeout=30)
    except RuntimeError as e:
        return json.dumps({'error': str(e)})
    return '\n'.join(rows) if isinstance(rows, list) else json.dumps(rows)


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


# device MCP serves this; kept for tools/thor scripts
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


# device MCP serves this; kept for tools/thor scripts
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
    # Presented-frame rate from the app's FPS badge (tag xenia-fps, about two
    # lines per second, guest swap count based). Available on the play-button
    # path with no trace cvar. last_seconds selects the tail of the run.
    fps_rows = _adb('logcat', '-d', '-s', 'xenia-fps:*', timeout=60).splitlines()
    fps_vals = [float(m.group(1)) for l in fps_rows
                for m in [re.search(r'fps=([0-9.]+)', l)] if m]
    presented = {}
    if fps_vals:
        tail = fps_vals[-window_lines:]
        s = sorted(tail)
        presented = {'samples': len(tail), 'median_fps': s[len(s) // 2],
                     'min_fps': s[0], 'max_fps': s[-1], 'last_10': tail[-10:]}
    return json.dumps({'title': title[-1] if title else None, 'summary': summary,
                       'presented': presented, 'last_lines': timing[-5:]}, indent=2)


# device MCP serves this; kept for tools/thor scripts
def xenia_screenshot(name: str = '') -> str:
    """Capture the screen to scratch/mcp and report the foreground package, so
    a capture of another session's app is never mistaken for ours."""
    os.makedirs(SCRATCH, exist_ok=True)
    path = os.path.join(SCRATCH, f'{name or "screen"}-{_stamp()}.png')
    # The in-app server copies the game surface itself (PixelCopy): always
    # our app, no other session's screen.
    try:
        png = _api('/screenshot', timeout=30, raw=True)
        if png[:4] == bytes([0x89]) + b'PNG':
            with open(path, 'wb') as f:
                f.write(png)
            return json.dumps({'path': path, 'foreground': PKG, 'ours': True, 'source': 'debug API'})
    except RuntimeError:
        pass
    fg = _foreground()
    _shell('screencap -p /data/local/tmp/xe_shot.png')
    _adb('pull', '/data/local/tmp/xe_shot.png', path)
    _shell('rm -f /data/local/tmp/xe_shot.png')
    return json.dumps({'path': path, 'foreground': fg, 'ours': PKG in fg})


# device MCP serves this; kept for tools/thor scripts
def xenia_backtrace(pid: Optional[int] = None, only: str = 'XThread,Emulator,or.github,GPU,Audio,XMA',
                    max_frames: int = 24) -> str:
    """Native backtrace of every thread in the emulator process, symbolized:
    the hang picture (which host lock, wait, or syscall each guest thread
    sits in). The app captures its own threads from inside the process
    (debuggerd needs root); this symbolizes the module+offset frames with
    the NDK llvm-symbolizer against the unstripped libxenia-app.so of the
    last build. only: a comma list of thread-name substrings to show ('' =
    all). Saves the full text to scratch/mcp/backtrace-<stamp>.txt."""
    try:
        data = _api('/backtrace', timeout=60)
    except RuntimeError as e:
        return f'in-app backtrace unavailable: {e}'
    if not isinstance(data, dict) or 'threads' not in data:
        return json.dumps(data)[:2000]
    # Symbolize every libxenia-app.so offset in one llvm-symbolizer run.
    so = os.path.join(REPO, 'android', 'android_studio_project', 'app', 'build', 'intermediates',
                      'ndkBuild', 'githubDebug', 'obj', 'local', 'arm64-v8a', 'libxenia-app.so')
    ndk = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk', 'ndk', '25.0.8775105',
                       'toolchains', 'llvm', 'prebuilt', 'windows-x86_64', 'bin', 'llvm-symbolizer.exe')
    offsets = []
    for t in data['threads']:
        for f in t.get('frames', []):
            if f.get('module') == 'libxenia-app.so':
                offsets.append(f['offset'])
    names = {}
    if offsets and os.path.exists(so) and os.path.exists(ndk):
        uniq = sorted(set(offsets))
        proc = subprocess.run([ndk, '--obj=' + so, '--functions=short', '--inlining=false',
                               '--demangle', '--basenames'] + ['0x' + o for o in uniq],
                              capture_output=True, text=True, timeout=300)
        blocks = proc.stdout.strip().split('\n\n')
        for o, block in zip(uniq, blocks):
            lines = block.strip().split('\n')
            if len(lines) >= 2:
                names[o] = f'{lines[0]} ({lines[1]})'
            elif lines:
                names[o] = lines[0]
    filters = [x.strip() for x in only.split(',') if x.strip()]
    out = [f"pid {data.get('pid')}: {len(data['threads'])} threads; symbols from {os.path.basename(so)}"]
    for t in data['threads']:
        comm = t.get('comm', '')
        if filters and not any(f in comm for f in filters):
            continue
        out.append(f"--- {t['tid']} {comm} wchan={t.get('wchan', '')}")
        for i, f in enumerate(t.get('frames', [])[:max_frames]):
            sym = names.get(f['offset']) if f.get('module') == 'libxenia-app.so' else f.get('symbol', '')
            out.append(f"  #{i:02d} {f.get('module', '?')}+0x{f['offset']} {sym or ''}")
    text = '\n'.join(out)
    os.makedirs(SCRATCH, exist_ok=True)
    path = os.path.join(SCRATCH, f'backtrace-{_stamp()}.txt')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    return f'saved {path}\n' + text[:12000]


STALL_MARKERS = ('SPINLOCK STALL', 'A64 CRASH DIAG', 'guest crash', 'Fatal',
                 'GPU is hung', 'unimplemented', 'Unhandled', 'DbgPrint',
                 'ANR', 'watchdog')


# device MCP serves this; kept for tools/thor scripts
def xenia_stall(pid: Optional[int] = None, hot_threads: int = 3) -> str:
    """The whole stall picture in one call, for a title that stopped producing
    frames: the marker lines in the log (SPINLOCK STALL, A64 CRASH DIAG, guest
    crash, GPU is hung, ...), the FPS badge state, GPU busy, the hottest
    threads, and a /proc row (state, ticks, wait channel) for each of them. The
    Banjo spin-lock stall of 2026-09-20 took a profile, a thread table, a
    diagnostic build, and a decode by hand; this tool gives that picture in
    seconds. A thread with high CPU and a sched_yield or futex wait channel is
    a spinner: look at the SPINLOCK STALL line for the lock, its owner word,
    and the guest link register of the spinner."""
    pid = pid or _pid(PKG)
    if not pid:
        return json.dumps({'running': False})
    # The in-app server gives the picture from inside: the last stall record,
    # the hottest threads over one second, the badge history, the GPU counters.
    try:
        st = _api('/stall', timeout=30)
        markers = _api('/log?lines=40&grep=STALL', timeout=30)
        crash = _api('/log?lines=20&grep=CRASH DIAG', timeout=30)
        st['markers'] = (markers if isinstance(markers, list) else []) + (crash if isinstance(crash, list) else [])
        sp = st.get('spinlock', {})
        hot = [r for r in st.get('hot_threads', []) if r.get('cpu_pct', 0) > 60]
        fps = st.get('fps', [])
        last_fps = fps[-1]['fps'] if fps else None
        if sp.get('count', 0) and sp.get('age_ms', 10**9) < 30000:
            st['verdict'] = f"spin lock {sp['lock']} held: spinner {sp['spinner_tid']} at lr {sp['lr']}"
        elif hot and any('yield' in r.get('wchan', '') for r in hot):
            st['verdict'] = 'a thread spins in sched_yield: xenia_profile(callgraph=True)'
        elif hot:
            st['verdict'] = f'{len(hot)} thread(s) run at full speed with fps {last_fps}: CPU-bound or a guest spin; xenia_profile'
        elif last_fps == 0.0:
            st['verdict'] = 'no frames and no hot thread: every guest thread waits; /threads shows lr and wait_reason'
        else:
            st['verdict'] = f'frames flow at {last_fps} fps'
        return json.dumps(st, indent=1)[:12000]
    except RuntimeError:
        pass
    log = _shell(f'logcat -d --pid={pid} -s xenia', timeout=120)
    # Unique marker lines with a count, so 121 copies of one benign
    # "unimplemented" line do not hide the one SPINLOCK STALL line.
    counts: dict[str, int] = {}
    for l in log.splitlines():
        if any(k in l for k in STALL_MARKERS):
            key = l[l.find('> ') + 2:].strip()[:220]
            counts[key] = counts.get(key, 0) + 1
    markers = [f'{v}x {k}' for k, v in counts.items()]
    fps_rows = _adb('logcat', '-d', '-s', 'xenia-fps:*', timeout=60).splitlines()
    fps_last = fps_rows[-1][fps_rows[-1].find('fps='):].strip() if fps_rows else 'no badge line'
    gpu_busy = _shell('cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage').strip()
    threads = _shell(f'ps -T -p {pid} -o TID,PCPU,NAME').splitlines()[1:]
    def cpu(r):
        f = r.split()
        return float(f[1]) if len(f) > 2 and f[1].replace('.', '').isdigit() else 0.0
    hot = sorted(threads, key=lambda r: -cpu(r))[:hot_threads]
    rows = []
    for r in hot:
        tid = r.split()[0]
        t = f'/proc/{pid}/task/{tid}'
        info = _shell(f'echo "$(cat {t}/comm) $(sed "s/.*) //" {t}/stat | cut -d" " -f1,12,13) '
                      f'$(cat {t}/wchan 2>/dev/null)"').strip()
        rows.append({'tid': int(tid), 'cpu_pct': cpu(r), 'comm_state_uticks_sticks_wchan': info})
    verdict = 'no marker'
    if any('SPINLOCK STALL' in l for l in markers):
        verdict = 'spin lock held forever: read the SPINLOCK STALL line'
    elif any('A64 CRASH DIAG' in l or 'guest crash' in l for l in markers):
        verdict = 'guest crash: read the A64 CRASH DIAG line, then xenia_crash'
    elif any('GPU is hung' in l for l in markers):
        verdict = 'GPU hang: run with vulkan validation'
    else:
        hot = [r for r in rows if r['cpu_pct'] > 60]
        if hot and any('yield' in r['comm_state_uticks_sticks_wchan'] for r in hot):
            verdict = 'a thread spins in sched_yield with no stall marker: xenia_profile(callgraph=True)'
        elif hot:
            verdict = (f'{len(hot)} thread(s) run guest code at full speed with no frames: '
                       'CPU-bound or a guest spin; xenia_profile(callgraph=True) names the code')
        elif fps_last.startswith('fps=0.0'):
            verdict = 'no frames and no hot thread: every guest thread waits; xenia_backtrace'
    return json.dumps({'running': True, 'pid': pid, 'verdict': verdict,
                       'fps_badge': fps_last, 'gpu_busy': gpu_busy,
                       'markers': markers[-12:], 'hot_threads': rows}, indent=2)


# device MCP serves this; kept for tools/thor scripts
def xenia_shader_cache() -> str:
    """Is the GPU thread compiling shaders? The VulkanPipelineCache lines of
    the current log (a line per 64 pipelines with the creation time, the seed
    line, the persist lines) and the cache files on the device: the
    VkPipelineCache blob and Mesa's own disk cache (files/vk_pipeline_cache).
    Pipeline creation was 74 % of the command processor thread in Banjo's
    first world at 2 fps (2026-09-20); a second run must show a seed line and
    fewer creation ms."""
    log = _adb('logcat', '-d', '-s', 'xenia:*', timeout=180)
    lines = [l[l.find('> ') + 2:].strip()[:200] for l in log.splitlines()
             if 'VulkanPipelineCache' in l or 'MESA_SHADER_CACHE' in l]
    files = _run_as('ls -la files/vk_pipeline_cache 2>/dev/null; '
                    'du -sk files/vk_pipeline_cache/mesa 2>/dev/null; '
                    'find files/vk_pipeline_cache/mesa -type f 2>/dev/null | wc -l')
    return json.dumps({'log': lines[-12:], 'device_files': files.strip().splitlines()[-8:]},
                      indent=2)


# device MCP serves this; kept for tools/thor scripts
def xenia_threads(pid: Optional[int] = None, top: int = 20) -> str:
    """Per-thread CPU use, nice value and name of the emulator process."""
    try:
        rows = _api('/threads')
        if isinstance(rows, list):
            rows.sort(key=lambda r: -r.get('cpu_ticks', 0))
            return json.dumps(rows[:top], indent=1)
    except RuntimeError:
        pass
    pid = pid or _pid(PKG)
    if not pid:
        return 'xenia is not running'
    out = _shell(f'ps -T -p {pid} -o TID,NI,PCPU,PRI,NAME')
    rows = out.splitlines()
    body = sorted(rows[1:], key=lambda r: -float(r.split()[2]) if len(r.split()) > 2 and r.split()[2].replace('.', '').isdigit() else 0)
    return '\n'.join(rows[:1] + body[:top])


# device MCP serves this; kept for tools/thor scripts
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


# device MCP serves this; kept for tools/thor scripts
def xenia_toggles() -> str:
    """Every optimization toggle in the app menu: key, title, default, the value
    set on the device, and the cvars it drives. This is the control surface
    (CLAUDE.md directive 17): set these, never cvars."""
    prefs = _pref_bools(_device_prefs())
    rows = []
    for t in _toggle_catalog():
        rows.append({**t, 'device': prefs.get(t['key'], t['default'])})
    return json.dumps(rows, indent=1)


# device MCP serves this; kept for tools/thor scripts
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


# device MCP serves this; kept for tools/thor scripts
def xenia_config_get(key: str = '') -> str:
    """Read the persisted device config (files/xenia.config.toml). It overrides
    compiled defaults; only intent extras beat it. Empty key returns all lines."""
    out = _run_as('cat files/xenia.config.toml')
    if not key:
        return out
    rows = [l for l in out.splitlines() if l.startswith(key)]
    return '\n'.join(rows) or f'{key} not in config'


# device MCP serves this; kept for tools/thor scripts
def xenia_config_set(key: str, value: str) -> str:
    """INERT since 2026-09-20: Android writes xenia.config.toml but never reads it. Use the
    device MCP cvar_set for a live value, a toggle for a shipped lever, or a launch extra.
    Set one key in the persisted device config. value is written as given,
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
def xenia_trap_context(title: str = 'banjo', export: str = 'XamShowDirtyDiscErrorUI',
                       seconds: int = 90) -> str:
    """Launch a title, hold the guest thread at the first call of a kernel
    export, and return the picture that names the cause: registers, the
    text at r3-r6, the memory at r24-r31 with one dereference (request
    objects), the guest call chain with disassembly at every return
    address, and every other thread's chain and stack text. The in-app
    trap report carries chain, mem and stack_text itself; this wraps
    tools/thor/guest_trap_context.py, which adds the disassembly and saves
    the packet to scratch/mcp/trap-<export>-<stamp>.txt."""
    cmd = [sys.executable, '-u', os.path.join(REPO, 'tools', 'thor', 'guest_trap_context.py'),
           title, export, str(seconds)]
    code, out = _run(cmd, timeout=seconds + 300)
    return out[-12000:]


@mcp.tool()
def xenia_guest_disasm(title: str, ranges: str) -> str:
    """PowerPC disassembly of guest code ranges through the in-app server:
    ranges is a space list of hexaddr[:count] (count defaults to 64). Uses
    the running title, or launches it and force-stops it after."""
    cmd = [sys.executable, '-u', os.path.join(REPO, 'tools', 'thor', 'guest_disasm.py'), title]
    cmd += ranges.split()
    code, out = _run(cmd, timeout=300)
    return out[-12000:]


@mcp.tool()
def xenia_dialog_check(seconds: int = 75, label: str = 'run') -> str:
    """One Banjo run: the time XamShowDirtyDiscErrorUI appears, or "no
    dialog", plus the log lines that name a cause and the saved log ring.
    Exit 0 no dialog, 1 dialog, 2 launch failed or died."""
    cmd = [sys.executable, '-u', os.path.join(REPO, 'tools', 'thor', 'banjo_dialog_check.py'),
           str(seconds), label]
    code, out = _run(cmd, timeout=seconds + 240)
    return f'exit {code}' + chr(10) + out[-6000:]


@mcp.tool()
def xenia_launch_cvars(set: str = '', clear: bool = False) -> str:
    """Diagnostic cvars for the NEXT launch (debug builds): set is a comma
    list of name=value; an empty value removes one; clear removes all; no
    arguments lists them. They apply after the profile and the toggles, so
    init-time cvars (render_target_path_vulkan, a GPU trace, UMA options)
    can be tested without a rebuild. Goes through the in-app server when a
    title runs, else writes files/debug_launch_cvars.properties with run-as.
    Not a control surface: clear them when the diagnosis is done."""
    if _api_up():
        if clear:
            return json.dumps(_api('/launch_cvars?clear=1', 'POST'))
        out = None
        for item in [i for i in set.split(',') if i.strip()]:
            name, _, value = item.partition('=')
            out = _api(f'/launch_cvars?name={name.strip()}&value={value.strip()}', 'POST')
        return json.dumps(out if out is not None else _api('/launch_cvars'))
    path = 'files/debug_launch_cvars.properties'
    current = {}
    if not clear:
        for line in _run_as(f'cat {path} 2>/dev/null').splitlines():
            if '=' in line and not line.startswith('#'):
                k, _, v = line.partition('=')
                current[k.strip()] = v.strip()
    for item in [i for i in set.split(',') if i.strip()]:
        name, _, value = item.partition('=')
        if value.strip():
            current[name.strip()] = value.strip()
        else:
            current.pop(name.strip(), None)
    body = ''.join(f'{k}={v}\\n' for k, v in sorted(current.items()))
    _run_as(f"sh -c \"printf '{body}' > {path}\"")
    return json.dumps({'file': path, 'cvars': current, 'via': 'run-as',
                       'note': 'applies at the next launch, after the profile and the toggles'})


@mcp.tool()
def xenia_trace_frame(pull: bool = True, dump: bool = True) -> str:
    """Record the next GPU frame on the device as an .xtr trace, pull it to
    scratch/traces, and replay it on the PC with xenia-gpu-vulkan-trace-dump
    (a PNG of the frame as the desktop GPU renders the same command stream).
    A device-only glitch that the PC replay renders correctly is in the
    device's execution (Android code or the driver), not in the command
    stream; one that the replay also shows is in the shared GPU logic and
    can be bisected on the PC."""
    before = {f['name'] for f in _api('/trace_frame').get('files', [])}
    req = _api('/trace_frame', 'POST', timeout=10)
    name = None
    for _ in range(40):
        time.sleep(0.5)
        files = _api('/trace_frame').get('files', [])
        new = [f for f in files if f['name'] not in before]
        if new:
            # The writer closes the file at the swap; wait for the size to settle.
            size = new[-1]['bytes']
            time.sleep(1)
            files = _api('/trace_frame').get('files', [])
            cur = [f for f in files if f['name'] == new[-1]['name']]
            if cur and cur[0]['bytes'] == size and size > 0:
                name = cur[0]['name']
                break
    if not name:
        return json.dumps({'requested': req, 'error': 'no trace file appeared in 20 s'})
    result = {'requested': req, 'file': name}
    if pull:
        out_dir = os.path.join(REPO, 'scratch', 'traces')
        os.makedirs(out_dir, exist_ok=True)
        local = os.path.join(out_dir, name)
        # run-as cat through adb exec-out keeps the bytes intact.
        with open(local, 'wb') as f:
            subprocess.run(['adb', '-s', SERIAL, 'exec-out', 'run-as', PKG, 'cat', f'files/traces/{name}'],
                           stdout=f, stderr=subprocess.DEVNULL, timeout=300, check=False)
        result['local'] = local
        result['local_bytes'] = os.path.getsize(local) if os.path.exists(local) else 0
        if dump and result['local_bytes'] > 0:
            exe = os.path.join(REPO, 'build', 'bin', 'Windows', 'Release', 'xenia-gpu-vulkan-trace-dump.exe')
            if os.path.exists(exe):
                png = local + '.png'
                code, out = _run([exe, '--target_trace_file=' + local, '--trace_dump_path=' + out_dir,
                                  '--log_file=' + local + '.log'], timeout=600)
                result['dump_exit'] = code
                result['dump_out'] = out[-800:]
                pngs = sorted((os.path.join(out_dir, f) for f in os.listdir(out_dir)
                               if f.endswith('.png')), key=os.path.getmtime)
                result['png'] = pngs[-1] if pngs else None
            else:
                result['dump'] = 'xenia-gpu-vulkan-trace-dump.exe not built'
    return json.dumps(result, indent=1)


@mcp.tool()
def xenia_trace_replay(trace: str, frame: int = -1, timeout_s: int = 180) -> str:
    """Replay a captured GPU trace ON THE DEVICE and return the rendered frame
    as a PNG on the PC: the GPU fix loop without a game boot. trace is a file
    name in the app's files/traces (from trace_frame or trace_stream) or an
    absolute device path; frame is an index or -1 for the last. Starts the
    app's trace viewer activity in dump mode, waits for the .done marker,
    pulls the PNG to scratch/traces/<name>.<frame>.device.png, force-stops
    the app, and reports the lower-half black fraction. Compare with the PC
    replay of the same trace (xenia_trace_frame) to split the command stream
    from the device's execution of it."""
    device_path = trace if trace.startswith('/') else f'/data/user/0/{PKG}/files/traces/{trace}'
    name = os.path.basename(device_path)
    png_device = f'/data/user/0/{PKG}/files/traces/{name}.{frame}.png'
    _run_as(f'rm -f files/traces/{name}.{frame}.png files/traces/{name}.{frame}.png.done')
    _shell(f'am force-stop {PKG}')
    time.sleep(1)
    out = _shell('am start -n ' + PKG + '/jp.xenia.emulator.GpuTraceViewerActivity'
                 f' --es target_trace_file "{device_path}" --es trace_viewer_dump_png "{png_device}"'
                 f' --ei trace_viewer_dump_frame {frame}')
    t0 = time.time()
    marker = ''
    while time.time() - t0 < timeout_s:
        time.sleep(1)
        marker = _run_as(f'cat files/traces/{name}.{frame}.png.done 2>/dev/null').strip()
        if marker:
            break
        if _pid(PKG) is None and time.time() - t0 > 5:
            marker = 'process died'
            break
    result = {'trace': device_path, 'frame': frame, 'am_start': out.strip()[-200:],
              'seconds': int(time.time() - t0), 'marker': marker}
    if marker.startswith('ok'):
        out_dir = os.path.join(REPO, 'scratch', 'traces')
        os.makedirs(out_dir, exist_ok=True)
        local = os.path.join(out_dir, f'{name}.{frame}.device.png')
        with open(local, 'wb') as f:
            subprocess.run(['adb', '-s', SERIAL, 'exec-out', 'run-as', PKG, 'cat', f'files/traces/{name}.{frame}.png'],
                           stdout=f, stderr=subprocess.DEVNULL, timeout=120, check=False)
        result['png'] = local
        result['png_bytes'] = os.path.getsize(local)
        try:
            from PIL import Image
            im = Image.open(local).convert('L')
            w, h = im.size
            px = im.crop((0, h // 2, w, h)).getdata()
            result['size'] = [w, h]
            result['lower_black'] = round(sum(1 for v in px if v < 12) / max(1, len(px)), 3)
        except Exception as e:  # noqa: BLE001
            result['score_error'] = str(e)
    _shell(f'am force-stop {PKG}')
    return json.dumps(result, indent=1)


@mcp.tool()
def xenia_goto(screen: str = 'menu', title: str = 'banjo', steps: str = '',
               launch: bool = True, screenshot: bool = True) -> str:
    """Drive a title to a screen by what is on the panel (the app's goto
    tool), launching it first when it is not running. screen is a preset of
    the title (Banjo: title, menu, world) or steps a raw route:
    'until:gold>0.35;press:START;settle:1500|until:lower_black>0.4'. Returns
    the app's answer (reached, seconds, the frame stats at each step) and a
    screenshot path. The title stays running for the next call."""
    result = {}
    if launch and not _api_up():
        r = json.loads(xenia_launch(title, skip_preflight=True))
        result['launch'] = {k: r.get(k) for k in ('launched', 'pid')}
        if not r.get('launched'):
            result['preflight'] = r.get('preflight')
            return json.dumps(result, indent=1)
        t0 = time.time()
        while time.time() - t0 < 60 and not _api_up():
            time.sleep(1)
    path = '/goto?steps=' + steps if steps else '/goto?screen=' + screen
    result['goto'] = _api(path, 'POST', timeout=400)
    if screenshot:
        result['screenshot'] = json.loads(xenia_screenshot('goto-' + screen)).get('path')
    return json.dumps(result, indent=1)


@mcp.tool()
def xenia_gpu_driver(use: str = None, install_url: str = '', install_zip: str = '',
                     delete: str = '') -> str:
    """The Vulkan drivers installed in the app (Turnip builds and a custom
    fork) and the one selected for the next launch. No arguments: list.
    use='<id>' selects one ('' = the system driver); install_url installs a
    zip from a URL (latest://owner/repo = a GitHub latest release);
    install_zip pushes a zip from the PC (run-as) and installs it; delete
    removes one. The emulator must be running for the in-app server; the
    selection applies at the next launch. A driver is a paradigm axis: name
    the one a measurement used."""
    if not _api_up():
        return json.dumps({'error': 'the emulator is not running; launch a title first (the driver '
                                    'manager lives in the app process)'})
    if install_zip:
        name = os.path.basename(install_zip)
        with open(install_zip, 'rb') as f:
            subprocess.run(['adb', '-s', SERIAL, 'shell', f'run-as {PKG} sh -c "cat > files/incoming_driver.zip"'],
                           stdin=f, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300, check=False)
        r = _api(f'/drivers?install_file=/data/user/0/{PKG}/files/incoming_driver.zip', 'POST', timeout=120)
        _run_as('rm -f files/incoming_driver.zip')
        r['pushed'] = name
        if use is None and r.get('ok') and 'installed ' in r.get('note', ''):
            use = r['note'].split('installed ')[1].split(';')[0]
    elif install_url:
        r = _api('/drivers?install_url=' + install_url, 'POST', timeout=300)
        if use is None and r.get('ok') and 'installed ' in r.get('note', ''):
            use = r['note'].split('installed ')[1].split(';')[0]
    if use is not None:
        r = _api('/drivers?use=' + use, 'POST', timeout=30)
    elif delete:
        r = _api('/drivers?delete=' + delete, 'POST', timeout=30)
    else:
        r = _api('/drivers', timeout=30)
    return json.dumps(r, indent=1)


@mcp.tool()
def xenia_git_head() -> str:
    """Commit the working tree is at, for citing builds."""
    return _run(['git', '-C', REPO, 'log', '-1', '--format=%h %cd %s', '--date=short'])[1]


if __name__ == '__main__':
    mcp.run()
