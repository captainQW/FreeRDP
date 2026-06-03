# Configure and build FreeRDP Windows client (wfreerdp) with MinGW gcc + ninja
$ErrorActionPreference = "Stop"

$repo    = "C:\Users\Administrator\Desktop\FreeRDP"
$install = Join-Path $repo "build-deps\install"
$bld     = Join-Path $repo "build"

$args = @(
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=gcc",
    "-DCMAKE_CXX_COMPILER=g++",
    # This MinGW is the win32 threads variant which ships no C11 <threads.h> but
    # does not advertise it, so tell winpr/platform.h to use the __thread fallback.
    # GCC 14+ promotes several pointer/int conversion diagnostics to hard errors
    # by default; this codebase targets older compilers, so downgrade them back
    # to warnings to build with the newer toolchain.
    "-DCMAKE_C_FLAGS=-D__STDC_NO_THREADS__ -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=int-to-pointer-cast",
    "-DCMAKE_CXX_FLAGS=-D__STDC_NO_THREADS__",
    "-DCMAKE_PREFIX_PATH=$install",
    "-DZLIB_ROOT=$install",
    # Use real OpenSSL (LibreSSL's TLS handshake is rejected by Windows RDP
    # servers with a fatal 'internal error' alert).
    "-DOPENSSL_ROOT_DIR=$install",
    "-DBUILD_SHARED_LIBS=ON",
    # client/server selection: build the Windows client only
    "-DWITH_CLIENT=ON",
    "-DWITH_CLIENT_SDL=OFF",
    "-DWITH_CLIENT_SDL2=OFF",
    "-DWITH_CLIENT_SDL3=OFF",
    "-DWITH_SERVER=OFF",
    "-DWITH_SHADOW=OFF",
    "-DWITH_SAMPLE=OFF",
    # disable heavy / unavailable optional deps
    "-DWITH_FFMPEG=OFF",
    "-DWITH_SWSCALE=OFF",
    # H.264 via OpenH264 in *loading* mode: only headers are needed at build
    # time, openh264.dll is loaded at runtime. This enables WITH_GFX_H264 which
    # the AVC420/AVC444 codecs and the black-block concealment feature require.
    "-DWITH_OPENH264=ON",
    "-DWITH_OPENH264_LOADING=ON",
    "-DOPENH264_INCLUDE_DIR=$install\include",
    "-DOPENH264_LIBRARY=$install\include\wels\codec_api.h",
    "-DWITH_OPUS=OFF",
    "-DWITH_FDK_AAC=OFF",
    "-DWITH_WEBVIEW=OFF",
    "-DWITH_MANPAGES=OFF",
    "-DWITH_SIMD=ON",
    "-DUSE_UNWIND=OFF",
    "-DWITH_WINPR_TOOLS=ON",
    "-DBUILD_TESTING=OFF",
    # URBDRC (USB redirection) needs libusb which is not available here
    "-DCHANNEL_URBDRC=OFF",
    "-S", $repo,
    "-B", $bld
)

Write-Output "==== Configuring FreeRDP ===="
& cmake @args
if ($LASTEXITCODE -ne 0) { throw "FreeRDP configure failed" }

Write-Output "==== Building wfreerdp ===="
& cmake --build $bld --target wfreerdp
if ($LASTEXITCODE -ne 0) { throw "wfreerdp build failed" }

Write-Output "FREERDP_BUILD_DONE"
