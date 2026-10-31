#!/usr/bin/env python3
"""Автоматическое подключение разработчика к проекту (запускается после клонирования репозитория).

  python tools/join.py [--login <логин GitHub>]

Определяет роль по логину GitHub (список — tools/team.json, его создал владелец), создаёт личные файлы,
настраивает хуки Git и переключает на твою стартовую ветку (a/start или b/start).
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREFIX = {"A": "a", "B": "b", "OWNER": "o"}


def sh(*cmd, check=False):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode:
        print(r.stdout + r.stderr)
        sys.exit(1)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--login")
    a = ap.parse_args()
    login = a.login or sh("gh", "api", "user", "--jq", ".login").stdout.strip()
    if not login:
        print("✖ Не удалось узнать логин GitHub. Выполни `gh auth login` и запусти снова (или добавь --login <логин>).")
        return 1
    path = os.path.join(ROOT, "tools", "team.json")
    if not os.path.exists(path):
        print("✖ Нет tools/team.json — владелец ещё не выполнил tools/bootstrap_github.py.")
        return 1
    team = json.load(open(path, encoding="utf-8"))["logins"]
    role = {k.lower(): v for k, v in team.items()}.get(login.lower())
    if not role:
        print(f"✖ Логина {login} нет в списке команды (tools/team.json). Скажи владельцу, чтобы добавил тебя.")
        return 1
    print(f"Логин {login} → роль {role}")
    py = sys.executable
    sh(py, "tools/make_personal.py", "--role", role, check=True)
    r = sh(py, "tools/setup_local.py")
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "")
    if not sh("git", "config", "user.email").stdout.strip():
        uid = sh("gh", "api", "user", "--jq", ".id").stdout.strip()
        if uid:
            sh("git", "config", "user.email", f"{uid}+{login}@users.noreply.github.com")
            sh("git", "config", "user.name", login)
    letter = PREFIX[role.split("+")[0]]
    sh("git", "fetch", "origin")
    cur = sh("git", "branch", "--show-current").stdout.strip()
    if cur in ("main", ""):
        c = sh("git", "checkout", f"{letter}/start")
        print(f"Ветка: {letter}/start" if c.returncode == 0 else f"⚠ Не удалось перейти на {letter}/start: {c.stderr.strip()}")
    print(f"""
ГОТОВО. Ты — {role}. На GitHub можно отправлять только ветки {letter}/…
Теперь запусти:  claude   и напиши:  Начинай""")
    return 0


if __name__ == "__main__":
    sys.exit(main())
