# Fetch FreeRDP build dependencies (native MinGW build)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$root = "C:\Users\Administrator\Desktop\FreeRDP\build-deps"
$src  = Join-Path $root "src"
New-Item -ItemType Directory -Force -Path $src | Out-Null

function Fetch($url, $file) {
    $dest = Join-Path $src $file
    if (Test-Path $dest) {
        Write-Output "exists: $file"
        return
    }
    Write-Output "downloading: $url"
    curl.exe -L --fail --retry 3 -o $dest $url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
}

function Extract($file, $dir) {
    $destDir = Join-Path $src $dir
    if (Test-Path $destDir) {
        Write-Output "extracted: $dir"
        return
    }
    Write-Output "extracting: $file"
    $tmp = Join-Path $src "_tmp_$dir"
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    tar.exe -xf (Join-Path $src $file) -C $tmp
    # move single top-level folder content to $destDir
    $entries = Get-ChildItem $tmp
    if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) {
        Move-Item $entries[0].FullName $destDir
    } else {
        Move-Item $tmp $destDir
    }
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
}

# zlib 1.3.1
Fetch "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz" "zlib.tar.gz"
Extract "zlib.tar.gz" "zlib"

# LibreSSL 4.2.1 (same version as official mingw.sh)
Fetch "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-4.2.1.tar.gz" "libressl.tar.gz"
Extract "libressl.tar.gz" "libressl"

Write-Output "FETCH_DONE"

# OpenH264 2.6.0 (headers only needed; loaded at runtime via WITH_OPENH264_LOADING)
Fetch "https://github.com/cisco/openh264/archive/refs/tags/v2.6.0.tar.gz" "openh264.tar.gz"
Extract "openh264.tar.gz" "openh264"

Write-Output "FETCH_OPENH264_DONE"
