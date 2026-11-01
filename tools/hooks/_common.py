"""Общие функции хуков Claude Code (читают JSON со stdin, знают роль, ветку и зоны)."""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import han_zones as hz  # noqa: E402


def read_input():
    try:
        return json.loads(sys.stdin.read() or "{}")
    except Exception:
        return {}


def git(*a):
    r = subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout.strip()


def branch():
    return git("branch", "--show-current")[1]


def effective_role():
    """Роль, по правилам которой проверяем правки сейчас (для A+OWNER зависит от префикса ветки)."""
    role = hz.role_from_local()
    if not role:
        return None, None
    if "SOLO" in role.upper().split("+"):
        return role, role  # соло-режим: все зоны независимо от префикса ветки
    br = hz.role_from_branch(branch())
    return (br if br in hz.held(role) else role), role


def rel(path):
    p = os.path.abspath(path if os.path.isabs(path) else os.path.join(ROOT, path))
    r = os.path.relpath(p, ROOT).replace("\\", "/")
    return None if r.startswith("..") else r


def deny(msg):
    print(msg, file=sys.stderr)
    sys.exit(2)
