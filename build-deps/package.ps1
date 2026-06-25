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

Write-Output "== dependency libraries (OpenSSL / zlib) =="
Grab @("$install\bin\*.dll") "OpenSSL/zlib runtime DLLs"

Write-Output "== OpenSSL legacy provider (MD4/RC4 -> required for NTLM/NLA) =="
# OpenSSL 3.x keeps MD4 (NTLM hash) and RC4 (RDP security / autoreconnect cookies)
# in the 'legacy' provider module. Without it WinPR logs 'NTLM support not
# available' and NLA logons fail. Ship the module that matches our libcrypto and
# point OpenSSL at it via OPENSSL_MODULES (set by the launcher) plus a co-located
# ossl-modules dir.
$osslModules = Join-Path $dist "ossl-modules"
New-Item -ItemType Directory -Force -Path $osslModules | Out-Null
$legacy = "$repo\build-deps\msys2\openssl_pkg\mingw64\lib\ossl-modules\legacy.dll"
if (Test-Path $legacy) {
    Copy-Item $legacy $osslModules -Force
    Write-Output "  + ossl-modules\legacy.dll"
} else {
    Write-Output "  ! MISSING: legacy.dll (NTLM/NLA will not work)"
}

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
REM Point OpenSSL at the bundled legacy provider (MD4/RC4) so NTLM/NLA works.
set OPENSSL_MODULES=%HERE%ossl-modules
"%HERE%wfreerdp.exe" /v:%1 /u:%2 /gfx:AVC444,conceal-black /dynamic-resolution +clipboard /cert:ignore /from-stdin
endlocal
'@
Set-Content -Path (Join-Path $dist "run-example.cmd") -Value $run -Encoding ASCII

# RemoteApp seamless-launch launcher.
#   run-remoteapp.cmd HOST:PORT USER PASSWORD "C:\path\to\app.exe"
# Authenticates silently (auto-logon), shows only the application window, and
# disconnects when the app window is closed. The server must publish the app as
# a RemoteApp (RDSH role, or TSAppAllowList on desktop Windows) - otherwise the
# server returns RAIL_EXEC_E_HOOK_NOT_LOADED. See docs/rail-remoteapp.md section 8.
$runApp = @'
@echo off
REM Seamless RemoteApp launcher.
REM Usage: run-remoteapp.cmd HOST:PORT USER PASSWORD "C:\Path\To\App.exe"
setlocal
set HERE=%~dp0
REM Point OpenSSL at the bundled legacy provider (MD4/RC4) so NTLM/NLA works.
set OPENSSL_MODULES=%HERE%ossl-modules
"%HERE%wfreerdp.exe" /v:%1 /u:%2 /p:%3 /app:program:"%~4" /cert:ignore /gfx:AVC444,conceal-black +clipboard /dynamic-resolution
endlocal
'@
Set-Content -Path (Join-Path $dist "run-remoteapp.cmd") -Value $runApp -Encoding ASCII

$readme = @'
wfreerdp (FreeRDP 3.x) - 64-bit standalone build with H.264 black-block concealment

Contents
  wfreerdp.exe            - the client
  run-example.cmd         - desktop session launcher
  run-remoteapp.cmd       - seamless RemoteApp launcher (app window only)
  lib*3.dll               - FreeRDP / WinPR libraries
  libcrypto/libssl*.dll   - OpenSSL 3.x (TLS)
  ossl-modules\legacy.dll - OpenSSL legacy provider (MD4/RC4; required for NTLM/NLA)
  libzlib.dll             - zlib
  openh264.dll            - Cisco OpenH264 (H.264 decoder, loaded at runtime)
  libgcc/libwinpthread/.. - MinGW C/C++ runtime

Black-block concealment (hides corrupt black macroblocks baked into the
server H.264 stream by a faulty hardware encoder):
  wfreerdp.exe /v:HOST:PORT /u:USER /p:PASS /gfx:AVC444,conceal-black /f

NTLM / NLA authentication
  OpenSSL 3.x keeps MD4 (NTLM) and RC4 (RDP security) in the 'legacy' provider.
  If you launch wfreerdp.exe directly (not via run-example.cmd), set:
      set OPENSSL_MODULES=<this folder>\ossl-modules
  otherwise NLA logons may fail with "NTLM support not available".

Notes
  - openh264.dll MUST sit next to wfreerdp.exe for AVC444/H.264 to work.
  - Prefer /from-stdin over /p:PASS to avoid leaking credentials in the
    process list.

Seamless RemoteApp launch
  run-remoteapp.cmd HOST:PORT USER PASSWORD "C:\Path\To\App.exe"
  Authenticates silently, shows only the application window, and disconnects
  when the app window closes. The SERVER must publish the application as a
  RemoteApp (RDSH role, or TSAppAllowList on desktop Windows); otherwise the
  server returns RAIL_EXEC_E_HOOK_NOT_LOADED and no app window can appear.
  See docs/rail-remoteapp.md section 8 for server setup and a two-step
  client/server diagnostic.
'@
Set-Content -Path (Join-Path $dist "README.txt") -Value $readme -Encoding ASCII

Write-Output ""
Write-Output "== dist contents =="
Get-ChildItem $dist | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Output

Write-Output "DIST=$dist"
Write-Output "PACKAGE_DONE"
