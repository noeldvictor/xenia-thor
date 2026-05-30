# Xenia-Thor MCP server

A small local MCP server that runs **on the Windows host** and exposes the
`tools/thor/*.ps1` helpers plus raw `adb` as MCP tools. It runs as a normal host
process (outside the Cowork Linux sandbox), so it has real access to PowerShell,
the Android build, and the connected AYN Thor over adb. An MCP client (Claude
Desktop / Cowork) calls these tools and they execute against the actual device.

This is the bridge that lets an agent drive the full loop in
`.agents/skills/xenia-thor-autonomous-driver/SKILL.md`:
build → deploy → launch → capture proof packet → classify → merge/push.

## Tools exposed

- `list_thor_scripts` — discover the available `tools/thor/*.ps1` and key paths.
- `thor_build(mode, device_serial?, variant?, no_subst?)` — wraps `thor_build.ps1`
  (`NativeCore`, `ApkShell`, `ApkShellDeploy`, `FullDeploy`, `Install`, `FullApk`).
- `thor_remote_debug(mode, device_serial?, seconds?, full_logcat?, clear_logcat?, out_dir?)`
  — wraps `thor_android_remote_debug.ps1` (`Status`/`Screenshot`/`Screenrecord`/
  `UiDump`/`CrashBundle`); artifacts land in `scratch/thor-debug/`.
- `thor_gamepad(button, device_serial?)` — debug controller input into the
  running emulator activity.
- `thor_xenia_debug(mode?, device_serial?)` — guest-progress capture.
- `thor_game_status_report(log_path)` — classify a captured logcat.
- `run_thor_script(script_name, args?, timeout?)` — escape hatch for any other
  `tools/thor/*.ps1` (path-confined to `tools/thor`).
- `adb(args, device_serial?, timeout?)` — raw adb passthrough.

## Prerequisites

- Python 3.10+ on the Windows host.
- `pip install mcp`
- PowerShell (5.1 is fine) and `adb` reachable — either on `PATH` or via
  `ANDROID_HOME` / `ANDROID_SDK_ROOT` (the scripts resolve `platform-tools\adb.exe`).
- The Thor authorized for USB adb (`adb devices` shows `c3ca0370  device`).

## Quick check

From the repo root, confirm the server imports and resolves the repo correctly:

```powershell
pip install mcp
python tools\thor\mcp\thor_mcp_server.py
```

It will block waiting on stdio (that is correct for an MCP server — Ctrl+C to
exit). To exercise tools interactively, use the MCP Inspector:

```powershell
npx @modelcontextprotocol/inspector python tools\thor\mcp\thor_mcp_server.py
```

## Wire into Claude Desktop

Add this to `claude_desktop_config.json`
(`%APPDATA%\Claude\claude_desktop_config.json`), fixing the absolute paths, then
fully restart Claude Desktop:

```json
{
  "mcpServers": {
    "xenia-thor": {
      "command": "python",
      "args": [
        "C:\\Users\\leanerdesigner\\Documents\\New project 8\\xenia-thor\\tools\\thor\\mcp\\thor_mcp_server.py"
      ],
      "env": {
        "XENIA_THOR_ROOT": "C:\\Users\\leanerdesigner\\Documents\\New project 8\\xenia-thor",
        "THOR_DEVICE_SERIAL": "c3ca0370"
      }
    }
  }
}
```

If `python` is not on PATH, use the full interpreter path or the Windows
launcher (`"command": "py"`, `"args": ["-3", "...thor_mcp_server.py"]`).

After restart, the `xenia-thor` tools appear in the client. Try
`list_thor_scripts`, then `adb` with `["devices"]` to confirm the device, then
`thor_remote_debug` with `mode="Status"`.

## Safety notes

- This server runs commands with your full Windows user privileges. PowerShell
  script execution is confined to `tools/thor`, and `adb` runs against the
  configured serial, but `run_thor_script` and `adb` are still powerful — review
  before exposing the server to anything other than your own client.
- Long operations (builds, screenrecord) have generous timeouts; output is
  trimmed for the model, while full artifacts are written under
  `scratch/thor-debug/`.
