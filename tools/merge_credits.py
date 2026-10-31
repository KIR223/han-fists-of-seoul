#!/usr/bin/env python3
"""Склеивает docs/credits/A.md и B.md в CREDITS.md (этап E15, перед релизом).
Каждая строка таблицы — один ассет. Проверяет, что в строке заполнены все колонки."""
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = "| Ассет | Автор | Источник (URL) | Лицензия | Где используется | Изменён? |"
rows, bad = [], []
for p in sorted(glob.glob(os.path.join(ROOT, "docs", "credits", "?.md"))):
    for n, line in enumerate(open(p, encoding="utf-8").read().splitlines(), 1):
        if not line.startswith("|") or line.startswith("| Ассет") or set(line) <= set("|- "):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 6 or any(not c for c in cells[:5]):
            bad.append(f"{os.path.relpath(p, ROOT)}:{n}: {line}")
        rows.append(line)
if bad:
    print("Неполные строки:\n" + "\n".join(bad))
    sys.exit(1)
out = os.path.join(ROOT, "CREDITS.md")
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("# CREDITS\n\nСобрано из docs/credits/*.md командой `python tools/merge_credits.py`.\n\n")
    f.write(HEADER + "\n|---|---|---|---|---|---|\n" + "\n".join(rows) + "\n")
print(f"CREDITS.md: {len(rows)} ассетов")
