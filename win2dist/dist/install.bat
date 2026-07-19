@echo off
:: Create a Start Menu shortcut to win2dist
set "TARGET=%~dp0win2dist.exe"
if not exist "%TARGET%" (
    echo ERROR: win2dist.exe not found next to this script.
    pause
    exit /b 1
)
powershell -ExecutionPolicy Bypass -Command "$d=[Environment]::GetFolderPath('Programs');$W=(New-Object -ComObject WScript.Shell).CreateShortcut(\"$d\win2dist.lnk\");$W.TargetPath='%TARGET%';$W.WorkingDirectory='%~dp0';$W.Description='win2dist - Window transparency + Acrylic frosted glass';$W.Save();Write-Host 'Shortcut added to Start Menu.'"
pause
