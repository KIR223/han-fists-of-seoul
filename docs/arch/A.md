# Архитектура — зона A (движок, мир, город)

## Слои и зависимости
```
core <- platform <- render | physics | audio | ui
physics <- world <- ai <- game/police, game/gangs
physics + world <- game/vehicles
render + ui + world <- editor          core <- tools
```
- Движок (`han/*`) не зависит от игры (`game/*`); игра зависит от движка.
- Между зонами A и B общаемся только через `contracts/` (см. `docs/team/CONTRACTS.md`).
- Каждый модуль — статическая библиотека `han_<имя>` (`cmake/HanModule.cmake`); корневой `CMakeLists.txt` сам подхватывает
  `han/*`, `game/*`, `tests/*`, если в папке есть `CMakeLists.txt`.

## Сборка
- Пресеты: `windows-debug`, `windows-release`, `windows-relwithdebinfo` (Visual Studio 2026, x64, триплет `x64-windows-static-md`).
- `vcpkg.json`: функция `tests` (doctest, включена по умолчанию, быстрый CI) и функция `engine` со ВСЕМИ библиотеками движка;
  версии зафиксированы `builtin-baseline`. Полный набор: `-DVCPKG_MANIFEST_FEATURES="tests;engine"`.
- CI: `.github/workflows/build.yml` (workflow `build`), Windows, кэш бинарников vcpkg.

## Визуальный стиль
Реализм (решение владельца, 2026-10-31). Подробности — художественная библия (части по графике).

## Бюджеты
Кадр 16,6 мс на RTX 2050, память — по части «Память». Бюджеты по подсистемам добавляются вместе с модулями (E2+).

## Конвейер ассетов (E20, часть 76 дизайна)
```
assets_src/manifest.json (в Git) ──► han_fetch.py fetch ──► assets_bin/downloads/<id> (хэш дерева в манифесте)
                                  └► han_fetch.py process ──► Blender (han/tools/blender/han_pipeline.py)
                                          нормализация → бюджет треугольников → UV → LOD1–3 → коллизия ≤ 32 вершин → поиск брендов
                                      ──► assets_bin/processed/<id>/*.glb ──► han_cooker ──► assets_bin/cooked/*.hmesh + tex_*.ktx2
                                      ──► docs/credits/<зона>.md (строка добавляется сама)
```
- Одна команда: `python han/tools/pipeline/han_fetch.py add --id … --source polyhaven|sketchfab|direct|local … --process`.
- Источники: Poly Haven (CC0, без токена), Sketchfab (токен в `SKETCHFAB_TOKEN`), прямая ссылка, локальный файл («положил в папку»).
- Защита: только HTTPS, лимит размера, безопасная распаковка zip, лицензия проверяется до сети (`license_rules.py`, общие тест-векторы с C++),
  при смене содержимого у источника хэш не совпадёт, а смена лицензии на запрещённую остановит загрузку.
- Остановки конвейера: бюджет треугольников превышен; найдены имена реальных брендов (`real_brands.txt`) — пока не вписан `brand_review`.
- Из манифеста всё воспроизводится с нуля: `han_fetch.py all`. Текстуры при варке называются по содержимому и делятся между LOD-ами.
- Тесты: `python -m unittest discover -s han/tools/pipeline/tests` (в CI; с установленным Blender добавляется настоящий прогон).
