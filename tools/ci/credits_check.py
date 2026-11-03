#!/usr/bin/env python3
"""Проверка лицензий: каждый добавленный или заменённый ассет должен иметь запись в docs/credits/*.md.
  python tools/ci/credits_check.py --base origin/main --head HEAD
Запись считается найденной, если путь файла или любой его родительской папки встречается в первой колонке таблицы,
Лицензии проверяются классификатором конвейера. CC-BY-SA и другие needs-owner
требуют approved_by_owner: true в манифесте того же ассета. NC и ND запрещены.
"""
import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXT = {".png", ".jpg", ".jpeg", ".tga", ".dds", ".exr", ".hdr", ".svg", ".wav", ".ogg", ".mp3", ".flac", ".fbx", ".glb", ".gltf",
       ".obj", ".blend", ".ttf", ".otf", ".ktx2", ".mp4", ".webm", ".psd", ".kra"}
SKIP = ("docs/", "tests/", ".github/", "contracts/")
from credits_policy import license_allowed, owner_approved

ap = argparse.ArgumentParser()
ap.add_argument("--base", default="origin/main")
ap.add_argument("--head", default="HEAD")
a = ap.parse_args()
r = subprocess.run(["git", "diff", "--name-only", "-z", "--diff-filter=AM", f"{a.base}...{a.head}"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
if r.returncode:
    print(f"Не удалось сравнить коммиты: {r.stderr.strip()}", file=sys.stderr)
    sys.exit(2)
assets = [f for f in r.stdout.split("\0") if os.path.splitext(f)[1].lower() in EXT and not f.startswith(SKIP)]
if not assets:
    print("Новых или заменённых ассетов нет.")
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
    elif not any(license_allowed(c[3], owner_approved(c, ROOT)) for c in found):
        bad.append(f"{f} — лицензия «{found[0][3]}» не разрешена (нужна свободная: CC0, CC-BY, MIT, OFL…; NC/ND нельзя)")
if bad:
    print("[ERROR] Проблемы с лицензиями ассетов:\n  " + "\n  ".join(bad[:30]))
    sys.exit(1)
print(f"Лицензии ассетов в порядке ({len(assets)}).")
