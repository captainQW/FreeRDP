$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$base    = "https://mirror.msys2.org/mingw/mingw64"
$work    = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\msys2"
$install = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\install"
New-Item -ItemType Directory -Force -Path $work | Out-Null

# OpenSSL package (provides libssl/libcrypto headers, import libs, DLLs)
$pkg = "mingw-w64-x86_64-openssl-3.6.2-2-any.pkg.tar.zst"

$dest = Join-Path $work $pkg
if (-not (Test-Path $dest)) {
    Write-Output "Downloading $pkg"
    curl.exe -L --fail --retry 3 -o $dest "$base/$pkg"
    if ($LASTEXITCODE -ne 0) { throw "download failed: $pkg" }
}

# Extract with bsdtar (handles .zst)
$ex = Join-Path $work "openssl_pkg"
if (Test-Path $ex) { Remove-Item -Recurse -Force $ex }
New-Item -ItemType Directory -Force -Path $ex | Out-Null
tar.exe -xf $dest -C $ex
if ($LASTEXITCODE -ne 0) { throw "extract failed" }

# MSYS2 layout: mingw64/{include,lib,bin}
$root = Join-Path $ex "mingw64"

Write-Output "== Installing OpenSSL headers =="
Copy-Item (Join-Path $root "include\openssl") (Join-Path $install "include\openssl") -Recurse -Force

Write-Output "== Installing OpenSSL import libs / static libs =="
foreach ($l in @("libssl.dll.a","libcrypto.dll.a","libssl.a","libcrypto.a")) {
    $p = Join-Path $root "lib\$l"
    if (Test-Path $p) { Copy-Item $p (Join-Path $install "lib") -Force; Write-Output "  + $l" }
}

Write-Output "== Installing OpenSSL DLLs =="
Get-ChildItem (Join-Path $root "bin") -Filter "lib*ssl*.dll" -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName (Join-Path $install "bin") -Force; Write-Output ("  + " + $_.Name) }
Get-ChildItem (Join-Path $root "bin") -Filter "lib*crypto*.dll" -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName (Join-Path $install "bin") -Force; Write-Output ("  + " + $_.Name) }

# pkgconfig (helps cmake find version)
$pcsrc = Join-Path $root "lib\pkgconfig"
if (Test-Path $pcsrc) {
    New-Item -ItemType Directory -Force -Path (Join-Path $install "lib\pkgconfig") | Out-Null
    Copy-Item (Join-Path $pcsrc "*.pc") (Join-Path $install "lib\pkgconfig") -Force -ErrorAction SilentlyContinue
}

Write-Output "OPENSSL_MSYS2_DONE"
Get-ChildItem (Join-Path $install "bin") -Filter "lib*crypto*.dll" | Select-Object -ExpandProperty Name
Get-ChildItem (Join-Path $install "bin") -Filter "lib*ssl*.dll" | Select-Object -ExpandProperty Name
