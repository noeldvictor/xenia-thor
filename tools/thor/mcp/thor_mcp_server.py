#!/usr/bin/env python3
"""Xenia-Thor local MCP server.

Runs on the Windows host (outside any sandbox) and exposes the repo's
tools/thor/*.ps1 helpers and raw adb as MCP tools, so an MCP client (Claude
Desktop / Cowork) can build, deploy, drive the AYN Thor over adb, and pull proof
packets - work the Linux sandbox cannot do directly.

Setup and wiring: see README.md in this directory.

Requires: Python 3.10+, `pip install mcp`, PowerShell + adb on PATH (or
ANDROID_HOME / ANDROID_SDK_ROOT set so the scripts can find adb.exe).

Env:
  XENIA_THOR_ROOT    repo root (default: inferred from this file's location)
  THOR_DEVICE_SERIAL default device serial (default: c3ca0370)
"""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path
from typing import List

from mcp.server.fastmcp import FastMCP

# --- Configuration ----------------------------------------------------------

REPO_ROOT = Path(
    os.environ.get("XENIA_THOR_ROOT", Path(__file__).resolve().parents[3])
).resolve()
THOR_DIR = (REPO_ROOT / "tools" / "thor").resolve()
JOBS_DIR = (REPO_ROOT / "scratch" / "thor-debug" / "mcp-jobs").resolve()
DEFAULT_SERIAL = os.environ.get("THOR_DEVICE_SERIAL", "c3ca0370")

# Windows process creation flags for detached background jobs.
_DETACHED_PROCESS = 0x00000008
_CREATE_NO_WINDOW = 0x08000000

# Output returned to the model is capped so a giant logcat never blows up the
# context. Scripts write full artifacts into scratch/thor-debug/ anyway.
MAX_OUTPUT_CHARS = 16000

mcp = FastMCP("xenia-thor")


# --- Helpers -----------------------------------------------------------------


def _clip(text: str) -> str:
    if len(text) <= MAX_OUTPUT_CHARS:
        return text
    head = text[: MAX_OUTPUT_CHARS // 4]
    tail = text[-(MAX_OUTPUT_CHARS * 3 // 4):]
    return f"{head}\n...[{len(text) - MAX_OUTPUT_CHARS} chars trimmed]...\n{tail}"


def _format(proc: "subprocess.CompletedProcess[str]") -> str:
    out = proc.stdout or ""
    err = proc.stderr or ""
    body = out
    if err.strip():
        body += f"\n--- stderr ---\n{err}"
    return _clip(f"exit_code={proc.returncode}\n{body}".strip())


def _run(cmd: List[str], timeout: int) -> str:
    """Run a command from the repo root, capturing output as text."""
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return _format(proc)
    except subprocess.TimeoutExpired as exc:
        partial = ""
        if exc.stdout:
            partial = exc.stdout if isinstance(exc.stdout, str) else exc.stdout.decode(errors="replace")
        return _clip(
            f"TIMEOUT after {timeout}s running: {' '.join(cmd)}\n"
            f"--- partial stdout ---\n{partial}"
        )
    except FileNotFoundError as exc:
        return (
            f"COMMAND NOT FOUND: {exc}. Ensure PowerShell and adb are installed "
            f"and on PATH (or ANDROID_HOME / ANDROID_SDK_ROOT are set)."
        )


def _ps(script_name: str, args: List[str], timeout: int) -> str:
    """Run a tools/thor PowerShell script safely (path-confined to THOR_DIR)."""
    script = (THOR_DIR / script_name).resolve()
    try:
        script.relative_to(THOR_DIR)
    except ValueError:
        return f"REFUSED: {script_name} resolves outside {THOR_DIR}"
    if script.suffix.lower() != ".ps1" or not script.is_file():
        return f"NOT FOUND: {script} (expected a .ps1 under tools/thor)"
    cmd = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(script),
    ] + [a for a in args if a != ""]
    return _run(cmd, timeout)


def _serial(device_serial: str) -> str:
    return device_serial or DEFAULT_SERIAL


# --- Tools -------------------------------------------------------------------


@mcp.tool()
def list_thor_scripts() -> str:
    """List the available tools/thor/*.ps1 helper scripts and key paths."""
    if not THOR_DIR.is_dir():
        return f"tools/thor not found at {THOR_DIR}"
    scripts = sorted(p.name for p in THOR_DIR.glob("*.ps1"))
    return (
        f"repo_root={REPO_ROOT}\n"
        f"thor_dir={THOR_DIR}\n"
        f"default_device_serial={DEFAULT_SERIAL}\n"
        f"scripts ({len(scripts)}):\n  " + "\n  ".join(scripts)
    )


@mcp.tool()
def thor_build(
    mode: str = "NativeCore",
    device_serial: str = "",
    variant: str = "GithubDebug",
    no_subst: bool = False,
) -> str:
    """Build / deploy the Xenia-Thor APK via tools/thor/thor_build.ps1.

    mode: FullApk | ApkShell | NativeCore | Install | FullDeploy | ApkShellDeploy
      - NativeCore: native C++/Vulkan/CPU-backend build
      - ApkShell:   fast Java/XML/resource-only APK build
      - ApkShellDeploy / FullDeploy / Install: build and push to the device
    device_serial: target device (default THOR_DEVICE_SERIAL).
    no_subst: pass -NoSubst (use for the resource/UI loop if the subst drive
      trips Gradle resource merge).
    """
    args = ["-Mode", mode, "-Variant", variant, "-DeviceSerial", _serial(device_serial)]
    if no_subst:
        args.append("-NoSubst")
    # Builds and deploys can take many minutes.
    return _ps("thor_build.ps1", args, timeout=3600)


@mcp.tool()
def thor_remote_debug(
    mode: str = "Status",
    device_serial: str = "",
    seconds: int = 30,
    full_logcat: bool = False,
    clear_logcat: bool = False,
    out_dir: str = "",
) -> str:
    """Capture a device proof packet via thor_android_remote_debug.ps1.

    mode: Status | Screenshot | Screenrecord | UiDump | CrashBundle
      (Mirror is interactive-only; avoid in headless automation.)
    seconds: duration for Screenrecord.
    clear_logcat: clear logcat before capture (do NOT use when the user says
      "check now" / reports a fresh crash - capture first).
    Artifacts land under scratch/thor-debug/; the returned text includes the
    packet path and classification info.
    """
    args = ["-Mode", mode, "-DeviceSerial", _serial(device_serial), "-Seconds", str(seconds)]
    if out_dir:
        args += ["-OutDir", out_dir]
    if full_logcat:
        args.append("-FullLogcat")
    if clear_logcat:
        args.append("-ClearLogcat")
    timeout = seconds + 180 if mode == "Screenrecord" else 600
    return _ps("thor_android_remote_debug.ps1", args, timeout=timeout)


@mcp.tool()
def thor_gamepad(button: str, device_serial: str = "") -> str:
    """Send a debug gamepad button into the running emulator activity.

    Requires a debug APK with EmulatorActivity running. Sends a package-scoped
    broadcast through the same native gamepad path as real controller input.
    button: e.g. A, B, X, Y, Start, Back, DpadUp...
    """
    args = ["-DeviceSerial", _serial(device_serial), "-Button", button]
    return _ps("thor_android_debug_gamepad_input.ps1", args, timeout=120)


@mcp.tool()
def thor_xenia_debug(mode: str = "Capture", device_serial: str = "") -> str:
    """Run thor_xenia_debug.ps1 (e.g. -Mode Capture) for guest-progress capture."""
    args = ["-Mode", mode, "-DeviceSerial", _serial(device_serial)]
    return _ps("thor_xenia_debug.ps1", args, timeout=900)


@mcp.tool()
def thor_game_status_report(log_path: str) -> str:
    """Classify a captured logcat via thor_android_game_status_report.ps1.

    log_path: path to a logcat file (e.g. scratch/thor-debug/<packet>/logcat-focused.txt).
    """
    return _ps("thor_android_game_status_report.ps1", ["-LogPath", log_path], timeout=300)


@mcp.tool()
def run_thor_script(script_name: str, args: List[str] | None = None, timeout: int = 900) -> str:
    """Run any other tools/thor/*.ps1 helper by name with raw args.

    Escape hatch for the audit/report scripts not given a dedicated tool, e.g.
    thor_burnout_no_present_stall_audit.ps1 with
    args=["-PacketDir","scratch/thor-debug/<packet>","-OutPath","scratch/thor-debug/audit.txt"].
    Confined to tools/thor.
    """
    return _ps(script_name, list(args or []), timeout=timeout)


@mcp.tool()
def adb(args: List[str], device_serial: str = "", timeout: int = 300) -> str:
    """Run a raw adb command against the device.

    The device serial is injected as `-s <serial>` unless args already target a
    device. Examples: ["devices"], ["shell","dumpsys","SurfaceFlinger"],
    ["logcat","-d","-t","2000"], ["install","-r","app.apk"].
    """
    arg_list = list(args)
    base = ["adb"]
    if arg_list and arg_list[0] not in ("devices", "kill-server", "start-server", "-s"):
        base += ["-s", _serial(device_serial)]
    return _run(base + arg_list, timeout=timeout)


# --- Background jobs (for long ops that exceed the MCP request timeout) ------


def _confine_repo(path_str: str) -> Path | None:
    raw = Path(path_str)
    p = (raw if raw.is_absolute() else (REPO_ROOT / raw)).resolve()
    try:
        p.relative_to(REPO_ROOT)
        return p
    except ValueError:
        return None


@mcp.tool()
def start_thor_job(script_name: str, args: List[str] | None = None, name: str = "") -> str:
    """Launch a tools/thor/*.ps1 in the BACKGROUND and return immediately.

    Use this for anything long (builds, deploys, Screenrecord) because a
    synchronous call exceeds the MCP request timeout. Returns the OS pid and the
    log path; poll progress with `read_log` and completion with `job_status`.
    Output (stdout+stderr) streams to a log under scratch/thor-debug/mcp-jobs/.
    """
    script = (THOR_DIR / script_name).resolve()
    try:
        script.relative_to(THOR_DIR)
    except ValueError:
        return f"REFUSED: {script_name} resolves outside {THOR_DIR}"
    if script.suffix.lower() != ".ps1" or not script.is_file():
        return f"NOT FOUND: {script}"
    JOBS_DIR.mkdir(parents=True, exist_ok=True)
    tag = (name or script.stem)
    log_path = JOBS_DIR / f"{tag}-{time.strftime('%Y%m%d-%H%M%S')}.log"
    runner = (THOR_DIR / "mcp" / "_run_job.ps1").resolve()
    if not runner.is_file():
        return f"NOT FOUND: runner {runner}"
    # The runner owns the log handle and does its own redirection, so the
    # detached child does not need to inherit a redirected stdout (which it
    # cannot reliably do). The launcher's own stdout goes to DEVNULL.
    cmd = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(runner),
        "-Script",
        str(script),
        "-Log",
        str(log_path),
    ] + [a for a in (args or []) if a != ""]
    try:
        proc = subprocess.Popen(
            cmd,
            cwd=str(REPO_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            creationflags=_DETACHED_PROCESS | _CREATE_NO_WINDOW,
            close_fds=True,
        )
    except FileNotFoundError as exc:
        return f"COMMAND NOT FOUND: {exc} (is PowerShell on PATH?)"
    return (
        f"started background job '{tag}'\n"
        f"pid={proc.pid}\n"
        f"log={log_path}\n"
        f"Poll with read_log(path) and job_status(pid)."
    )


@mcp.tool()
def start_build(
    mode: str = "NativeCore",
    device_serial: str = "",
    variant: str = "GithubDebug",
    no_subst: bool = False,
) -> str:
    """Background build/deploy via thor_build.ps1 (returns immediately).

    Preferred over thor_build for NativeCore / *Deploy modes, which run for
    minutes. Poll the returned log with read_log and job_status.
    """
    args = ["-Mode", mode, "-Variant", variant, "-DeviceSerial", _serial(device_serial)]
    if no_subst:
        args.append("-NoSubst")
    return start_thor_job("thor_build.ps1", args, name=f"build-{mode}")


@mcp.tool()
def read_log(path: str, tail_lines: int = 200) -> str:
    """Tail a job log (or any file under the repo). Safe on files still being
    written. Use after start_thor_job / start_build to watch progress."""
    p = _confine_repo(path)
    if p is None:
        return f"REFUSED: {path} resolves outside {REPO_ROOT}"
    if not p.is_file():
        return f"NOT FOUND (not created yet?): {p}"
    res = _run(
        ["powershell", "-NoProfile", "-Command",
         f"Get-Content -LiteralPath '{p}' -Tail {int(tail_lines)}"],
        timeout=60,
    )
    return res


@mcp.tool()
def job_status(pid: int) -> str:
    """Report whether a background job pid is RUNNING or EXITED."""
    res = _run(
        ["powershell", "-NoProfile", "-Command",
         f"if (Get-Process -Id {int(pid)} -ErrorAction SilentlyContinue) "
         f"{{ 'RUNNING' }} else {{ 'EXITED' }}"],
        timeout=30,
    )
    return res


if __name__ == "__main__":
    mcp.run()
