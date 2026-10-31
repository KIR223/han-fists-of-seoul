"""Общие функции для скриптов GitHub Actions (используют gh CLI и переменную GH_TOKEN)."""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import han_zones as hz  # noqa: E402


def gh(*args, check=True):
    r = subprocess.run(["gh", *args], cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode:
        print(f"gh {' '.join(args)}\n{r.stderr}", file=sys.stderr)
        sys.exit(1)
    return r


def gh_json(*args):
    r = gh(*args, check=False)
    try:
        return json.loads(r.stdout or "null")
    except ValueError:
        return None


def team():
    p = os.path.join(ROOT, "tools", "team.json")
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {"logins": {}}


def ensure_label(name, color="ededed", desc=""):
    gh("label", "create", name, "--color", color, "--description", desc, check=False)


def zone_labels(files):
    zones = hz.load_zones()
    out, owner_files = set(), False
    for f in files:
        o = hz.owner_of(f, zones)
        if o in ("A", "B"):
            out.add("zone-" + o.lower())
        elif o == "OWNER":
            owner_files = True
    return out, owner_files
