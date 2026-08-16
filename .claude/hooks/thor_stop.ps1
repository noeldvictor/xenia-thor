# Thor autonomous-grind Stop hook.
# Fires when Claude finishes a message. While the control file
# .claude/thor-loop-active exists, it BLOCKS the stop and feeds back a
# continuation prompt -> Claude keeps grinding (the "never stop" loop).
# Delete .claude/thor-loop-active (user OR Claude) to end the loop.
#
# Device safety: a hook-driven loop has no natural 15-min pause, so this hook
# PACES on the Thor GPU temperature (sleeps while hot) - it can never let the
# loop fire the device back-to-back (the Thor crashed once from exactly that).
$ErrorActionPreference = 'SilentlyContinue'

# Drain stdin (the hook input JSON) so the caller never blocks on the pipe.
try { [Console]::In.ReadToEnd() | Out-Null } catch {}

# OFF switch: no control file -> allow the stop (exit 0, no output).
$flag = Join-Path $PSScriptRoot '..\thor-loop-active'
if (-not (Test-Path $flag)) { exit 0 }

# Best-effort device-temp read for pacing + reporting.
$adb = 'C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$temp = 0.0
try {
  $raw = & $adb -s c3ca0370 shell cat /sys/class/kgsl/kgsl-3d0/temp 2>$null
  if ($raw) { $temp = [int]("0$($raw.ToString().Trim())") / 1000.0 }
} catch {}
# Pace while hot so the loop cannot over-fire the device.
if ($temp -ge 45) { Start-Sleep -Seconds 45 }
$tnote = if ($temp -gt 0) { "Thor GPU temp=$temp C." } else { "Thor temp unavailable (check adb)." }

$reason = "AUTONOMOUS THOR GRIND (hook-driven; .claude/thor-loop-active present -> keep going, never stop). $tnote Do ONE high-value unit then this Stop hook re-fires and you continue. SOURCE OF TRUTH = memory thor-game-state (read it for the live priority; priorities shift, so trust it over this line). THERMAL+BATTERY GATE FIRST: only fire a game if temp<45C AND battery>=30%; ONE fire per cooldown; after every fire force-stop jp.xenia.emulator.github.debug + screen asleep (keyevent 223); NEVER thrash. ALWAYS read the png (RULE 0: never measure movies) + grep logcat. REBUILD+INSTALL the APK (gradle from X: subst, NOT C:\\xt) before any readout fire that depends on new code. CURRENT THREAD (2026-06-06): BD GPU is EDRAM-TRANSFER/BARRIER-bound not binning (gpu_pass ~2.6ms vs gpu_frame ~90ms; ~49 pass-breaks x ~1.8ms tile-flush = ~87ms) -> (1) A/B vulkan_coalesce_edram_transfers on a STABLE heavy scene (BTTF in-game, dodge BD intro boot-variance) for direction; (2) the real win = BUILD the :4910 single-pass transfer merge in PerformTransfersAndResolveClears (vulkan_render_target_cache.cc:4604) cvar-gated. Burnout rt-gate SHIPPED default-on (+34%, pushed). Don't revert GPU/CPU work, don't talk levers down. Don't ask; forward-only; never fabricate (numbers from THIS turn only). Targeted git add (NEVER -A / build / *.vcxproj / *.sln); ascii here-string .git/COMMIT_THOR_MSG.txt; Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>. Update thor-game-state each milestone. If NOTHING high-value+safe remains this iteration, DELETE .claude\\thor-loop-active to end the loop cleanly."

$json = @{ decision = 'block'; reason = $reason } | ConvertTo-Json -Compress
[Console]::Out.Write($json)
exit 0
