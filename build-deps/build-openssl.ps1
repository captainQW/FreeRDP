$ErrorActionPreference = "Stop"

$src     = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\src\openssl"
$install = "C:\Users\Administrator\Desktop\FreeRDP\build-deps\install"
$perl    = "C:\Program Files\Git\usr\bin\perl.exe"
$make    = "mingw32-make"

# Ensure MinGW + git-usr tools are on PATH (OpenSSL Configure needs a unix-ish env for some helpers)
$env:PATH = "C:\mingw64\bin;C:\Program Files\Git\usr\bin;" + $env:PATH

Push-Location $src
try {
    Write-Output "== Configure OpenSSL (mingw64, static) =="
    & $perl Configure mingw64 no-shared no-tests no-docs no-apps `
        --prefix=$install --openssldir=$install/ssl threads
    if ($LASTEXITCODE -ne 0) { throw "openssl configure failed" }

    Write-Output "== Build libcrypto/libssl =="
    & $make -j4 build_libs
    if ($LASTEXITCODE -ne 0) { throw "openssl build failed" }

    Write-Output "== Install (headers + libs) =="
    & $make install_dev
    if ($LASTEXITCODE -ne 0) { throw "openssl install_dev failed" }

    Write-Output "OPENSSL_BUILD_DONE"
}
finally {
    Pop-Location
}
