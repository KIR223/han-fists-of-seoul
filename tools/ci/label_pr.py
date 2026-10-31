#!/usr/bin/env python3
"""Ставит метки Pull Request по затронутым зонам: zone-a/b/c, contract, needs-owner.  Переменные: PR (номер), GH_TOKEN."""
import os
import sys

from common import ensure_label, gh, gh_json, zone_labels

pr = os.environ.get("PR") or (sys.argv[1] if len(sys.argv) > 1 else "")
if not pr:
    sys.exit("нужен номер PR")
info = gh_json("pr", "view", pr, "--json", "files,headRefName,labels") or {}
files = [f["path"] for f in info.get("files", [])]
branch = info.get("headRefName", "")
labels, owner_files = zone_labels(files)
if any(f.startswith("contracts/") for f in files):
    labels.add("contract")
if owner_files and not branch.startswith("o/"):
    labels.add("needs-owner")
for l in sorted(labels):
    ensure_label(l, {"contract": "fbca04", "needs-owner": "b60205"}.get(l, "0e8a16"))
if labels:
    gh("pr", "edit", pr, "--add-label", ",".join(sorted(labels)), check=False)
print("Метки:", ", ".join(sorted(labels)) or "нет")
