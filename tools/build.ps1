# Сборка и тесты одной командой (Windows).  Использование:  powershell -File tools/build.ps1 [-Config Release|Debug|RelWithDebInfo] [-NoTest] [-Run]
param([ValidateSet("Release","Debug","RelWithDebInfo")][string]$Config = "Release", [switch]$NoTest, [switch]$Run)
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not (Test-Path CMakePresets.json)) { Write-Host "Каркас сборки ещё не готов: его делает разработчик A на этапе E0 (CMakePresets.json)."; exit 0 }
if (-not $env:VCPKG_ROOT) { Write-Host "✖ Не задан VCPKG_ROOT. Запусти: python tools/doctor.py --fix"; exit 1 }
$preset = switch ($Config) { "Release" {"windows-release"} "Debug" {"windows-debug"} "RelWithDebInfo" {"windows-relwithdebinfo"} }
Write-Host "→ Настройка ($preset)"; cmake --preset $preset; if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "→ Сборка";             cmake --build --preset $preset --parallel; if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not $NoTest) { Write-Host "→ Тесты"; ctest --preset $preset --output-on-failure; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
if ($Run) { $exe = Get-ChildItem -Recurse -Filter "han_game.exe" -Path build | Select-Object -First 1; if ($exe) { & $exe.FullName } else { Write-Host "han_game.exe пока нет." } }
Write-Host "✔ Готово"
