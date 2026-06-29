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
