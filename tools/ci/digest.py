#!/usr/bin/env python3
"""Еженедельная сводка: прогресс по этапам, влитые и открытые PR, Issues. Создаёт Issue «Сводка недели» и шлёт в Telegram/Discord."""
import datetime
import glob
import os
import re

from common import ROOT, ensure_label, gh, gh_json
from notify import send

today = datetime.date.today()
week = (today - datetime.timedelta(days=7)).isoformat()
lines = [f"📊 HAN: сводка недели ({today:%d.%m.%Y})", ""]
for r in "AB":
    p = os.path.join(ROOT, "docs", "progress", f"{r}.md")
    if os.path.exists(p):
        t = open(p, encoding="utf-8").read()
        done, total = len(re.findall(r"- \[[xXхХ]\]", t)), len(re.findall(r"- \[[ xXхХ]\]", t))
        lines.append(f"{r}: этапов готово {done} из {total}" + (f" ({done * 100 // total}%)" if total else ""))
merged = gh_json("pr", "list", "--state", "merged", "--search", f"merged:>={week}", "--json", "number,title,author", "--limit", "50") or []
opened = gh_json("pr", "list", "--state", "open", "--json", "number,title,author,labels", "--limit", "50") or []
issues = gh_json("issue", "list", "--state", "open", "--json", "number,labels", "--limit", "200") or []
lines += ["", f"Влито за неделю: {len(merged)}"] + [f"  • #{p['number']} {p['title']} — {p['author']['login']}" for p in merged[:15]]
lines += ["", f"Ждут слияния/решения: {len(opened)}"] + [f"  • #{p['number']} {p['title']}" for p in opened[:10]]
by = {}
for i in issues:
    for l in i["labels"]:
        if l["name"].startswith("zone-") or l["name"] in ("blocked", "bug", "playtest", "contract"):
            by[l["name"]] = by.get(l["name"], 0) + 1
lines += ["", f"Открытых Issues: {len(issues)}" + (" (" + ", ".join(f"{k}: {v}" for k, v in sorted(by.items())) + ")" if by else "")]
text = "\n".join(lines)
print(text)
ensure_label("digest", "c5def5", "Сводка недели")
gh("issue", "create", "--title", f"Сводка недели {today:%d.%m.%Y}", "--body", text, "--label", "digest", check=False)
send(text)
