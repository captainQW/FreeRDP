$ErrorActionPreference = "Stop"

# Ensure MinGW is on PATH so vcpkg's mingw triplet uses our gcc/g++/ninja
$env:PATH = "C:\mingw64\bin;" + $env:PATH
# vcpkg with no MSVC: force mingw host + target triplets
$env:VCPKG_DEFAULT_TRIPLET      = "x64-mingw-dynamic"
$env:VCPKG_DEFAULT_HOST_TRIPLET = "x64-mingw-dynamic"

$vcpkg = "C:\vcpkg\vcpkg.exe"

# FreeRDP third-party dependencies available as vcpkg ports.
# Core (required): openssl, zlib
# Optional but useful for a full-featured Windows client:
#   - cjson  (JSON / AAD support)
#   - openh264 currently not a reliable mingw port; we keep runtime-loading.
$ports = @(
    "zlib",
    "openssl",
    "cjson"
)

Write-Output "==== vcpkg install: $($ports -join ', ') (triplet x64-mingw-dynamic) ===="
& $vcpkg install @ports --triplet x64-mingw-dynamic --host-triplet x64-mingw-dynamic --clean-after-build
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }

Write-Output "VCPKG_INSTALL_DONE"
& $vcpkg list
