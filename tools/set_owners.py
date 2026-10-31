#!/usr/bin/env python3
"""Пересобирает .github/CODEOWNERS из tools/zones.json с настоящими логинами GitHub.

  python tools/set_owners.py --owner <логин владельца> [--a <логин A>] [--b <логин B>]
  python tools/set_owners.py --from-team        (взять логины из tools/team.json)

Роль без логина (друг ещё не завёл GitHub) временно закрепляется за владельцем.
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write(owners):
    zones = json.load(open(os.path.join(ROOT, "tools", "zones.json"), encoding="utf-8"))["zones"]
    at = {k: "@" + v.lstrip("@") for k, v in owners.items()}
    at["shared"] = " ".join(sorted({at["A"], at["B"]}))
    lines = ["# Автоматически создан из tools/zones.json командой tools/set_owners.py — вручную не править.",
             "# Последнее совпавшее правило побеждает, поэтому общие папки идут выше частных.", "", f"* {at['OWNER']}", ""]
    for z in sorted(zones, key=lambda z: (len(z["path"]), z["path"])):
        lines.append(f"/{z['path']:<28} {at[z.get('codeowners', z['owner'])]}")
    with open(os.path.join(ROOT, ".github", "CODEOWNERS"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def from_team():
    t = json.load(open(os.path.join(ROOT, "tools", "team.json"), encoding="utf-8"))["logins"]
    owners = {}
    for login, role in t.items():
        for r in role.split("+"):
            owners.setdefault(r, login)
    return owners


def main():
    ap = argparse.ArgumentParser()
    for k in ("owner", "a", "b"):
        ap.add_argument(f"--{k}")
    ap.add_argument("--from-team", action="store_true")
    a = ap.parse_args()
    owners = from_team() if a.from_team else {}
    owner = a.owner or owners.get("OWNER")
    if not owner:
        sys.exit("нужен --owner или --from-team")
    owners["OWNER"] = owner
    for k in ("a", "b"):
        if getattr(a, k):
            owners[k.upper()] = getattr(a, k)
        owners.setdefault(k.upper(), owner)
    write(owners)
    print("CODEOWNERS обновлён: " + ", ".join(f"{k}={v}" for k, v in sorted(owners.items())))


if __name__ == "__main__":
    main()
