#!/usr/bin/env python3
"""После слияния PR, менявшего contracts/, создаёт Issue «Контракт изменился» для остальных зон.  Аргумент: номер PR."""
import sys

from common import ensure_label, gh, gh_json, team

n = sys.argv[1] if len(sys.argv) > 1 else ""
if not n:
    sys.exit("нужен номер PR")
info = gh_json("pr", "view", n, "--json", "files,title,author,url,mergedAt") or {}
files = [f["path"] for f in info.get("files", []) if f["path"].startswith("contracts/") and not f["path"].startswith("contracts/loc_requests/")]
if not files or not info.get("mergedAt"):
    print("Контракты не менялись (или PR не влит).")
    sys.exit(0)
title = f"Контракт изменился: {', '.join(f.replace('contracts/', '') for f in files[:3])}{'…' if len(files) > 3 else ''} (PR #{n})"
if gh("issue", "list", "--state", "all", "--search", f"PR #{n} in:title", "--json", "number", "--jq", "length", check=False).stdout.strip() not in ("", "0"):
    print("Issue уже есть.")
    sys.exit(0)
author = (info.get("author") or {}).get("login", "")
who = sorted({l for l in team().get("logins", {}) if l.lower() != author.lower()})
body = (f"В main влит PR #{n} «{info.get('title')}» ({info.get('url')}), который меняет общие интерфейсы.\n\n"
        "**Изменённые файлы:**\n" + "\n".join(f"- `{f}`" for f in files) + "\n\n"
        "**Что нужно сделать:** прочитай изменения и `contracts/registry.md`; если твой код использует этот контракт — адаптируй его. "
        "Старая версия живёт, пока все не перешли. Когда готово — закрой Issue комментарием.\n\n"
        + ("Кого касается: " + " ".join("@" + w for w in who) if who else ""))
ensure_label("contract", "fbca04", "Изменение общего интерфейса")
args = ["issue", "create", "--title", title, "--body", body, "--label", "contract"]
if who:
    args += ["--assignee", ",".join(who)]
r = gh(*args, check=False)
if r.returncode and who:  # исполнитель мог быть недоступен
    r = gh("issue", "create", "--title", title, "--body", body, "--label", "contract", check=False)
print(r.stdout.strip() or r.stderr.strip())
