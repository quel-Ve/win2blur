@echo off
:: Create a Start Menu shortcut to win2blur
set "TARGET=%~dp0win2blur.exe"
if not exist "%TARGET%" (
    echo ERROR: win2blur.exe not found next to this script.
    pause
    exit /b 1
)
powershell -ExecutionPolicy Bypass -Command "$d=[Environment]::GetFolderPath('Programs');$W=(New-Object -ComObject WScript.Shell).CreateShortcut(\"$d\win2blur.lnk\");$W.TargetPath='%TARGET%';$W.WorkingDirectory='%~dp0';$W.Description='win2blur - Window transparency + Acrylic frosted glass';$W.Save();Write-Host 'Shortcut added to Start Menu.'"
pause
