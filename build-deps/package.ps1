# Assemble a standalone, runnable wfreerdp distribution with all dependencies
$ErrorActionPreference = "Stop"

$repo    = "C:\Users\Administrator\Desktop\FreeRDP"
$build   = Join-Path $repo "build"
$install = Join-Path $repo "build-deps\install"
$dist    = Join-Path $repo "dist\wfreerdp-x64"

# fresh dist dir
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $dist | Out-Null

function Grab($pattern, $desc) {
    $found = $false
    foreach ($p in $pattern) {
        $items = Get-ChildItem -Path $p -ErrorAction SilentlyContinue
        foreach ($it in $items) {
            Copy-Item $it.FullName $dist -Force
            Write-Output ("  + {0}" -f $it.Name)
            $found = $true
        }
    }
    if (-not $found) { Write-Output ("  ! MISSING: {0}" -f $desc) }
}

Write-Output "== executable =="
Grab @("$build\client\Windows\cli\wfreerdp.exe") "wfreerdp.exe"

Write-Output "== FreeRDP / WinPR libraries =="
Grab @(
    "$build\libfreerdp\libfreerdp3.dll",
    "$build\client\common\libfreerdp-client3.dll",
    "$build\client\Windows\libwfreerdp-client3.dll",
    "$build\winpr\libwinpr\libwinpr3.dll"
) "FreeRDP core DLLs"

Write-Output "== dependency libraries (LibreSSL / zlib) =="
Grab @("$install\bin\*.dll") "LibreSSL/zlib runtime DLLs"

Write-Output "== H.264 decoder =="
Grab @("$repo\build-deps\openh264.dll") "openh264.dll"

Write-Output "== MinGW runtime =="
$mingwBin = Split-Path (Get-Command gcc).Source -Parent
Grab @(
    "$mingwBin\libgcc_s_seh-1.dll",
    "$mingwBin\libwinpthread-1.dll",
    "$mingwBin\libstdc++-6.dll",
    "$mingwBin\libssp-0.dll"
) "MinGW runtime DLLs"

# Convenience launcher
$run = @'
@echo off
REM Example launcher. Edit host/user as needed.
REM Black-block concealment is enabled via /gfx:AVC444,conceal-black
setlocal
set HERE=%~dp0
"%HERE%wfreerdp.exe" /v:%1 /u:%2 /gfx:AVC444,conceal-black /dynamic-resolution +clipboard /cert:ignore /from-stdin
endlocal
'@
Set-Content -Path (Join-Path $dist "run-example.cmd") -Value $run -Encoding ASCII

$readme = @'
wfreerdp (FreeRDP 3.x) - 64-bit standalone build with H.264 black-block concealment

Contents
  wfreerdp.exe            - the client
  lib*3.dll               - FreeRDP / WinPR libraries
  libcrypto/ssl/tls*.dll  - LibreSSL (TLS)
  libzlib.dll             - zlib
  openh264.dll            - Cisco OpenH264 (H.264 decoder, loaded at runtime)
  libgcc/libwinpthread/.. - MinGW C/C++ runtime

Black-block concealment (hides corrupt black macroblocks baked into the
server H.264 stream by a faulty hardware encoder):
  wfreerdp.exe /v:HOST:PORT /u:USER /p:PASS /gfx:AVC444,conceal-black /f

Notes
  - openh264.dll MUST sit next to wfreerdp.exe for AVC444/H.264 to work.
  - Prefer /from-stdin over /p:PASS to avoid leaking credentials in the
    process list.
'@
Set-Content -Path (Join-Path $dist "README.txt") -Value $readme -Encoding ASCII

Write-Output ""
Write-Output "== dist contents =="
Get-ChildItem $dist | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Output

Write-Output "DIST=$dist"
Write-Output "PACKAGE_DONE"
