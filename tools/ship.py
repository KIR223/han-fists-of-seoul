#!/usr/bin/env python3
"""Отправка работы одной командой: коммит (по-русски) → проверка зон → push в свою ветку → Pull Request → автослияние.

  python tools/ship.py -m "Бой: кадровая машина ударов (G1)" [--branch b/g1-core] [--draft] [--body "Подробности"]

Что делает:
  1. проверяет, что ты не в main и ветка — твоей буквы (на a/start, b/start или main сам создаст новую ветку);
  2. сообщение коммита обязано быть по-русски;
  3. проверяет зоны владения; коммитит все изменения; подтягивает свежий main (rebase);
  4. отправляет ветку и открывает Pull Request (заголовок = сообщение). Метки зон и автослияние делает GitHub Actions:
     если проверки зелёные и PR не трогает контракты/файлы владельца, он вливается сам.
"""
import argparse
import datetime
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import han_zones as hz  # noqa: E402
from commit_msg_check import is_russian  # noqa: E402


def sh(*cmd, check=True, quiet=False):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode:
        print(f"✖ Не удалось: {' '.join(cmd)}\n{r.stdout}{r.stderr}")
        sys.exit(1)
    return r


def slug(text):
    tr = dict(zip("абвгдеёжзийклмнопрстуфхцчшщъыьэюя", "a b v g d e e zh z i y k l m n o p r s t u f h c ch sh sch _ y _ e yu ya".split()))
    s = "".join(tr.get(ch, ch) for ch in text.lower())
    return re.sub(r"[^a-z0-9]+", "-", s).strip("-")[:30] or "work"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-m", "--message", required=True, help="что сделано (по-русски)")
    ap.add_argument("--branch")
    ap.add_argument("--body", default="")
    ap.add_argument("--draft", action="store_true")
    ap.add_argument("--no-pr", action="store_true")
    a = ap.parse_args()

    if not is_russian(a.message):
        print("✖ Сообщение должно быть на русском. Пример: -m \"Бой: кадровая машина ударов (G1)\"")
        return 1
    role = hz.role_from_local()
    if not role:
        print("✖ Роль не определена. Выполни: python tools/join.py")
        return 1
    prefixes = sorted(hz.allowed_prefixes(role))
    cur = sh("git", "branch", "--show-current").stdout.strip()
    dev_prefix = [p for p in prefixes if p != "o"] or prefixes
    if a.branch:
        target = a.branch
    elif cur in ("", "main") or cur.endswith("/start"):
        target = f"{(cur.split('/')[0] if cur.endswith('/start') else dev_prefix[0])}/{datetime.date.today():%m%d}-{slug(a.message)}"
    else:
        target = cur
    if target.split("/")[0] not in prefixes or "/" not in target:
        print(f"✖ Ветка {target} не твоя. Твои ветки: {', '.join(p + '/…' for p in prefixes)}")
        return 1
    if target != cur:
        sh("git", "checkout", "-b", target)
        print(f"→ Новая ветка {target}")

    eff = hz.role_from_branch(target) if hz.role_from_branch(target) in hz.held(role) else role
    status = sh("git", "status", "--porcelain").stdout.strip()
    if status:
        files = [l[3:].split(" -> ")[-1].strip('"') for l in status.splitlines()]
        bad = hz.violations(files, eff)
        if bad:
            hz.report(bad, eff)
            return 1
        sh("git", "add", "-A")
        sh("git", "commit", "-m", a.message)
        print("✔ Коммит создан")
    else:
        print("• Новых изменений нет — отправляю уже сделанные коммиты")

    sh("git", "fetch", "origin")
    if sh("git", "rev-parse", "--verify", "-q", "origin/main", check=False).returncode == 0:
        r = sh("git", "rebase", "origin/main", check=False)
        if r.returncode:
            sh("git", "rebase", "--abort", check=False)
            print("✖ Конфликт при подтягивании main. Реши его: git pull --rebase origin main (конфликтующие файлы чужих зон не правь — напиши владельцу).")
            return 1
        base = "origin/main"
        files = [f for f in sh("git", "diff", "--name-only", f"{base}...HEAD").stdout.split("\n") if f]
        bad = hz.violations(files, eff)
        if bad:
            hz.report(bad, eff)
            return 1
        if not files:
            print("✖ Нечего отправлять: ветка не отличается от main.")
            return 1
    push = sh("git", "push", "-u", "origin", target, check=False)
    if push.returncode:
        print(push.stdout + push.stderr)
        return 1
    print(f"✔ Ветка {target} отправлена на GitHub")
    if a.no_pr:
        return 0

    exists = sh("gh", "pr", "view", target, "--json", "url", "--jq", ".url", check=False)
    if exists.returncode == 0 and exists.stdout.strip():
        print(f"✔ Pull Request уже есть, обновлён: {exists.stdout.strip()}")
        return 0
    tpl = open(os.path.join(ROOT, ".github", "pull_request_template.md"), encoding="utf-8").read()
    body = (a.body + "\n\n" if a.body else "") + tpl
    cmd = ["gh", "pr", "create", "--base", "main", "--head", target, "--title", a.message, "--body", body]
    if a.draft:
        cmd.append("--draft")
    r = sh(*cmd, check=False)
    if r.returncode:
        print("⚠ Pull Request не создан (gh не авторизован?):\n" + r.stderr)
        return 1
    print(f"✔ Pull Request: {r.stdout.strip()}")
    print("Дальше всё автоматически: проверки → метки → слияние в main (если PR не трогает контракты и файлы владельца; тогда его одобряет владелец).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
