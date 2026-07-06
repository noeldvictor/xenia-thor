# Headless RenderDoc replay runner.
#   .\run.ps1 <script.py> [capture.rdc]
# qrenderdoc has no headless stdout, so scripts write to rd_out.txt; this polls it.
param(
    [string]$Script = "$PSScriptRoot\rd_analyze.py",
    [string]$Rdc = ""
)
$qr = "C:\Program Files\RenderDoc\qrenderdoc.exe"
$out = "$PSScriptRoot\rd_out.txt"
if (Test-Path $out) { Remove-Item $out -Force }
$rdcArg = if ($Rdc) { $Rdc } else { "" }
Write-Host "Running $Script under qrenderdoc (headless)..."
$p = Start-Process -FilePath $qr -ArgumentList @("--python", $Script, $rdcArg) -PassThru
# poll rd_out.txt for the DONE sentinel (scripts must end by writing it)
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Seconds 5
    if ((Test-Path $out) -and (Select-String -Path $out -Pattern "=== DONE ===" -Quiet)) { break }
}
Start-Sleep -Seconds 1
Get-Process qrenderdoc -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "=== rd_out.txt ==="
if (Test-Path $out) { Get-Content $out } else { Write-Host "(no output - script may have crashed before writing)" }
