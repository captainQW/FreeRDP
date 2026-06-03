$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$src = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\src"
New-Item -ItemType Directory -Force -Path $src | Out-Null

$ver = "3.4.1"
$file = Join-Path $src "openssl.tar.gz"
$dir  = Join-Path $src "openssl"

if (-not (Test-Path $file)) {
    $url = "https://github.com/openssl/openssl/releases/download/openssl-$ver/openssl-$ver.tar.gz"
    Write-Output "Downloading $url"
    curl.exe -L --fail --retry 3 -o $file $url
    if ($LASTEXITCODE -ne 0) { throw "openssl download failed" }
}

if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
$tmp = Join-Path $src "_oss_tmp"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
tar.exe -xf $file -C $tmp
$inner = Get-ChildItem $tmp | Select-Object -First 1
Move-Item $inner.FullName $dir
Remove-Item -Recurse -Force $tmp

Write-Output ("OPENSSL_SRC_READY: " + $dir)
Test-Path (Join-Path $dir "Configure")
