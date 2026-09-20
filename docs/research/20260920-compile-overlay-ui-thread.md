# The compile overlay froze because the precompile ran on the UI thread

Date: 2026-09-20. User report: "compiling progress bar don't work for game".

## Symptom

The "Compiling game code" overlay shows "Starting…" and never updates. Android shows
"isn't responding" during the compile. The 2026-09-18 log showed the same: the UI watchdog
reported the main thread stalled at +4 s, sync and async posts both stalled, Java stack idle
in `nativePollOnce`.

## Root cause

`Emulator::CompleteLaunch` (src/xenia/emulator.cc) hops to the UI thread with
`CallInUIThreadSynchronous` because `Window::SetIcon` and the game config load callbacks
need the UI thread. The same function then calls `LoadUserModule`, `FinishLoadingUserModule`,
and `LaunchModule`. `FinishLoadingUserModule` runs `XexModule::LoadContinue`, which runs the
load-window AOT precompile and joins its workers. So the whole precompile ran on the Android
main thread inside the native looper callback `UIThreadLooperCallback`.

A native looper callback blocks the Java `MessageQueue`. No Java message runs, sync or async,
until the callback returns. `Handler.createAsync` cannot help: an async message bypasses a
sync barrier, not a blocked looper. The Java stack shows `nativePollOnce` because the block is
below it, in native code.

## Evidence on the device (run of 2026-09-20 15:08, pid 29009, Blue Dragon)

| item | value |
|---|---|
| process start | 15:08:43 |
| ANR | 15:09:41, "Input dispatching timed out, waited 5002ms for MotionEvent" |
| main thread after 8 min | state S, 18 utime ticks, 241 voluntary switches |
| Emulator thread | 2 utime ticks, 171 switches: waits on the synchronous UI call |
| PrecompileJIT workers | 6 threads, about 9,000 utime ticks each |
| AotCompileWatcher | 26 utime ticks, 2,578 switches: it polls, but its posts never run |
| precompile rate | 22,528 of ~33,414 functions at 15:16:49, about 60 functions per second |

`debuggerd -b` refuses without root on this device, so the per-thread `/proc` table is the
evidence. The MCP `xenia_backtrace` tool now falls back to that table.

## Fix

On Android, `CompleteLaunch` runs on the calling (emulator) thread. Only the two `SetIcon`
calls and the game config callback loop hop to the UI thread through a local
`in_ui_thread` helper. Other platforms keep the whole-function hop. The Java side marks
`mAotSawMarker` volatile and its comment names the cause.

Second finding: commit `2e8c7975a0` left `lstrip('\')` in `tools/mcp/xenia_thor_mcp.py`,
a Python syntax error. The MCP server could not start. Fixed to `lstrip('\\')`.

## Device result (APK `58c0c9b2d922112f`, run of 15:47, pid 10601, Blue Dragon, cold object cache)

Script: `tools/thor/aot_overlay_test.py`. Launch through the VIEW intent. Battery 36 %, charging.
GPU 34.4 C at launch.

| time | overlay text (screenshot) | log |
|---|---|---|
| +5 s | | watcher: "precompile pass 1 running, frontier ~16954" |
| +10 s | "546 functions compiled", bar at 3 % | |
| +57 s | "2,830 functions compiled", bar at 16 % | |
| +414 s | | "pre-warmed 19884 function(s) in 409744ms", then pass 2: 16,954 functions in 23 ms (object cache) |
| +422 s | title screen, "press START", 29.6 fps | |

No `xenia-uiwatchdog` line. No `am_anr` event for this pid. The overlay updates, the game starts.
The fix is verified.

## Side measurement: the AOT OOM is a mapping-count problem, not fragmentation

The script sampled `/proc/<pid>/maps` every 30 s. `vm.max_map_count` is 65,530.

| time | functions compiled | mappings | largest free gap | VmRSS |
|---|---|---|---|---|
| +31 s | ~1,500 | 6,558 | 363.7 GiB | 480 MB |
| +125 s | ~6,000 | 13,260 | 363.7 GiB | 580 MB |
| +251 s | ~12,000 | 21,627 | 363.7 GiB | 627 MB |
| +345 s | ~16,500 | 28,877 | 363.7 GiB | 686 MB |
| +376 s | ~18,000 | 32,770 | 363.7 GiB | 720 MB |
| +408 s | ~19,700 | 41,268 | 363.7 GiB | 764 MB |

The largest free gap never changed. Fragmentation is refuted. The mapping count grew from 6,558 to
41,268 in one pass of 19,884 compiles, and the growth accelerated near the end: about 5.6 new
mappings per function over the last 31 s. The archive refuted the map-count hypothesis on
2026-08-10 with one sample of 2,177 mappings at 7,477 functions. That sample was from a different
build and does not match this run (13,260 at ~6,000).

The crashed run of 15:08 compiled 37,632 functions in one pass. Its mapping count is not
measured. By extrapolation of the rate above, that pass passed 65,530 mappings before its end.
A failed `mmap` returns ENOMEM, Scudo returns null, and LLVM raises `report_bad_alloc_error`.
This matches the tombstone. The mapping count of a crashing run is the measurement that
confirms it. The run of 15:47 survived because
its second pass loaded 16,954 functions from the object cache instead of compiling them.

Not yet measured: which allocations own the mappings. The next step is a `maps` diff between
two samples 30 s apart, grouped by permission and size, on the same run. That names the owner
(JIT code slabs, Scudo secondary blocks, or object-cache files). This is not done.
