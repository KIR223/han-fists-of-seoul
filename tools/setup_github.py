#!/usr/bin/env python3
"""Создаёт метки (labels) в GitHub-репозитории. Запускает владелец один раз после первого push.

  gh auth login
  python tools/setup_github.py

Защиту ветки main настраивай в веб-интерфейсе (см. docs/team/OWNER_CHECKLIST.md) — на бесплатном плане
для приватных репозиториев она может быть недоступна; тогда работают хуки и правила Claude.
"""
import shutil
import subprocess
import sys

LABELS = [
    ("zone-a", "0e8a16", "Зона A: движок, мир, город, транспорт"),
    ("zone-b", "d93f0b", "Зона B: бой, сюжет, интерфейс, игровые системы"),
    ("contract", "fbca04", "Изменение общего интерфейса (нужно одобрение всех)"),
    ("blocked", "b60205", "Жду другую зону"),
    ("bug", "ee0701", "Ошибка"),
    ("legal", "5319e7", "Лицензии, бренды, авторские права"),
    ("playtest", "c5def5", "Замечание после игры в недельную сборку"),
]

if not shutil.which("gh"):
    print("Нет GitHub CLI (gh). Поставь: https://cli.github.com/")
    sys.exit(1)
for name, color, desc in LABELS:
    r = subprocess.run(["gh", "label", "create", name, "--color", color, "--description", desc, "--force"],
                       capture_output=True, text=True)
    print(("✔ " if r.returncode == 0 else "✖ ") + name + ("" if r.returncode == 0 else " — " + r.stderr.strip()))
