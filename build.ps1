# RCNotchMonitor — Windows Dynamic Notch Hardware Monitor Build Script
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$CompilerDir = Join-Path $ProjectRoot "deps\w64devkit\bin"
$DistDir = Join-Path $ProjectRoot "dist"

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
if (Test-Path $CompilerDir) {
    $env:PATH = "$CompilerDir;$env:PATH"
}

# Clean up any unlocked old binaries first
Get-ChildItem -Path $DistDir -Filter "*NotchMonitor.old*.exe" -ErrorAction SilentlyContinue | ForEach-Object {
    Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
}

$TargetExe = Join-Path $DistDir "RCNotchMonitor.exe"
if (Test-Path $TargetExe) {
    $Stamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $OldExe = Join-Path $DistDir "RCNotchMonitor.old.$Stamp.exe"
    Move-Item $TargetExe $OldExe -Force
}

$ResObj = Join-Path $ProjectRoot "src\app_res.o"
windres.exe -I "$ProjectRoot\src" "$ProjectRoot\src\app.rc" -O coff -o "$ResObj"
if ($LASTEXITCODE -ne 0) {
    throw "windres.exe failed with exit code $LASTEXITCODE"
}

Write-Host "[1/2] Compiling RCNotchMonitor.exe (C++17 + Direct2D + DirectWrite + Icon Resource)..." -ForegroundColor Cyan
g++.exe -std=c++17 -O2 -municode -mwindows `
    "$ProjectRoot\src\main.cpp" `
    "$ProjectRoot\src\config.cpp" `
    "$ProjectRoot\src\telemetry.cpp" `
    "$ProjectRoot\src\notch_window.cpp" `
    "$ProjectRoot\src\settings_window.cpp" `
    "$ResObj" `
    -o "$TargetExe" `
    -ld2d1 -ldwrite -ldwmapi -lpowrprof -lpsapi -ladvapi32 -lcomdlg32 -lgdi32 -luser32 -lshell32 -lole32 `
    -static-libgcc -static-libstdc++ -s

if ($LASTEXITCODE -ne 0) {
    throw "g++.exe compilation failed with exit code $LASTEXITCODE"
}

$exeInfo = Get-Item "$TargetExe"
Write-Host "[2/2] Build Successful! Output: $($exeInfo.FullName) ($([math]::Round($exeInfo.Length / 1KB, 1)) KB)" -ForegroundColor Green
