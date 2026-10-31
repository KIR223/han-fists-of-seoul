# Чек-лист владельца (один раз, ~30 минут)

**Быстрый путь (рекомендуется):** `gh auth login`, затем
```text
python tools/bootstrap_github.py --b <логин B>
```
Друга без GitHub можно не указывать (запусти без `--b`) и добавить позже: `python tools/add_member.py --role B --login <логин>`.
Скрипт сделает пункты 2–6 ниже сам (приватный репозиторий, логины, первый push, ветки `a/start` и `b/start`, приглашение, метки,
попытку включить защиту `main`). Ниже — ручной путь на случай, если что-то пошло не так.

- [ ] 1. Поставь программы из `SETUP.md` (минимум: Git for Windows, Python, `gh`).
- [ ] 2. Создай **приватный** репозиторий на GitHub (например `han-fists-of-seoul`), пригласи B с правом записи (Write).
- [ ] 3. Узнай логин GitHub B и выполни в папке проекта:
  ```text
  python tools/set_owners.py --owner <твойЛогин> --a <твойЛогин> --b <логинB>
  ```
- [ ] 4. Личные файлы владельца: `python tools/make_personal.py --role OWNER` (если ты ещё и разработчик A — `--role A+OWNER`).
- [ ] 5. Первая отправка (один раз, прямо в `main` — хук разрешает только создание пустой `main` на сервере):
  ```text
  git init -b main
  python tools/setup_local.py
  git add -A
  git commit -m "Каркас проекта: правила, роли, дизайн, контракты"
  git remote add origin <ссылка>
  git push -u origin main
  ```
  `CLAUDE.local.md` и `.claude/settings.local.json` в Git не попадают (они в `.gitignore`).
- [ ] 6. Метки: `gh auth login` → `python tools/setup_github.py`.
- [ ] 7. Защита `main` в GitHub: Settings → Branches (или Rules → Rulesets): «Require a pull request before merging», «Require review from Code Owners», «Block force pushes».
  На бесплатном плане для приватных репозиториев эти настройки могут быть недоступны — проверь в своём аккаунте. Тогда защиту держат хук `.githooks/pre-push`,
  Edit-запреты в `.claude/settings.local.json` и проверка `repo-check` (она сработает на PR и без защиты ветки).
- [ ] 8. Отправь B файл `JOIN_HAN.ps1` (создаётся в корне проекта). Запасной путь — архив `HAN_B_igra.zip` и его `START_HERE_B.txt`.
- [ ] 9. Договорись о чате (Discord/Telegram) и дне недельной сборки (`WORKFLOW.md`).
- [ ] 10. Когда A сообщит «E0 готов» — посмотри PR с каркасом и контрактами, проверь, что сборка одной командой проходит на чистой машине.

## Уведомления в Telegram/Discord (по желанию)
Создай бота у @BotFather, узнай id чата и выполни: `gh secret set TELEGRAM_BOT_TOKEN`, `gh secret set TELEGRAM_CHAT_ID` (или `DISCORD_WEBHOOK_URL`).
Тогда сводка по понедельникам и сообщения о упавших проверках придут в чат. Без секретов всё работает, но сообщения будут только в логах и Issues.

## Что твой Claude делает по умолчанию
Роль OWNER правит только общие файлы (`docs/design`, `docs/team`, `tools`, `CLAUDE.md`, `.github/CODEOWNERS`); зону B — через её Issues. Аварийно: `HAN_ALLOW_ZONE=1`.

## Юридическая линия (твоя ответственность до релиза)
- Каждый ассет — запись в `docs/credits/<роль>.md`; перед E15 запусти `python tools/merge_credits.py` — неполные строки будут перечислены.
- Реальные бренды и имена — нет; музыка и озвучка — по правилам частей 2, 42, 52 дизайна; `AI_USAGE.md` и `VOICE_RIGHTS.md` ведёт B.
- Ассеты Quixel Megascans и MetaHuman — только для Unreal Engine; в этом проекте их нет.
