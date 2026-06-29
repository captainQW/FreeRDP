@echo off
REM Seamless RemoteApp launcher.
REM Usage: run-remoteapp.cmd HOST:PORT USER PASSWORD "C:\Path\To\App.exe"
setlocal
set HERE=%~dp0
REM Point OpenSSL at the bundled legacy provider (MD4/RC4) so NTLM/NLA works.
set OPENSSL_MODULES=%HERE%ossl-modules
"%HERE%wfreerdp.exe" /v:%1 /u:%2 /p:%3 /app:program:"%~4" /cert:ignore /gfx:AVC444,conceal-black +clipboard /dynamic-resolution
endlocal
