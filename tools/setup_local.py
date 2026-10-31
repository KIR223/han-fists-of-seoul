#!/usr/bin/env python3
"""Настройка клона репозитория на компьютере разработчика (запускать один раз).

  python tools/setup_local.py          настроить и проверить программы
  python tools/setup_local.py --check  только проверить

Что делает: включает хуки Git (защита main и зон), ставит Git LFS, проверяет, что нужные программы найдены.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
check_only = "--check" in sys.argv


def run(*a):
    return subprocess.run(a, cwd=ROOT, capture_output=True, text=True)


def have(cmd):
    return shutil.which(cmd) is not None


def main():
    ok = True
    print("== Git ==")
    if not have("git"):
        print("✖ Git не найден. Поставь Git for Windows: https://git-scm.com/downloads/win")
        return 1
    hp = run("git", "config", "core.hooksPath").stdout.strip()
    hook = os.path.join(ROOT, ".githooks", "pre-push")
    if os.name == "posix":
        for h in ("pre-push", "commit-msg"):
            hp_ = os.path.join(ROOT, ".githooks", h)
            if os.path.exists(hp_):
                os.chmod(hp_, 0o755)
    if hp != ".githooks":
        if check_only:
            print("✖ Хуки не включены. Запусти: python tools/setup_local.py")
            ok = False
        else:
            run("git", "config", "core.hooksPath", ".githooks")
            print("✔ Хуки включены (core.hooksPath = .githooks)")
    else:
        print("✔ Хуки включены")
    if run("git", "lfs", "version").returncode == 0:
        if not check_only:
            run("git", "lfs", "install", "--local")
        print("✔ Git LFS найден")
    else:
        print("✖ Git LFS не найден (обычно ставится вместе с Git for Windows).")
        ok = False

    print("\n== Роль ==")
    p = os.path.join(ROOT, "CLAUDE.local.md")
    if os.path.exists(p) and "РОЛЬ:" in open(p, encoding="utf-8").read():
        print("✔ " + open(p, encoding="utf-8").read().splitlines()[0])
    else:
        print("✖ Нет CLAUDE.local.md. Запусти: python tools/make_personal.py --role A|B|A+OWNER")
        ok = False

    print("\n== Программы ==")
    need = [("claude", "Claude Code (см. docs/team/SETUP.md)"),
            ("cmake", "CMake — https://cmake.org/download/"),
            ("python", "Python 3 — https://www.python.org/downloads/"),
            ("gh", "GitHub CLI — https://cli.github.com/ (затем gh auth login)")]
    for cmd, hint in need:
        alt = {"python": ["python", "python3", "py"]}.get(cmd, [cmd])
        if any(have(c) for c in alt):
            print(f"✔ {cmd}")
        else:
            print(f"✖ {cmd} не найден: {hint}")
            ok = False
    for cmd, hint in [("cl", "компилятор Visual Studio (открой «Developer PowerShell for VS» — там он есть)"),
                      ("vcpkg", "vcpkg: https://github.com/microsoft/vcpkg (переменная VCPKG_ROOT)"),
                      ("blender", "Blender (нужен A для конвертации ассетов)")]:
        print(("✔ " if have(cmd) else "• не найден (проверь позже): ") + cmd + ("" if have(cmd) else f" — {hint}"))
    if not os.environ.get("VCPKG_ROOT"):
        print("• переменная VCPKG_ROOT не задана (нужна для сборки C++)")
    print("\n" + ("Всё готово." if ok else "Есть пункты ✖ — исправь их по подсказкам."))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
