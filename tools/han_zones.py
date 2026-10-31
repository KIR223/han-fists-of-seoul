#!/usr/bin/env python3
"""Проверка зон владения (чтобы никто не правил чужие папки) и защита main.

Использование:
  python tools/han_zones.py check --base origin/main --head HEAD [--role B | --branch b/g1-core]
  python tools/han_zones.py pre-push <remote> <url>      (вызывается хуком .githooks/pre-push, читает stdin)
  python tools/han_zones.py who <путь>                    (кто владеет путём)

Аварийные переменные окружения (только для владельца):
  HAN_ALLOW_MAIN=1   разрешить push в main
  HAN_ALLOW_ZONE=1   отключить проверку зон
  HAN_ROLE=B         задать роль вручную
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZERO = "0" * 40
BRANCH_ROLE = {"a": "A", "b": "B", "o": "OWNER"}


def git(*args):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return r.returncode, r.stdout.strip()


def load_zones():
    with open(os.path.join(ROOT, "tools", "zones.json"), encoding="utf-8") as f:
        zs = json.load(f)["zones"]
    return sorted(zs, key=lambda z: len(z["path"]), reverse=True)


def owner_of(path, zones=None):
    zones = zones or load_zones()
    path = path.replace("\\", "/").lstrip("./")
    for z in zones:
        p = z["path"]
        if (p.endswith("/") and path.startswith(p)) or path == p:
            return z["owner"]
    return None  # ничьё — можно всем


PREFIX = {"A": "a", "B": "b", "OWNER": "o"}


def allowed_prefixes(role):
    return {PREFIX[r] for r in held(role) if r in PREFIX}


def role_from_local():
    env = os.environ.get("HAN_ROLE")
    if env:
        return env.strip().upper()
    p = os.path.join(ROOT, "CLAUDE.local.md")
    if os.path.exists(p):
        m = re.search(r"РОЛЬ:\s*((?:OWNER|[ABCabc])(?:\+(?:OWNER|[ABCabc]))*)", open(p, encoding="utf-8").read())
        if m:
            return m.group(1).upper()
    return None


def role_from_branch(branch):
    m = re.match(r"^(?:refs/heads/)?([abo])/", branch or "")
    return BRANCH_ROLE[m.group(1)] if m else None


def held(role):
    """Роль может быть составной: A+OWNER (разработчик A, он же владелец)."""
    return set((role or "").upper().split("+"))


def violations(files, role):
    roles = held(role)
    zones = load_zones()
    bad = []
    for f in files:
        o = owner_of(f, zones)
        if o is None or o == "shared" or o in roles:
            continue
        bad.append((f, o))
    return bad


def changed_files(base, head):
    rc, out = git("diff", "--name-only", f"{base}...{head}")
    if rc != 0:
        return None
    return [l for l in out.splitlines() if l]


def report(bad, role):
    print(f"\n✖ Роль {role} трогает чужие зоны:", file=sys.stderr)
    for f, o in bad[:30]:
        print(f"    {f}   (владелец: {o})", file=sys.stderr)
    if len(bad) > 30:
        print(f"    … и ещё {len(bad) - 30}", file=sys.stderr)
    print("  Откати эти файлы и создай Issue для владельца зоны (см. docs/team/WORKFLOW.md).\n"
          "  Если это общий интерфейс — он должен лежать в contracts/.", file=sys.stderr)


def cmd_check(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--role")
    ap.add_argument("--branch")
    a = ap.parse_args(argv)
    role = (a.role or role_from_branch(a.branch) or role_from_local() or "").upper() or None
    if not role:
        print("Роль не определена (имя ветки a/ b/ o/ или CLAUDE.local.md) — проверка пропущена.")
        return 0
    files = changed_files(a.base, a.head)
    if files is None:
        print(f"Не удалось сравнить {a.base}...{a.head} — проверка пропущена.")
        return 0
    bad = violations(files, role)
    if bad:
        report(bad, role)
        return 1
    print(f"✔ Зоны в порядке (роль {role}, файлов: {len(files)}).")
    return 0


def cmd_pre_push(argv):
    if os.environ.get("HAN_ALLOW_ZONE") == "1" and os.environ.get("HAN_ALLOW_MAIN") == "1":
        return 0
    role = role_from_local()
    status = 0
    for line in sys.stdin.read().splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        local_ref, local_sha, remote_ref, remote_sha = parts
        is_main = remote_ref in ("refs/heads/main", "refs/heads/master")
        # удаление ветки
        if local_sha == ZERO:
            if is_main:
                print("✖ Нельзя удалять main.", file=sys.stderr)
                status = 1
            continue
        # push в main
        if is_main and remote_sha != ZERO and os.environ.get("HAN_ALLOW_MAIN") != "1":
            print("✖ Push прямо в main запрещён. Работай в своей ветке (a/…, b/…) и открой Pull Request.",
                  file=sys.stderr)
            status = 1
            continue
        # принудительный push (не fast-forward)
        if remote_sha != ZERO:
            rc, _ = git("merge-base", "--is-ancestor", remote_sha, local_sha)
            if rc != 0:
                print(f"✖ Принудительный push запрещён ({remote_ref}). Сделай `git pull --rebase origin main`.",
                      file=sys.stderr)
                status = 1
                continue
        # привязка роли к своим веткам: A -> a/…, B -> b/…; владелец — любые (кроме main)
        if role and "OWNER" not in held(role) and not is_main:
            m = re.match(r"^refs/heads/([^/]+)/", remote_ref)
            if not m or m.group(1) not in allowed_prefixes(role):
                mine = ", ".join(p + "/…" for p in sorted(allowed_prefixes(role)))
                print(f"✖ Роль {role} может отправлять на GitHub только свои ветки: {mine}\n  Сейчас: {remote_ref}. "
                      f"Создай ветку командой `git checkout -b {sorted(allowed_prefixes(role))[0]}/<этап>`.", file=sys.stderr)
                status = 1
                continue
        # зоны
        if os.environ.get("HAN_ALLOW_ZONE") == "1":
            continue
        branch_role = role_from_branch(remote_ref)
        r = branch_role if (role and branch_role in held(role)) else (role or branch_role)
        if not r:
            print("⚠ Роль не определена — проверка зон пропущена.", file=sys.stderr)
            continue
        if role and branch_role and branch_role not in held(role) and "OWNER" not in held(role):
            print(f"⚠ Твоя роль {role}, а ветка {remote_ref} похожа на ветку роли {branch_role}.", file=sys.stderr)
        rc, base = git("rev-parse", "--verify", "-q", "origin/main")
        if rc != 0:
            continue  # первого push ещё не было
        rc, mb = git("merge-base", base, local_sha)
        files = changed_files(mb if rc == 0 else base, local_sha) if remote_sha == ZERO else changed_files(remote_sha, local_sha)
        if files is None:
            continue
        bad = violations(files, r)
        if bad:
            report(bad, r)
            status = 1
    return status


def cmd_who(argv):
    for p in argv:
        print(f"{p}: {owner_of(p) or 'ничьё (можно всем)'}")
    return 0


if __name__ == "__main__":
    cmds = {"check": cmd_check, "pre-push": cmd_pre_push, "who": cmd_who}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        print(__doc__)
        sys.exit(2)
    sys.exit(cmds[sys.argv[1]](sys.argv[2:]))
