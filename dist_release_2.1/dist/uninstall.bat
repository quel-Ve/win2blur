@echo off
powershell -ExecutionPolicy Bypass -Command "$f=[Environment]::GetFolderPath('Programs')+'\win2blur.lnk';if(Test-Path $f){Remove-Item $f -Force;Write-Host 'Removed.'}else{Write-Host 'Not found.'}"
pause
