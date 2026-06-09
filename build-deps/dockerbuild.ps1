$ErrorActionPreference = "Continue"
$docker = "C:\Program Files\Docker\Docker\resources\bin\docker.exe"
$repo = "C:\Users\Administrator\Desktop\FreeRDP"
$log = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\docker_build.log"
Remove-Item $log -ErrorAction SilentlyContinue

"=== docker ps check ===" | Out-File $log -Encoding utf8
& $docker ps 2>&1 | Out-File -Append $log -Encoding utf8
"PS_RC=$LASTEXITCODE" | Out-File -Append $log -Encoding utf8

"=== docker build start ===" | Out-File -Append $log -Encoding utf8
& $docker build -f "$repo\scripts\Dockerfile.debian" -t freerdp-debian "$repo" 2>&1 | Out-File -Append $log -Encoding utf8
"BUILD_RC=$LASTEXITCODE" | Out-File -Append $log -Encoding utf8
"=== DONE ===" | Out-File -Append $log -Encoding utf8
