$startup = [Environment]::GetFolderPath("Startup")
$shortcut = Join-Path $startup "win2dist.lnk"
if (Test-Path $shortcut) {
    Remove-Item $shortcut -Force
    Write-Host "Removed: $shortcut"
} else {
    Write-Host "Shortcut not found."
}
pause
