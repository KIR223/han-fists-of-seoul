#!/usr/bin/env python3
"""Хук SessionStart: готовит рабочую копию к работе и выдаёт Claude краткую сводку (stdout попадает в контекст сессии)."""
import os
import re
import subprocess
import sys

from _common import ROOT, branch, git, hz

out = []
role = hz.role_from_local()
if not role and os.path.exists(os.path.join(ROOT, "tools", "team.json")):
    r = subprocess.run([sys.executable, "tools/join.py"], cwd=ROOT, capture_output=True, text=True)
    out.append("Автоподключение (tools/join.py): " + (r.stdout.strip().splitlines() or ["нет вывода"])[-1] + (r.stderr.strip()[:200] if r.returncode else ""))
    role = hz.role_from_local()
if not git("remote", "get-url", "origin")[0] == 0:
    out.append("Репозитория на GitHub ещё нет — если ты владелец, выполни п. 3 из CLAUDE.md (tools/bootstrap_github.py).")
else:
    git("fetch", "-q", "origin")
    br = branch()
    dirty = git("status", "--porcelain")[1]
    if br and br != "main" and not dirty:
        published = git("rev-parse", "--verify", "-q", f"origin/{br}")[0] == 0
        rc = git("merge", "--ff-only", "origin/main")[0] if published else git("rebase", "origin/main")[0]
        if rc != 0:
            git("rebase", "--abort")
            out.append("Ветку не удалось автоматически подтянуть к main (конфликт или своя история) — реши вручную: git pull --rebase origin main.")
        else:
            out.append(f"Ветка {br} подтянута к origin/main.")
    rc, diff = git("diff", "--name-only", "HEAD", "origin/main", "--", "contracts/")
    if diff:
        out.append("⚠ В main изменились контракты (интерфейсы между зонами): " + ", ".join(diff.splitlines()[:8]) +
                   ". Прочитай и адаптируй свой код (contracts/registry.md).")
out.insert(0, f"Роль: {role or 'не определена'}; ветка: {branch() or '—'}. Сообщения коммитов — только на русском. Отправка работы: python tools/ship.py -m \"Что сделано\".")
print("\n".join(out))
