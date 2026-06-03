$ErrorActionPreference = "Continue"
$env:PATH = "C:\mingw64\bin;" + $env:PATH
$log = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\package.log"
Remove-Item $log -ErrorAction SilentlyContinue
& powershell -ExecutionPolicy Bypass -File "C:\Users\Administrator\Desktop\FreeRDP\build-deps\package.ps1" 2>&1 | Tee-Object -FilePath $log
# zip it up
$dist = "C:\Users\Administrator\Desktop\FreeRDP\dist\wfreerdp-x64"
$zip  = "C:\Users\Administrator\Desktop\FreeRDP\dist\wfreerdp-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$dist\*" -DestinationPath $zip -Force
"ZIP=$zip" | Out-File -Append $log -Encoding utf8
"PACKAGE2_DONE" | Out-File -Append $log -Encoding utf8
