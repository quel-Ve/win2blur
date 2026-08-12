# win2dist - Create shortcut in Startup folder
# Run: powershell -ExecutionPolicy Bypass -File create_shortcut.ps1
# Or: right-click → Run with PowerShell

$ErrorActionPreference = "Stop"

# Get the directory where this script is located
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$target = Join-Path $scriptDir "dist2\win2dist.exe"

if (-not (Test-Path $target)) {
    Write-Host "ERROR: win2dist.exe not found at $target"
    Write-Host "Make sure you run this script from the project folder."
    pause
    exit 1
}

$startup = [Environment]::GetFolderPath("Startup")
$shortcut = Join-Path $startup "win2dist.lnk"

$WshShell = New-Object -ComObject WScript.Shell
$lnk = $WshShell.CreateShortcut($shortcut)
$lnk.TargetPath = $target
$lnk.WorkingDirectory = Split-Path $target
$lnk.Description = "win2dist - Window transparency + Acrylic frosted glass"
$lnk.WindowStyle = 7  # Minimized
$lnk.Save()

Write-Host "Shortcut created: $shortcut"
Write-Host "win2dist will start automatically on next login."
Write-Host ""
Write-Host "To remove: delete $shortcut"
Write-Host "Or run: remove_shortcut.ps1"
pause
