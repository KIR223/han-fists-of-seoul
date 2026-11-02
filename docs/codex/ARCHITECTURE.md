# Архитектура — карта для Codex

Только ориентиры; детали — `docs/arch/A.md`, `docs/arch/B.md`, `docs/design/`.

| Область | Где | Зона |
|---|---|---|
| Ядро, платформа (окно, ввод), логирование | `han/core`, `han/platform` | A |
| Vulkan-рендер (PBR, каскадные тени, bloom, TAA, точечные огни), шейдеры, атлас вывесок | `han/render`, `shaders/` | A |
| Физика Jolt (в т.ч. рельеф heightfield), звук miniaudio | `han/physics`, `han/audio` | A |
| Мир: стриминг ячеек, город из OSM, рельеф, вода, запекание `.hcell` (`han_citycook`) | `han/world` | A |
| ИИ города: навигация, толпа, трафик, метро, районы, мировые сервисы | `han/ai` | A |
| Полиция, банды, транспорт | `game/police`, `game/gangs`, `game/vehicles` | A |
| UI-движок (RmlUi поверх ImGui), локализация | `han/ui` | A (экраны — B) |
| Конвейер ассетов (`han_fetch`, `han_cooker`, Blender-скрипты) | `han/tools/pipeline` | A |
| Анимация, ragdoll, ткань, Lua-привязки | `han/anim`, `han/ragdoll`, `han/cloth`, `han/script` | B |
| Бой (ядро без движка), стили, обучение, мини-игры, игрок | `game/combat`, `styles`, `learning`, `minigames`, `player` | B |
| Миссии, сюжет, экономика, телефон, стрим, сохранения | `game/missions`, `economy`, `phone`, `stream`, `save` | B |
| Контракты между зонами | `contracts/` (заголовки, схемы, `loc_requests/`) | общее, метка `contract` |
| Данные | `data/**` (мир/город/люди — A; бой/сюжет/UI/loc — B) | по зонам |
| Тесты | `tests/engine`, `tests/combat`, `tests/gameplay`, `han/tools/pipeline/tests` | по зонам |

Принципы: данные вместо чисел в коде; бой общается с миром только через `ICombatWorld`; мир отдаёт сервисы через `IWorldServices`; рендер относительно камеры (карта 16×13 км);
ячейки стримятся, тяжёлое запекается заранее. Демо: `han_city_demo` (город), `han_arena_demo` (бой), `han_ui_demo` (интерфейс).
