#!/usr/bin/env python3
"""Проверка лицензий ассетов: каждый НОВЫЙ ассет в PR должен быть записан в docs/credits/*.md с разрешённой лицензией.
  python tools/ci/credits_check.py --base origin/main --head HEAD
Запись считается найденной, если путь файла или любой его родительской папки встречается в первой колонке таблицы,
а колонка «Лицензия» содержит разрешённую лицензию (CC0, CC-BY, CC-BY-SA, MIT, Apache, OFL, BSD, Unlicense, Public Domain, «свой»).
Запрещены NC (некоммерческие) и ND.
"""
import argparse
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXT = {".png", ".jpg", ".jpeg", ".tga", ".dds", ".exr", ".hdr", ".svg", ".wav", ".ogg", ".mp3", ".flac", ".fbx", ".glb", ".gltf",
       ".obj", ".blend", ".ttf", ".otf", ".ktx2", ".mp4", ".webm", ".psd", ".kra"}
SKIP = ("docs/", "tests/", ".github/", "contracts/")
OK = re.compile(r"(cc0|cc[- ]by|mit|apache|ofl|bsd|unlicense|public domain|zlib|свой|own work|оригинал)", re.I)
BAD = re.compile(r"(\bnc\b|non-?commercial|\bnd\b|no-?derivs|некоммерч)", re.I)

ap = argparse.ArgumentParser()
ap.add_argument("--base", default="origin/main")
ap.add_argument("--head", default="HEAD")
a = ap.parse_args()
r = subprocess.run(["git", "diff", "--name-only", "--diff-filter=A", f"{a.base}...{a.head}"], cwd=ROOT, capture_output=True, text=True)
assets = [f for f in r.stdout.splitlines() if os.path.splitext(f)[1].lower() in EXT and not f.startswith(SKIP)]
if not assets:
    print("✔ Новых ассетов нет.")
    sys.exit(0)
rows = []
for p in glob.glob(os.path.join(ROOT, "docs", "credits", "?.md")):
    for line in open(p, encoding="utf-8").read().splitlines():
        if line.startswith("|") and not line.startswith("| Ассет") and not set(line) <= set("|- "):
            cells = [c.strip() for c in line.strip("|").split("|")]
            if len(cells) >= 4:
                rows.append(cells)
bad = []
for f in assets:
    parents = [f] + [f.rsplit("/", i)[0] + "/" for i in range(1, f.count("/") + 1)]
    found = [c for c in rows if any(x and x in c[0].replace("\\", "/") for x in parents)]
    if not found:
        bad.append(f"{f} — нет записи в docs/credits/<буква>.md (колонка «Ассет» должна содержать путь файла или папки)")
    elif not any(OK.search(c[3]) and not BAD.search(c[3]) for c in found):
        bad.append(f"{f} — лицензия «{found[0][3]}» не разрешена (нужна свободная: CC0, CC-BY, MIT, OFL…; NC/ND нельзя)")
if bad:
    print("✖ Проблемы с лицензиями ассетов:\n  " + "\n  ".join(bad[:30]))
    sys.exit(1)
print(f"✔ Лицензии новых ассетов в порядке ({len(assets)}).")
