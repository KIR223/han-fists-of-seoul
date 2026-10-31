#!/usr/bin/env python3
"""Хук PreToolUse: не даёт Claude править чужие зоны (в т.ч. через shell), пушить в чужие ветки и коммитить не по-русски.
Дополняет Edit-запреты из .claude/settings.local.json: те не ловят правки через sed, tee, перенаправление и т. п.
Отключить на один запуск (только владелец): HAN_ALLOW_ZONE=1
"""
import os
import re
import shlex
import sys

from _common import ROOT, deny, effective_role, hz, read_input, rel

if os.environ.get("HAN_ALLOW_ZONE") == "1":
    sys.exit(0)
data = read_input()
tool = data.get("tool_name", "")
inp = data.get("tool_input", {}) or {}
eff, role = effective_role()
if not role:
    sys.exit(0)


def check_paths(paths, why):
    bad = []
    for p in paths:
        r = rel(p)
        if r is None:
            continue
        bad += hz.violations([r], eff)
    if bad:
        lst = "\n".join(f"  {f} (владелец: {o})" for f, o in bad[:10])
        deny(f"✖ {why}: роль {eff} не может менять чужие зоны:\n{lst}\n"
             f"  Создай Issue для владельца зоны (docs/team/WORKFLOW.md). Общие интерфейсы — только в contracts/.")


if tool in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
    fp = inp.get("file_path") or inp.get("notebook_path")
    if fp:
        check_paths([fp], "Правка файла")
    sys.exit(0)

if tool == "Bash":
    cmd = inp.get("command", "")
    # 1) git push: только свои ветки, не main, без force
    for m in re.finditer(r"git\s+push\b([^;&|\n]*)", cmd):
        args = m.group(1)
        if re.search(r"(--force\b|--force-with-lease|\s-f\b|\s\+\S)", args):
            deny("✖ Принудительный push запрещён.")
        refs = re.findall(r"(?:HEAD:)?(?:refs/heads/)?([abco]/[\w./-]+|main|master)\b", args)
        for ref in refs:
            if ref in ("main", "master"):
                deny("✖ Push в main запрещён: работай в своей ветке и открой Pull Request (python tools/ship.py).")
            if "OWNER" not in hz.held(role) and ref[0] not in hz.allowed_prefixes(role):
                deny(f"✖ Роль {role} может отправлять только ветки {', '.join(sorted(hz.allowed_prefixes(role)))}/…, а не {ref}.")
    # 2) коммит с английским сообщением
    for m in re.finditer(r"git\s+commit\b[^\n]*?(?:-m|--message)[ =]\s*(\"([^\"]*)\"|'([^']*)')", cmd):
        from commit_msg_check import is_russian  # noqa: E402
        msg = (m.group(2) or m.group(3) or "").splitlines()[0] if (m.group(2) or m.group(3)) else ""
        if msg and not is_russian(msg):
            deny(f"✖ Сообщение коммита должно быть на русском. Получено: «{msg}»")
    # 3) запись в файлы чужих зон через shell
    writes = bool(re.search(r"(>>?\s*\S|\btee\b|\bsed\s+-i|\bmv\b|\bcp\b|\brm\b|\btouch\b|\bmkdir\b|\bdel\b|\bcopy\b|\bmove\b|"
                            r"Set-Content|Add-Content|Out-File|Remove-Item|New-Item|\bgit\s+(checkout|restore|apply|mv|rm)\b)", cmd))
    if writes:
        try:
            toks = shlex.split(cmd, posix=True)
        except ValueError:
            toks = cmd.split()
        cands = [t.lstrip(">") for t in toks if "/" in t or "\\" in t or "." in t]
        cands = [c for c in cands if c and not c.startswith("-")]
        check_paths(cands, "Команда меняет файлы")
sys.exit(0)
