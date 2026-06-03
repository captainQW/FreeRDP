$ErrorActionPreference = "Continue"
$env:PATH = "C:\mingw64\bin;" + $env:PATH
$bld = "C:\Users\Administrator\Desktop\FreeRDP\build"
$log = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\rebuild.log"
Remove-Item $log -ErrorAction SilentlyContinue
& cmake --build $bld --target wfreerdp 2>&1 | Tee-Object -FilePath $log
"EXITCODE=$LASTEXITCODE" | Out-File -Append $log -Encoding utf8
"REBUILD_DONE" | Out-File -Append $log -Encoding utf8
