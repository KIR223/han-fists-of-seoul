#!/usr/bin/env python3
"""Добавить разработчика в проект ПОЗЖЕ, когда он завёл аккаунт GitHub (запускает владелец).

  python tools/add_member.py --role B --login <логин>

Что делает: записывает логин в tools/team.json (по нему JOIN_HAN.ps1 и join.py определяют роль), пересобирает CODEOWNERS,
отправляет приглашение в репозиторий (право записи), проверяет, что ветка <буква>/start есть, и вливает изменение в main
через Pull Request из ветки владельца o/…  После этого другу достаточно запустить JOIN_HAN.ps1.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(*cmd, check=True):
    print("  $ " + " ".join(cmd))
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode:
        print(r.stdout + r.stderr)
        sys.exit(f"✖ Не удалось: {' '.join(cmd)}")
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--role", required=True, choices=["A", "B"])
    ap.add_argument("--login", required=True)
    a = ap.parse_args()
    login = a.login.lstrip("@")
    p = os.path.join(ROOT, "tools", "team.json")
    team = json.load(open(p, encoding="utf-8"))
    full = team["repo"]
    if run("gh", "api", f"users/{login}", "--jq", ".login", check=False).returncode != 0:
        sys.exit(f"✖ На GitHub нет пользователя {login}. Проверь логин.")
    if run("git", "status", "--porcelain", check=False).stdout.strip():
        sys.exit("✖ Есть несохранённые изменения. Сначала отправь или убери их (python tools/ship.py …).")

    # роль больше никому не принадлежит (кроме составной роли владельца)
    team["logins"] = {k: v for k, v in team["logins"].items() if v != a.role}
    team["logins"][login] = a.role
    cur = run("git", "branch", "--show-current").stdout.strip()
    run("git", "fetch", "origin")
    br = f"o/team-{a.role.lower()}-{login.lower()}"
    run("git", "checkout", "-B", br, "origin/main")
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        json.dump(team, f, ensure_ascii=False, indent=2)
    run(sys.executable, "tools/set_owners.py", "--from-team")
    run("git", "add", "tools/team.json", ".github/CODEOWNERS")
    run("git", "commit", "-m", f"Команда: добавлен разработчик {a.role} ({login})")
    run("git", "push", "-u", "origin", br)
    pr = run("gh", "pr", "create", "--base", "main", "--head", br, "--title", f"Команда: добавлен разработчик {a.role} ({login})",
             "--body", "Логин в tools/team.json и CODEOWNERS. Создано tools/add_member.py.")
    url = pr.stdout.strip()
    m = run("gh", "pr", "merge", url, "--squash", "--delete-branch", check=False)
    print("  ✔ влито в main" if m.returncode == 0 else f"  • PR создан, влей его сам: {url}")

    r = run("gh", "api", "--method", "PUT", f"repos/{full}/collaborators/{login}", "-f", "permission=push", check=False)
    print(f"  ✔ приглашение отправлено: {login}" if r.returncode == 0 else f"  ✖ приглашение не отправлено: {r.stderr.strip()}")
    letter = a.role.lower()
    if run("git", "ls-remote", "--exit-code", "--heads", "origin", f"{letter}/start", check=False).returncode != 0:
        run("git", "push", "origin", f"origin/main:refs/heads/{letter}/start", check=False)
    run("git", "checkout", cur or "main", check=False)
    print(f"""
ГОТОВО: {login} — разработчик {a.role}, его ветка {letter}/start.
  Отправь ему файл JOIN_HAN.ps1 (лежит в корне проекта). Он запустит его — скрипт сам примет приглашение, склонирует проект,
  определит роль по логину и запустит Claude Code. Ему останется написать «Начинай».""")


if __name__ == "__main__":
    main()
