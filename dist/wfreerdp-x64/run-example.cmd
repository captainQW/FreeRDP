@echo off
REM Example launcher. Edit host/user as needed.
REM Black-block concealment is enabled via /gfx:AVC444,conceal-black
setlocal
set HERE=%~dp0
REM Point OpenSSL at the bundled legacy provider (MD4/RC4) so NTLM/NLA works.
set OPENSSL_MODULES=%HERE%ossl-modules
"%HERE%wfreerdp.exe" /v:%1 /u:%2 /gfx:AVC444,conceal-black /dynamic-resolution +clipboard /cert:ignore /from-stdin
endlocal
