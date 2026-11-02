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
- doctest-фильтры с кириллицей нестабильны — используйте ASCII-префиксы: `han_tests_engine -tc="terrain*"`.
- Зависший тест — `taskkill /F /IM <имя>.exe`.
- Данные вне Git (нужны демо): `assets_src/osm/*.osm`, `assets_bin/terrain/seoul.hht` (`han/tools/pipeline/han_terrain_fetch.py`), `assets_bin/cooked/*`, запечённые ячейки `assets_bin/city/mapo` (`han_citycook`).
- CI (`.github/workflows/build.yml`): тесты конвейера, кэш vcpkg, `cmake --preset windows-release`, сборка, `ctest`. Локально зелёное = то же, что в CI.
