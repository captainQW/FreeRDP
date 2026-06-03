# Build FreeRDP dependencies (zlib, LibreSSL) with MinGW gcc + ninja
$ErrorActionPreference = "Stop"

$root    = "C:\Users\Administrator\Desktop\FreeRDP\build-deps"
$src     = Join-Path $root "src"
$bld     = Join-Path $root "build"
$install = Join-Path $root "install"
New-Item -ItemType Directory -Force -Path $bld, $install | Out-Null

$env:CC = "gcc"
$env:CXX = "g++"

function CMakeBuild($name, $extraArgs) {
    $srcDir = Join-Path $src $name
    $bldDir = Join-Path $bld $name
    Write-Output "==== Configuring $name ===="
    $args = @(
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=gcc",
        "-DCMAKE_CXX_COMPILER=g++",
        "-DCMAKE_INSTALL_PREFIX=$install",
        "-DCMAKE_PREFIX_PATH=$install",
        "-DBUILD_SHARED_LIBS=OFF",
        "-S", $srcDir,
        "-B", $bldDir
    ) + $extraArgs
    & cmake @args
    if ($LASTEXITCODE -ne 0) { throw "$name configure failed" }
    Write-Output "==== Building $name ===="
    & cmake --build $bldDir
    if ($LASTEXITCODE -ne 0) { throw "$name build failed" }
    & cmake --install $bldDir
    if ($LASTEXITCODE -ne 0) { throw "$name install failed" }
    Write-Output "==== $name DONE ===="
}

# zlib
CMakeBuild "zlib" @("-DZLIB_BUILD_EXAMPLES=OFF")

# LibreSSL - disable ASM (gcc here is 32-bit i686, LibreSSL otherwise selects amd64 asm)
CMakeBuild "libressl" @("-DLIBRESSL_APPS=OFF", "-DLIBRESSL_TESTS=OFF", "-DENABLE_ASM=OFF")

Write-Output "DEPS_BUILD_DONE"
