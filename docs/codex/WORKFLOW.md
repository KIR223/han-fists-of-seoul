# Рабочий процесс Codex

Codex заменяет обе роли (A и B), но проверки репозитория (зоны, контракты, русские коммиты, лицензии) остаются в силе. Общие правила — `AGENTS.md`, `CLAUDE.md`, `docs/team/WORKFLOW.md`.

## Ветки
- Работа движка/мира/города — `a/<этап>-<суть>`; бой/сюжет/игра — `b/<этап>-<суть>`; файлы владельца (`CLAUDE.md`, `docs/design`, `docs/team`, `tools`, `.githooks`, `.github/CODEOWNERS`) — `o/<суть>`.
- `AGENTS.md` и `docs/codex/*` — «ничьи», их можно менять в любой ветке.
- CI определяет роль по префиксу ветки. PR, затрагивающий и зону A, и зону B (или владельца), провалит проверку `zones` → делить на несколько веток/PR.
- Нужна функция из соседней зоны: сначала интерфейс + заглушка в `contracts/` (отдельный PR, метка `contract`), потом реализация по обе стороны.
- Две параллельные сессии — только через `git worktree`.

## Цикл задачи
1. `git fetch && git checkout -b <префикс>/<этап> origin/main` (не работать в `main`).
2. Прочитать этап: `docs/team/STAGES_A.md` / `STAGES_B.md`, прогресс `docs/progress/`, нужные части дизайна.
3. Маленькие изменения, тесты рядом (`tests/engine`, `tests/combat`, `tests/gameplay`), числа — в `data/`, тексты — ключи локализации.
4. Сборка и тесты (`docs/codex/BUILD.md`), `han_zones.py check`, `credits_check.py`, `validate_data.py`.
5. Коммит по-русски; `python tools/ship.py -m "…"` делает коммит, проверки, push в свою ветку и PR — **только когда владелец сказал заливать**.
6. Слияние: `automerge` сливает при зелёных проверках, кроме меток `contract`, `needs-owner`, `hold` (одобряет владелец).
7. Обновить `docs/progress/<роль|SOLO>.md`.

## Чего не делать
`push` в `main`, `--force`, удаление веток, `--no-verify`, обход хуков, правка чужих зон «заодно», массовый рефакторинг, большие бинарники в Git, ассеты без записи в `docs/credits/`.

## Хуки Claude и Codex
`.claude/settings.json` (SessionStart/PreToolUse/Stop, запреты push в `main` и `--force`) работает только в Claude Code. Для Codex те же ограничения обеспечивают git-хуки `.githooks` (`core.hooksPath`; настроить: `python tools/setup_local.py`) и CI — поэтому они обязательны.
