$exe = "C:\Users\Administrator\Desktop\FreeRDP\build\client\Windows\cli\wfreerdp.exe"
$out = @()
if (Test-Path $exe) {
    $t = (Get-Item $exe).LastWriteTime
    $age = (Get-Date) - $t
    $out += "EXE_EXISTS"
    $out += "EXE_MTIME=$t"
    $out += "EXE_AGE_MIN=" + [math]::Round($age.TotalMinutes,1)
} else {
    $out += "EXE_MISSING"
}
$tail = Get-Content "C:\Users\Administrator\Desktop\FreeRDP\build-deps\rebuild.log" -Tail 6
$out += "---LOG_TAIL---"
$out += $tail
$out | Out-File "C:\Users\Administrator\Desktop\FreeRDP\build-deps\chk2.txt" -Encoding utf8
