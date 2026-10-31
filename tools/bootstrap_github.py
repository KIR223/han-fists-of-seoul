#!/usr/bin/env python3
"""Первая настройка проекта на GitHub одной командой (запускает владелец, один раз).

  gh auth login                                   (если ещё не входил)
  python tools/bootstrap_github.py [--b <логин B>] [--a <логин A>] [--owner <свой логин>] [--repo han-fists-of-seoul] [--org <организация>]

Что делает по порядку:
  1. подставляет логины в .github/CODEOWNERS;
  2. создаёт личные файлы владельца (роль A+OWNER), если их ещё нет;
  3. git init, настройка хуков, первый коммит;
  4. создаёт ПРИВАТНЫЙ репозиторий на GitHub и отправляет main;
  5. создаёт ветки a/start и b/start (по одной на каждого разработчика);
  6. приглашает B как соавтора (право записи) — если логины указаны; друзей без GitHub можно добавить позже:
     python tools/add_member.py --role B --login <логин>;
  7. создаёт метки Issues;
  8. пытается включить защиту main (на бесплатном плане для приватных репозиториев может быть недоступна — это не ошибка).
  --dry-run  только показать команды.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRY = False


def run(*cmd, check=True, input_=None, quiet=False):
    if not quiet:
        print("  $ " + " ".join(cmd))
    if DRY:
        return subprocess.CompletedProcess(cmd, 0, "", "")
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, input=input_)
    if check and r.returncode != 0:
        print(r.stdout + r.stderr)
        print(f"✖ Команда не удалась: {' '.join(cmd)}")
        sys.exit(1)
    return r


def step(n, text):
    print(f"\n[{n}] {text}")


def main():
    global DRY
    ap = argparse.ArgumentParser()
    ap.add_argument("--b", help="логин GitHub разработчика B (можно позже: tools/add_member.py)")
    ap.add_argument("--a", help="логин GitHub разработчика A (по умолчанию — владелец)")
    ap.add_argument("--owner", help="логин владельца (по умолчанию — текущий вход gh)")
    ap.add_argument("--repo", default="han-fists-of-seoul")
    ap.add_argument("--org", help="создать репозиторий в организации, а не в личном аккаунте")
    ap.add_argument("--tg-token", help="токен Telegram-бота для уведомлений (необязательно)")
    ap.add_argument("--tg-chat", help="id чата Telegram для уведомлений (необязательно)")
    ap.add_argument("--discord", help="URL вебхука Discord для уведомлений (необязательно)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    DRY = a.dry_run

    for tool in ("git", "gh"):
        if not shutil.which(tool):
            print(f"✖ Не найден {tool}. См. docs/team/SETUP.md")
            return 1
    if not DRY and run("gh", "auth", "status", check=False, quiet=True).returncode != 0:
        print("✖ Ты не вошёл в GitHub. Выполни: gh auth login")
        return 1

    me = a.owner or (run("gh", "api", "user", "--jq", ".login", quiet=True).stdout.strip() if not DRY else "owner")
    la = (a.a or me).lstrip("@")
    lb = (a.b or "").lstrip("@")
    full = f"{a.org or me}/{a.repo}"
    print(f"Владелец и A: {la}   B: {lb or 'добавить позже'}   Репозиторий: {full} (приватный)")

    step(1, "Логины в CODEOWNERS")
    args = ["--owner", me, "--a", la] + (["--b", lb] if lb else [])
    run(sys.executable, "tools/set_owners.py", *args)

    step(2, "Список команды и скрипт подключения для друзей")
    if not DRY:
        with open(os.path.join(ROOT, "tools", "team.json"), "w", encoding="utf-8", newline="\n") as f:
            logins = {la: "A+OWNER" if la == me else "A", **({me: "OWNER"} if la != me else {})}
            if lb:
                logins[lb] = "B"
            json.dump({"repo": full, "logins": logins}, f, ensure_ascii=False, indent=2)
        tpl = open(os.path.join(ROOT, "tools", "join_windows.ps1"), encoding="utf-8").read().replace("__REPO__", full)
        for rel in (os.path.join("tools", "join_windows.ps1"), "JOIN_HAN.ps1"):
            with open(os.path.join(ROOT, rel), "w", encoding="utf-8-sig", newline="\r\n") as f:
                f.write(tpl)
    print("  tools/team.json и JOIN_HAN.ps1 созданы")

    step("2b", "Личные файлы владельца")
    if not os.path.exists(os.path.join(ROOT, "CLAUDE.local.md")):
        run(sys.executable, "tools/make_personal.py", "--role", "A+OWNER")
    else:
        print("  уже есть")

    step(3, "Git: репозиторий, хуки, коммит")
    if not os.path.isdir(os.path.join(ROOT, ".git")):
        run("git", "init", "-b", "main")
    if not DRY and not run("git", "config", "user.email", check=False, quiet=True).stdout.strip():
        uid = run("gh", "api", "user", "--jq", ".id", quiet=True).stdout.strip()
        run("git", "config", "user.email", f"{uid}+{me}@users.noreply.github.com")
        run("git", "config", "user.name", me)
    run(sys.executable, "tools/setup_local.py", check=False)
    run("git", "add", "-A")
    if DRY or run("git", "status", "--porcelain", quiet=True).stdout.strip():
        run("git", "commit", "-m", "Каркас проекта: правила, роли, дизайн, контракты")
    run("git", "branch", "-M", "main")

    step(4, "Приватный репозиторий на GitHub и отправка main")
    if DRY or run("git", "remote", "get-url", "origin", check=False, quiet=True).returncode != 0:
        run("gh", "repo", "create", full, "--private", "--source", ".", "--remote", "origin", "--push")
    else:
        print("  origin уже есть — отправляю main")
        run("git", "push", "-u", "origin", "main")

    step(5, "Ветки разработчиков")
    for r in ("a", "b"):
        run("git", "push", "origin", f"main:refs/heads/{r}/start", check=False)

    step(6, "Приглашения соавторам")
    if not lb:
        print("  друга пока нет — добавишь позже: python tools/add_member.py --role B --login <логин>")
    for login in sorted({lb, la} - {me, ""}):
        r = run("gh", "api", "--method", "PUT", f"repos/{full}/collaborators/{login}", "-f", "permission=push", check=False)
        print("  ✔ приглашение отправлено: " + login if r.returncode == 0 else f"  ✖ не удалось пригласить {login}: {r.stderr.strip()}")

    step("6b", "Настройки репозитория для автоматики")
    run("gh", "repo", "edit", full, "--delete-branch-on-merge", "--enable-squash-merge", "--enable-merge-commit=false", "--enable-rebase-merge=false", check=False)
    run("gh", "api", "--method", "PUT", f"repos/{full}/actions/permissions/workflow", "-f", "default_workflow_permissions=write",
        "-F", "can_approve_pull_request_reviews=true", check=False)
    for name, val in (("TELEGRAM_BOT_TOKEN", a.tg_token), ("TELEGRAM_CHAT_ID", a.tg_chat), ("DISCORD_WEBHOOK_URL", a.discord)):
        if val:
            run("gh", "secret", "set", name, "--repo", full, "--body", val, check=False)

    step(7, "Метки Issues")
    run(sys.executable, "tools/setup_github.py", check=False)

    step(8, "Защита main (может быть недоступна на бесплатном плане)")
    prot = {"required_status_checks": None, "enforce_admins": False,
            "required_pull_request_reviews": {"required_approving_review_count": 1, "require_code_owner_reviews": True},
            "restrictions": None, "allow_force_pushes": False, "allow_deletions": False}
    r = run("gh", "api", "--method", "PUT", f"repos/{full}/branches/main/protection", "--input", "-", check=False, input_=json.dumps(prot))
    print("  ✔ защита main включена" if r.returncode == 0 else
          "  • GitHub не включил защиту (для приватных репозиториев нужен платный план) — защищают хук Git и правила Claude")

    print(f"""
ГОТОВО.
  Репозиторий: https://github.com/{full}
  Ветки: a/start (твоя), b/start (B). Каждый пушит только в ветки со своей буквой: a/… (и o/… владелец), b/…
  Друг ещё не добавлен? Когда заведёт GitHub:  python tools/add_member.py --role B --login <логин>
  ДРУГУ (B) ОТПРАВЬ ОДИН ФАЙЛ:  {ROOT}/JOIN_HAN.ps1
  Он запускает его (ПКМ → «Выполнить с помощью PowerShell»): скрипт сам поставит программы, откроет вход в GitHub,
  примет приглашение, склонирует репозиторий, определит роль по логину, переключит на ветку b/start и запустит Claude Code.
  Ему остаётся написать Claude «Начинай». Архив B больше не обязателен (запасной путь).
  Автоматика на GitHub: проверки PR, метки зон, автослияние, Issue при смене контрактов, сводка по понедельникам, недельная сборка.
  Уведомления в Telegram/Discord (по желанию): python tools/bootstrap_github.py … --tg-token <токен> --tg-chat <id>  или  gh secret set TELEGRAM_BOT_TOKEN
""")
    return 0


if __name__ == "__main__":
    sys.exit(main())
