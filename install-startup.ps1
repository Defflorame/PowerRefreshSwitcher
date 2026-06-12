$ErrorActionPreference = "Stop"

$exePath = Join-Path $PSScriptRoot "PowerRefreshSwitcher.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "PowerRefreshSwitcher.exe not found. Build it first with build-msvc.bat."
}

$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
Set-ItemProperty -Path $runKey -Name "PowerRefreshSwitcher" -Value "`"$exePath`""
Write-Host "Startup entry installed for current user:"
Write-Host $exePath
