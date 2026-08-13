@echo off
:: win2blur setup - one-time elevated run
:: 1) Start Menu shortcut  2) logon task so win2blur auto-starts silently (no UAC)

:: self-elevate (needed for the logon task with highest privileges)
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

set "TARGET=%~dp0win2blur.exe"
if not exist "%TARGET%" (
    echo ERROR: win2blur.exe not found next to this script.
    pause
    exit /b 1
)

:: 1) Start Menu shortcut
powershell -ExecutionPolicy Bypass -Command "$d=[Environment]::GetFolderPath('Programs');$W=(New-Object -ComObject WScript.Shell).CreateShortcut(\"$d\win2blur.lnk\");$W.TargetPath='%TARGET%';$W.WorkingDirectory='%~dp0';$W.Description='win2blur - Window transparency + Acrylic frosted glass';$W.Save();Write-Host 'Shortcut added to Start Menu.'"

:: 2) logon task - win2blur starts elevated and silent at every logon
schtasks /create /tn "win2blur" /tr "\"%TARGET%\"" /sc onlogon /ru "%USERNAME%" /rl highest /f
if %errorlevel% equ 0 (
    echo Logon task registered - win2blur will start automatically, no UAC prompts.
) else (
    echo WARNING: failed to register logon task.
)
pause
