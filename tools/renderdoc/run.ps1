# Headless RenderDoc replay runner.
#   .\run.ps1 <script.py> <capture.rdc> [extra input lines...]
# qrenderdoc has no usable stdout, so the scripts read rd_in.txt (the
# capture path, then the extra lines) and write rd_out.txt next to this
# file; this polls rd_out.txt for "=== DONE ===" and kills qrenderdoc.
# A .rdc positional argument to qrenderdoc opens the capture in the UI and
# the script never runs - the path goes through rd_in.txt instead.
param(
    [string]$Script = "$PSScriptRoot\rd_bindings.py",
    [string]$Rdc = "",
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Extra = @(),
    [int]$TimeoutSec = 900
)
$qr = "C:\Program Files\RenderDoc\qrenderdoc.exe"
$out = "$PSScriptRoot\rd_out.txt"
$in = "$PSScriptRoot\rd_in.txt"
if (Test-Path $out) { Remove-Item $out -Force }
$lines = @($Rdc) + $Extra
Set-Content -Path $in -Value ($lines -join "`n") -Encoding ascii
# A stale qrenderdoc keeps the script from running (the launch is flaky).
Get-Process qrenderdoc -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 3
Write-Host "Running $Script on $Rdc under qrenderdoc (headless)..."
$p = Start-Process -FilePath $qr -ArgumentList @("--python", $Script) -PassThru
$deadline = (Get-Date).AddSeconds($TimeoutSec)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if ((Test-Path $out) -and (Select-String -Path $out -Pattern "=== DONE ===" -Quiet)) { break }
    if ($p.HasExited -and -not (Test-Path $out)) {
        # Flaky launch: retry once.
        Write-Host "qrenderdoc exited without output; retrying"
        $p = Start-Process -FilePath $qr -ArgumentList @("--python", $Script) -PassThru
    }
}
Start-Sleep -Seconds 1
Get-Process qrenderdoc -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "=== rd_out.txt ==="
if (Test-Path $out) { Get-Content $out } else { Write-Host "(no output - script may have crashed before writing)" }
