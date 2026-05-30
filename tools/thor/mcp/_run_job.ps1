param(
    [Parameter(Mandatory = $true)][string]$Script,
    [Parameter(Mandatory = $true)][string]$Log,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Remaining
)

# Background job runner for the xenia-thor MCP server. Owns its own log handle
# (so it does not depend on inheriting a redirected stdout from the launcher),
# runs the target tools/thor script, captures every output stream to $Log, and
# appends a completion sentinel with the exit code.

$ErrorActionPreference = "Continue"

"THOR_JOB_START $(Get-Date -Format o)" | Out-File -LiteralPath $Log -Encoding utf8
"THOR_JOB_SCRIPT $Script" | Add-Content -LiteralPath $Log
"THOR_JOB_ARGS $($Remaining -join ' ')" | Add-Content -LiteralPath $Log

$code = 0
try {
    if ($Remaining) {
        & $Script @Remaining *>> $Log
    } else {
        & $Script *>> $Log
    }
    $code = $LASTEXITCODE
    if ($null -eq $code) { $code = 0 }
} catch {
    ($_ | Out-String) | Add-Content -LiteralPath $Log
    $code = 1
}

"THOR_JOB_DONE exit=$code" | Add-Content -LiteralPath $Log
