$ErrorActionPreference = "Stop"

$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
Remove-ItemProperty -Path $runKey -Name "PowerRefreshSwitcher" -ErrorAction SilentlyContinue
Write-Host "Startup entry removed for current user."
