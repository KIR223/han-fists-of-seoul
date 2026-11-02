# AGENTS.md — инструкции для OpenAI Codex (и любых ИИ-агентов)

Этот файл — точка входа для Codex. Он **не заменяет** `CLAUDE.md` и `.claude/`, а ссылается на них. При расхождении главнее `CLAUDE.md` (правила команды) и `docs/design/` (дизайн).
Язык общения и документации — **русский**, коротко. Идентификаторы в коде — английские.

## 1. Проект
**HAN: Fists of Seoul** — 3D-игра от третьего лица, открытый мир Сеула и окрестностей, главная фишка — рукопашный бой (8 стилей), строго 18+.
Свой движок **HAN Engine**: C++20, Vulkan 1.3, Jolt Physics, Lua, miniaudio, RmlUi/ImGui, vcpkg. Цель: RTX 2050, 14–16 ГБ ОЗУ, кадр 16,6 мс.
Только бесплатные ассеты с чистой лицензией и вымышленные бренды. Детей и подростков в игровом контенте нет.

## 2. Кто такой Codex в этом репозитории
Codex — **единый полноценный агент** с доступом ко всему репозиторию. Он выполняет **обе роли**: A (движок, мир, город) и B (бой, сюжет, игра).
Роли определяют *владение файлами и порядок проверки*, а не людей. Подробности ролей: `docs/team/ROLE_A.md`, `docs/team/ROLE_B.md`; карта зон — `tools/zones.json`
(кто владеет путём: `python tools/han_zones.py who <путь>`). Владелец проекта — человек (GitHub `KIR223`), он решает, когда заливать в `main`.

## 3. Архитектура (кратко; подробно — `docs/codex/ARCHITECTURE.md`, `docs/arch/A.md`, `docs/arch/B.md`)
- `han/` — движок: `core platform render physics audio world ui editor tools ai` (зона A), `anim ragdoll cloth script` (зона B).
- `game/` — игра: `police gangs vehicles` (A); `combat styles learning minigames player missions stream economy phone customization activities save` (B).
- `contracts/` — **единственное место общих интерфейсов** между зонами (заголовки, схемы, `loc_requests/`). Менять только отдельным малым PR с меткой `contract`.
- `data/` — данные (числа и тексты живут здесь, не в коде): `world city people terrain materials vehicles` (A); `combat ai story economy ui loc audio progress` (B).
- `shaders/` (GLSL → SPIR-V, `python shaders/compile_shaders.py`), `tests/` (doctest), `bench/`, `tools/` (скрипты команды, зона владельца), `cmake/`, `docs/`.
- Модули подхватываются CMake автоматически (`han/*/CMakeLists.txt`, `game/*/CMakeLists.txt`, `tests/*/CMakeLists.txt`) — корневые CMake-файлы не править без нужды.

## 4. Обязательные правила (не ослаблять)
**Git**
- Никаких `push` в `main`, `--force`, `reset --hard origin`, удаления веток/истории, `--no-verify`. В `main` — только через Pull Request.
- Сообщения коммитов и заголовки PR — **только по-русски** (хук `commit-msg`, CI). Пример: `Бой: кадровая машина ударов (G1)`.
- Ветки по префиксу: `a/…` (работа A), `b/…` (работа B), `o/…` (файлы владельца: `CLAUDE.md`, `docs/design`, `docs/team`, `tools`, `.githooks`, `.github`). CI-проверка `zones` смотрит на **префикс ветки**,
  поэтому один PR не должен смешивать зоны A, B и владельца: кросс-зонную работу делить на отдельные ветки/PR (contract-first: сначала `contracts/` + заглушка).
- Пушить, открывать PR и сливать — **только по явной команде владельца**. Не обходить хуки и CI; если проверка мешает — остановиться и объяснить.
- Конфликт в файле другой зоны — не решать «своей версией», сообщить владельцу.

**Данные, тексты, ассеты**
- Числа — в `data/**`; тексты — только ключи локализации (`data/loc/<язык>/<зона>.csv`; строки зоны A — в `contracts/loc_requests/A.csv`). Один общий csv запрещён.
- Новый ассет — только лицензии CC0/CC-BY/свободные (часть 76 дизайна), сначала запись в `docs/credits/<A|B>.md` (Ассет | Автор | Источник URL | Лицензия | Где используется | Изменён?). NC/ND, рипы, непонятная лицензия — нельзя.
- Большие исходники в Git не кладём (`assets_bin/`, `assets_src/osm/*.osm` в `.gitignore`). Данные OSM (ODbL) в Git не класть.
- Реальные бренды, логотипы, марки машин, чужая музыка запрещены — пародийная вселенная (часть 13 дизайна).
- В святых местах (флаг `sacred`) драка невозможна; храмы — без насилия и насмешек (часть 74).

**Код**
- Ядро боя не зависит от рендера и Jolt (только `ICombatWorld`). Бюджеты производительности и памяти — в соответствующих частях дизайна.
- Не писать свои физику/звук/UI-движок поверх готовых библиотек; без мультиплеера и универсального редактора уровней.
- Каждая новая функция — с тестом (doctest в `tests/<зона>/`, Python — `unittest`). Изменения минимальные и точечные.
- Определение готовности этапа — конец `docs/design/64_tehnicheskie_prilozheniya.md`.

## 5. Сборка и проверка (проверенные команды — `docs/codex/BUILD.md`)
```
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release --no-tests=error
python data/_tools/validate_data.py
python tools/han_zones.py check --base origin/main --head HEAD --branch <ветка>
python tools/ci/credits_check.py --base origin/main --head HEAD
```
Пресеты `windows-*` — Visual Studio 2026 (генератор «Visual Studio 18 2026»), `vs2022-*` — Visual Studio 2022. Нужен `VCPKG_ROOT`. Среда сломана → `python tools/doctor.py --fix`.
**Не заявлять «собирается/тесты проходят», если команды не запускались** — в отчёте указывать, что реально выполнено и с каким результатом.

## 6. Что запрещено без прямого поручения владельца
Переписывать игру или движок, менять игровую логику и баланс, добавлять механики, «улучшать» графику, массовый рефакторинг и переименования, трогать `contracts/` без метки `contract`,
править файлы владельца вне ветки `o/…`, создавать фиктивные/заглушечные файлы ради вида, удалять или ослаблять `CLAUDE.md`, `.claude/`, `.githooks`, `tools/zones.json`, CI.

## 7. Старт сессии и отчётность
1. Прочитать `CLAUDE.local.md` (роль; режим `SOLO` = владелец держит зоны A, B и владельца), `docs/progress/` (A, B, SOLO) и `docs/team/STAGES_A.md` / `STAGES_B.md`.
2. Читать только части дизайна, нужные этапу: `docs/design/INDEX.md`, `docs/team/READING_A.md`, `READING_B.md`.
3. Работать в своей ветке (не в `main`), маленькими коммитами по-русски; процесс — `docs/codex/WORKFLOW.md`.
4. В конце сессии обновить `docs/progress/<роль>.md` (что сделано, что дальше, блокеры) и отчитаться: что изменено, что запускалось и с каким результатом, что не проверено.

## 8. Карта документов
`CLAUDE.md` (правила команды) · `docs/design/INDEX.md` (дизайн) · `docs/team/` (роли, процесс, автоматика, контракты) · `docs/arch/` · `docs/progress/` · `docs/credits/` ·
`contracts/` · `docs/codex/WORKFLOW.md`, `BUILD.md`, `ARCHITECTURE.md` (памятки для Codex).
