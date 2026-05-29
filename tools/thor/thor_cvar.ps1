<#
.SYNOPSIS
  Flip a Xenia cvar at runtime on the AYN Thor WITHOUT relaunching the title.

  Sends the EmulatorActivity SET_CVAR broadcast (added for the fast OODA loop).
  The running emulator applies the value immediately with command-line priority.

.NOTES
  Per-frame cvars (e.g. vulkan_trace_perf_counters, most GPU/debug toggles) take
  effect live. Init-only cvars that are read once at startup (e.g.
  gpu_uma_direct_shared_memory, which allocates the shared-memory buffer at init)
  will NOT change a running session - those still need a relaunch, but at least
  no rebuild/reinstall.

.EXAMPLE
  thor_cvar.ps1 -Name vulkan_trace_perf_counters -Value true
  thor_cvar.ps1 -Name vulkan_trace_perf_counters_log_interval -Value 60
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string]$Package = "jp.xenia.emulator.github.debug",
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Value
)

$action = "$Package.SET_CVAR"
& $Adb -s $DeviceSerial shell am broadcast -a $action -p $Package `
    --es cvar_name $Name --es cvar_value $Value
# The native side logs "nativeSetConfigVar: <name>=<value> -> applied|unknown cvar".
& $Adb -s $DeviceSerial logcat -d -t 200 | Select-String -Pattern "nativeSetConfigVar|SET_CVAR" |
    Select-Object -Last 3 | ForEach-Object { $_.Line }
