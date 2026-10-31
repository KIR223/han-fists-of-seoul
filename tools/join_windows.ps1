# HAN: Fists of Seoul — установка и подключение одним скриптом (Windows 10/11).
# Запуск: ПКМ по файлу -> «Выполнить с помощью PowerShell»  или в PowerShell:  powershell -ExecutionPolicy Bypass -File JOIN_HAN.ps1
# Параметры: -Blender (поставить Blender, нужен B и A)  -SkipVS (не ставить Visual Studio)
param([switch]$Blender, [switch]$SkipVS)
$ErrorActionPreference = "Continue"
$Repo = "KIR223/han-fists-of-seoul"
$Dir = Join-Path $HOME "han-fists-of-seoul"

function Has($c) { return [bool](Get-Command $c -ErrorAction SilentlyContinue) }
function RefreshPath { $env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User") }
function Install($cmd, $id, $extra) {
  if (Has $cmd) { Write-Host "✔ $cmd уже есть"; return }
  Write-Host "→ Устанавливаю $id ..."
  winget install --id $id -e --silent --accept-package-agreements --accept-source-agreements $extra
  RefreshPath
}

if (-not (Has winget)) { Write-Host "✖ Нет winget. Обнови «Установщик приложений» в Microsoft Store и запусти снова."; exit 1 }
Install git Git.Git
Install python Python.Python.3.12
Install gh GitHub.cli
Install cmake Kitware.CMake
RefreshPath
if ($Blender) { Install blender BlenderFoundation.Blender }
if (-not $SkipVS -and -not (Test-Path "C:\Program Files\Microsoft Visual Studio\2022")) {
  Write-Host "→ Visual Studio 2022 Community (C++), это долго (несколько ГБ) ..."
  winget install --id Microsoft.VisualStudio.2022.Community -e --silent --accept-package-agreements --accept-source-agreements --override "--passive --wait --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
}
if (-not (Has claude)) { Write-Host "→ Устанавливаю Claude Code ..."; irm https://claude.ai/install.ps1 | iex; RefreshPath; $env:Path += ";$HOME\.local\bin" }
git lfs install | Out-Null

# vcpkg (нужен для сборки C++)
if (-not (Test-Path "C:\vcpkg")) { git clone https://github.com/microsoft/vcpkg C:\vcpkg; & C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics }
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User"); $env:VCPKG_ROOT = "C:\vcpkg"

# Вход в GitHub
gh auth status 2>$null
if ($LASTEXITCODE -ne 0) { Write-Host "→ Войди в GitHub (откроется браузер) ..."; gh auth login -w -p https -h github.com; gh auth setup-git }

# Принять приглашение в репозиторий автоматически
$inv = gh api /user/repository_invitations --jq ".[] | select(.repository.full_name==`"$Repo`") | .id" 2>$null
foreach ($id in $inv) { gh api --method PATCH /user/repository_invitations/$id | Out-Null; Write-Host "✔ Приглашение принято" }

# Клонирование и автонастройка роли
if (-not (Test-Path $Dir)) { gh repo clone $Repo $Dir }
Set-Location $Dir
git pull --ff-only 2>$null | Out-Null
python tools/join.py
if ($LASTEXITCODE -ne 0) { Write-Host "✖ Подключение не удалось, см. сообщение выше."; exit 1 }
Write-Host "`nЗапускаю Claude Code. Напиши ему: Начинай"
claude
