#!/usr/bin/env python3
"""Хук Stop: в конце работы Claude сам проверяет зоны и просит обновить прогресс (один раз за остановку)."""
import json
import sys

from _common import branch, effective_role, git, hz, read_input

data = read_input()
if data.get("stop_hook_active"):
    sys.exit(0)
eff, role = effective_role()
if not role:
    sys.exit(0)
rc, st = git("status", "--porcelain")
files = [l[3:].split(" -> ")[-1].strip('"') for l in st.splitlines()]
bad = hz.violations(files, eff) if files else []
msgs = []
if bad:
    msgs.append("В рабочей копии есть правки чужих зон: " + ", ".join(f for f, _ in bad[:8]) + ". Откати их (git checkout -- <файл>) и создай Issue владельцу зоны.")
main = role.split("+")[0]
prog = f"docs/progress/{main}.md"
if files and main != "OWNER" and prog not in files:
    rc, ahead = git("diff", "--name-only", "origin/main...HEAD", "--", prog)
    if not ahead:
        msgs.append(f"Обнови {prog} (что сделано, что дальше, блокеры), затем при готовности отправь работу: python tools/ship.py -m \"<что сделано по-русски>\".")
if msgs:
    print(json.dumps({"decision": "block", "reason": " ".join(msgs)}, ensure_ascii=False))
sys.exit(0)
