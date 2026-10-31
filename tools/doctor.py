#!/usr/bin/env python3
"""Проверка и починка рабочего окружения.

  python tools/doctor.py          проверить всё и показать, что не так
  python tools/doctor.py --fix    поставить недостающее (Windows, winget) и поправить настройки
"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = "--fix" in sys.argv
WIN = os.name == "nt"
problems = []


def run(*c):
    try:
        return subprocess.run(c, cwd=ROOT, capture_output=True, text=True, timeout=60)
    except Exception as e:  # noqa: BLE001
        return subprocess.CompletedProcess(c, 1, "", str(e))


def line(ok, text, hint=""):
    print(("✔ " if ok else "✖ ") + text + ("" if ok else (f"\n    → {hint}" if hint else "")))
    if not ok:
        problems.append(text)


def winget(pkg, cmd):
    if not (FIX and WIN and shutil.which("winget")):
        return False
    print(f"   … ставлю {pkg}")
    run("winget", "install", "--id", pkg, "-e", "--silent", "--accept-package-agreements", "--accept-source-agreements")
    return shutil.which(cmd) is not None


def tool(cmd, pkg, name, hint=None, optional=False):
    ok = shutil.which(cmd) is not None or winget(pkg, cmd)
    if optional and not ok:
        print(f"• {name} не найден (не обязателен для твоей роли)")
        return
    line(ok, f"{name} найден", hint or f"winget install --id {pkg} -e  (или python tools/doctor.py --fix)")


def main():
    print("== Программы ==")
    tool("git", "Git.Git", "Git")
    tool("python" if WIN else "python3", "Python.Python.3.12", "Python")
    line(sys.version_info >= (3, 11), f"Python {sys.version.split()[0]} (нужен 3.11+)", "Поставь Python 3.12")
    tool("gh", "GitHub.cli", "GitHub CLI")
    tool("cmake", "Kitware.CMake", "CMake")
    tool("claude", "", "Claude Code", "В PowerShell: irm https://claude.ai/install.ps1 | iex")
    tool("blender", "BlenderFoundation.Blender", "Blender", optional=True)
    r = run("git", "lfs", "version")
    line(r.returncode == 0, "Git LFS", "Ставится вместе с Git for Windows; затем: git lfs install")
    if WIN:
        vsw = os.path.join(os.environ.get("ProgramFiles(x86)", ""), "Microsoft Visual Studio", "Installer", "vswhere.exe")
        ok = os.path.exists(vsw) and bool(run(vsw, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath").stdout.strip())
        line(ok, "Visual Studio 2022 с C++ (рабочая нагрузка «Разработка классических приложений на C++»)", "Установщик Visual Studio → Изменить → отметить C++")
    vr = os.environ.get("VCPKG_ROOT")
    line(bool(vr and os.path.isdir(vr)), "vcpkg (VCPKG_ROOT)", "git clone https://github.com/microsoft/vcpkg C:\\vcpkg ; C:\\vcpkg\\bootstrap-vcpkg.bat ; задай переменную VCPKG_ROOT=C:\\vcpkg")

    print("\n== GitHub ==")
    ok = run("gh", "auth", "status").returncode == 0 if shutil.which("gh") else False
    line(ok, "Вход в GitHub (gh auth login)", "gh auth login")
    login = run("gh", "api", "user", "--jq", ".login").stdout.strip() if ok else ""
    line(run("git", "remote", "get-url", "origin").returncode == 0, "Репозиторий подключён (origin)", "git clone <ссылка> или (владелец) python tools/bootstrap_github.py")

    print("\n== Настройка проекта ==")
    line(run("git", "config", "core.hooksPath").stdout.strip() == ".githooks", "Хуки Git включены", "python tools/setup_local.py")
    role = None
    if os.path.exists(os.path.join(ROOT, "CLAUDE.local.md")):
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import han_zones
        role = han_zones.role_from_local()
    if not role and FIX:
        run(sys.executable, "tools/join.py")
        run(sys.executable, "tools/setup_local.py")
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import han_zones
        role = han_zones.role_from_local()
    line(bool(role), f"Роль определена: {role or '—'}", "python tools/join.py")
    line(os.path.exists(os.path.join(ROOT, ".claude", "settings.local.json")), "Личные запреты Claude (.claude/settings.local.json)", "python tools/join.py")
    tj = os.path.join(ROOT, "tools", "team.json")
    if os.path.exists(tj) and login:
        t = {k.lower(): v for k, v in json.load(open(tj, encoding="utf-8"))["logins"].items()}
        line(login.lower() in t, f"Логин {login} есть в списке команды", "Скажи владельцу добавить логин в tools/team.json")
    br = run("git", "branch", "--show-current").stdout.strip()
    print(f"• Текущая ветка: {br or '—'}")
    print()
    if problems:
        print(f"Проблем: {len(problems)}." + ("" if FIX else " Попробуй: python tools/doctor.py --fix"))
        return 1
    print("Всё в порядке. Запускай claude и пиши «Начинай».")
    return 0


if __name__ == "__main__":
    sys.exit(main())
