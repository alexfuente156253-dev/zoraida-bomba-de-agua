# Compila el simulador (requiere g++; ej. winget install BrechtSanders.WinLibs.POSIX.UCRT)
$ErrorActionPreference = "Stop"
$sim = $PSScriptRoot
New-Item -ItemType Directory -Force "$sim\build" | Out-Null
g++ -std=c++17 -O2 -static -Wall -Wno-unused-variable -I "$sim\arduino_stub" "$sim\sim.cpp" -o "$sim\build\sim.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "OK: $sim\build\sim.exe"
