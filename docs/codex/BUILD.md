# Сборка и проверки (для Codex)

Только команды, существующие в репозитории. Статус «проверено» — см. отчёт сессии; здесь указано, что подтверждено на машине владельца.

## Требования
Windows 11, Visual Studio с «Разработка классических приложений на C++», CMake ≥ 3.25, Python 3, Git (+LFS), vcpkg (`VCPKG_ROOT`, у владельца `C:\vcpkg`), Vulkan SDK (для шейдеров — `glslc`).
Проверка среды: `python tools/doctor.py` (`--fix` чинит что может).

## Пресеты (`CMakePresets.json`)
| Пресеты | Генератор | Статус |
|---|---|---|
| `windows-debug`, `windows-release`, `windows-relwithdebinfo` | Visual Studio 18 2026 | основные; установлены на машине владельца, используются в CI |
| `vs2022-debug`, `vs2022-release`, `vs2022-relwithdebinfo` | Visual Studio 17 2022 | сохранены для тех, у кого VS 2022; на машине владельца **не проверялись** (VS 2022 не установлена) |

Рабочие пресеты не удалять и не переименовывать. Выбирайте пресет по установленной Visual Studio.

## Команды
```
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release --no-tests=error      # 3 набора: engine, combat, gameplay
powershell -File tools/build.ps1                     # то же одним скриптом
```
Бинарники: `build/windows-release/bin/Release/` (`han_city_demo`, `han_arena_demo`, `han_ui_demo`, `han_cooker`, `han_citycook`, тесты `han_tests_*`).

## Остальные проверки
```
cd han/tools/pipeline/tests && PYTHONUTF8=1 python -m unittest discover -s .   # тесты конвейера ассетов
python han/tools/pipeline/han_fetch.py verify                                   # манифест ассетов
python data/_tools/validate_data.py                                             # схемы данных
python tools/han_zones.py check --base origin/main --head HEAD --branch <ветка> # зоны
python tools/ci/credits_check.py --base origin/main --head HEAD                 # лицензии новых ассетов
python shaders/compile_shaders.py                                               # после правки GLSL; коммитить только изменённые generated-файлы
```

## Подсказки
Город после изменения `data/city/streetscape.json` нужно перепечь. Запускать из корня репозитория:
```powershell
build/windows-release/bin/Release/han_citycook.exe --osm assets_src/osm/mapo_big.osm --out assets_bin/city/mapo --terrain assets_bin/terrain/seoul.hht
build/windows-release/bin/Release/han_city_demo.exe --cooked assets_bin/city/mapo --walk --hour 10
build/windows-release/bin/Release/han_city_demo.exe --cooked assets_bin/city/mapo --walk --hour 21
```
Без OSM запечённая геометрия отображается, но трафик и прохожие не создаются. Солнце, экспозиция и окна автоматически меняются по `data/city/day_cycle.json`; `--sun` задаёт ручной свет.

Материалы из локальных ZIP требуют исходную папку `C:\Users\insf9\Downloads\assets` (пути — `assets_src/manifest.json`). `python han/tools/pipeline/han_fetch.py process <id>` читает роли `texture_maps`; для EXR нужен Blender. Три модели `pr_street_bench`, `pr_street_planter`, `pr_street_bin` скачиваются из Poly Haven через тот же конвейер. После обновления cooker с поддержкой PBR модели нужно сварить заново. Формат hmesh v4 читает карты нормалей/шероховатости, старые v3 остаются совместимыми.

Деревья: `python han/tools/pipeline/han_fetch.py fetch pr_city_tree`, затем `python han/tools/pipeline/han_fetch.py process pr_city_tree` (нужны Blender и актуальный cooker). Форма модели воспроизводится из `data/city/tree_profile.json`; радиусы/LOD — `data/city/vegetation.json`. После изменения высот/коллизий в streetscape перепечь город командой выше. Актуальная запись — hmesh v5 и HCL3; чтение hmesh v3/v4 и HCL1/HCL2 сохранено. Новые графические сцены запускаются `han_scene_demo --alpha-test`, регрессии — обычным `tests/engine/screenshots/run.py`.

- doctest-фильтры с кириллицей нестабильны — используйте ASCII-префиксы: `han_tests_engine -tc="terrain*"`.
- Зависший тест — `taskkill /F /IM <имя>.exe`.
- Данные вне Git (нужны демо): `assets_src/osm/*.osm`, `assets_bin/terrain/seoul.hht` (`han/tools/pipeline/han_terrain_fetch.py`), `assets_bin/cooked/*`, запечённые ячейки `assets_bin/city/mapo` (`han_citycook`).
- CI (`.github/workflows/build.yml`): тесты конвейера, кэш vcpkg, `cmake --preset windows-release`, сборка, `ctest`. Локально зелёное = то же, что в CI.
