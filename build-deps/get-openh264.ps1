# Download and decompress openh264.dll (64-bit) from Cisco releases
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = "C:\Users\Administrator\Desktop\FreeRDP\build-deps"
$out  = Join-Path $root "openh264"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# Candidate asset names across openh264 release versions (win64)
$versions = @("2.6.0","2.5.0","2.4.1","2.4.0")
$ok = $false

foreach ($v in $versions) {
    $candidates = @(
        "openh264-$v-win64.dll.bz2",
        "openh264-$v-win64.dll",
        "openh264-win64-$v.dll.bz2"
    )
    foreach ($name in $candidates) {
        $url = "https://github.com/cisco/openh264/releases/download/v$v/$name"
        $dest = Join-Path $out $name
        try {
            Write-Output "Trying: $url"
            curl.exe -L --fail --retry 2 -o $dest $url
            if ($LASTEXITCODE -ne 0) { continue }
            if (-not (Test-Path $dest) -or (Get-Item $dest).Length -lt 10000) { continue }

            if ($name.EndsWith(".bz2")) {
                # decompress with tar (supports bzip2) by wrapping is not possible directly;
                # use .NET? no bz2. Use tar -xjf only works on tar.bz2. So use bzip2 via tar fallback:
                # tar can extract a raw .bz2? No. Use the embedded approach: rename to .tar.bz2 won't work.
                # Instead use System.IO + SharpZipLib not available. Use 'tar' cannot. So decode via certutil? no.
                # Fallback: use the bzip2 stream decoder in .NET via DeflateStream? Not bz2.
                # We rely on 7z/bzip2 if present:
                $bzip2 = Get-Command bzip2 -ErrorAction SilentlyContinue
                $sevenz = Get-Command 7z -ErrorAction SilentlyContinue
                $dllPath = Join-Path $out "openh264.dll"
                if ($bzip2) {
                    & bzip2 -d -k -f $dest
                    $decoded = $dest.Substring(0, $dest.Length - 4)
                    Move-Item $decoded $dllPath -Force
                } elseif ($sevenz) {
                    & 7z e $dest "-o$out" -y | Out-Null
                    $decoded = $dest.Substring(0, $dest.Length - 4)
                    if (Test-Path $decoded) { Move-Item $decoded $dllPath -Force }
                } else {
                    Write-Output "NO_BZ2_TOOL"
                    # leave the .bz2 for manual handling
                    $script:bz2only = $dest
                    continue
                }
                if (Test-Path $dllPath) { $script:resultDll = $dllPath; $ok = $true; break }
            } else {
                $dllPath = Join-Path $out "openh264.dll"
                Move-Item $dest $dllPath -Force
                $script:resultDll = $dllPath; $ok = $true; break
            }
        } catch {
            Write-Output "fail: $url -> $($_.Exception.Message)"
        }
    }
    if ($ok) { break }
}

if ($ok) {
    Write-Output "OPENH264_OK: $script:resultDll"
} else {
    Write-Output "OPENH264_NOT_DECODED"
    if ($script:bz2only) { Write-Output "BZ2_AT: $script:bz2only" }
}
