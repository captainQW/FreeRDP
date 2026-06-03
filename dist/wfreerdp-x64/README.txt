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
