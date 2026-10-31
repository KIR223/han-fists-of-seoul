#!/usr/bin/env python3
"""Автослияние: вливает открытые PR, у которых все проверки зелёные, если они не требуют решения владельца.
Не вливает PR с метками contract, needs-owner, hold или в статусе Draft — их одобряет владелец (метка automerge разрешает слияние принудительно).
Переменные: GH_TOKEN, GITHUB_REPOSITORY, SHA (необязательно — проверять PR только этого коммита).
"""
import os
import subprocess
import sys

from common import ROOT, gh, gh_json

repo = os.environ["GITHUB_REPOSITORY"]
sha = os.environ.get("SHA")
prs = gh_json("pr", "list", "--state", "open", "--base", "main", "--json", "number,title,headRefName,headRefOid,labels,isDraft,mergeable") or []
for pr in prs:
    n, title, labels = pr["number"], pr["title"], {l["name"] for l in pr["labels"]}
    if sha and pr["headRefOid"] != sha:
        continue
    if pr["isDraft"] or (labels & {"contract", "needs-owner", "hold"} and "automerge" not in labels):
        print(f"#{n}: ждёт владельца/решения ({', '.join(sorted(labels)) or 'draft'})")
        continue
    if pr["mergeable"] == "CONFLICTING":
        print(f"#{n}: конфликт — автор должен подтянуть main")
        continue
    runs = (gh_json("api", f"repos/{repo}/commits/{pr['headRefOid']}/check-runs?per_page=100") or {}).get("check_runs", [])
    runs = [r for r in runs if r["name"] not in ("automerge", "merge")]
    if not runs:
        print(f"#{n}: проверок ещё нет")
        continue
    if any(r["status"] != "completed" for r in runs):
        print(f"#{n}: проверки ещё идут")
        continue
    if any(r["conclusion"] not in ("success", "skipped", "neutral") for r in runs):
        print(f"#{n}: есть упавшие проверки")
        continue
    from commit_msg_check_proxy import is_russian  # noqa
    if not is_russian(title):
        print(f"#{n}: заголовок PR не на русском — не вливаю")
        continue
    r = gh("pr", "merge", str(n), "--squash", "--delete-branch", "--subject", f"{title} (#{n})", "--body", "", check=False)
    if r.returncode:
        print(f"#{n}: слить не удалось: {r.stderr.strip()}")
        continue
    print(f"✔ #{n} влит в main")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "ci", "contract_issue.py"), str(n)], cwd=ROOT)
