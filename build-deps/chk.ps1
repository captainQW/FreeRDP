$out = @()
if (Test-Path "C:\Users\Administrator\Desktop\FreeRDP\build\build.ninja") { $out += "BUILD_DIR_OK" } else { $out += "NO_BUILD_DIR" }
if (Test-Path "C:\mingw64\bin\gcc.exe") { $out += "GCC_OK" } else { $out += "NO_GCC" }
$out | Out-File "C:\Users\Administrator\Desktop\FreeRDP\build-deps\chk.txt" -Encoding utf8
