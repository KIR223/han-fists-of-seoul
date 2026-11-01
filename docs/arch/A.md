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

## Рендер кадра (E4)
```
проход 0 (глубина D32, массив из 3 слоёв): карты теней каскадов 15 / 50 / 150 м (всегда: при выключенных тенях — только очистка)
проход 1 (HDR RGBA16F + глубина D32): меши (PBR, GGX, солнце с тенью + окружение из неба) → небо на дальней плоскости (z = 1, тест «≤»)
проход 2 (PostProcess, `han/render/vulkan/post_process.*`): TAA → bloom → тонмаппинг AgX → изображение swapchain (sRGB)
   TAA: история в двух изображениях RGBA16F (пинг-понг), переотображение по матрице «клип → прошлый клип» и глубине, обрезка истории
        по окрестности 3×3 в YCoCg, смешивание в сжатом цвете; сцена и небо рисуются с джиттером Холтона из 8 фаз (`taa.h`)
   bloom: половинное разрешение, до 6 уровней, спуск 13 отсчётов с порогом и весами Каариса, подъём 3×3 «палаткой» аддитивно
   тонмаппинг: (сцена + bloom·интенсивность)·экспозиция → AgX с контрастом и насыщенностью, дизеринг
проход 3: наложение (ImGui)
```
- Данные кадра — один UBO на кадр в полёте (set 1): матрицы, камера, солнце, цвета неба, экспозиция. Структура `FrameUbo` в `renderer.cpp`
  зеркалит блок `Frame` в `shaders/common.glsl`; менять только вместе.
- Материал части меша (металличность, шероховатость) идёт push constants фрагментного шейдера; цвет — множитель из вершин и текстура (set 0).
- Тени (`shadow.h`): `computeShadowCascades` подгоняет каждый каскад под срез камеры (сфера охвата, радиус не зависит от поворота камеры),
  сдвиг привязан к сетке текселей в пространстве света (тени не «плывут»). Углы среза разворачиваются на NDC-глубине самого среза: углы
  дальней плоскости (2000 м при ближней 0,1) из обратной матрицы в float неточны на метры. В шейдере (`shadow.glsl`): выбор каскада по глубине
  вида, сдвиг по нормали, 3×3 отсчёта сравнивающего сэмплера (аппаратный PCF), плавное затухание на краю последнего каскада.
- Общие помощники Vulkan (создание образов, конвейеров, поиск памяти) — `vulkan/vk_util.*`; цепочка постобработки — отдельный класс `PostProcess`.
- Окно свёрнуто → Windows сообщает поверхность 0×0: swapchain не создаётся, кадры пропускаются, при развороте создаётся заново.
- Известное ограничение TAA: у движущихся объектов нет векторов движения, их «шлейф» ограничивает только обрезка истории. Векторы движения — отдельный шаг.
- Свет и небо: `makeSun`/`skyFromSun` (CPU). Это стартовая модель; таблица времени суток из части 34.1 переедет в `data/render/*.json`.
- Шейдеры собираются `shaders/compile_shaders.py` (glslc из Vulkan SDK, `#include "common.glsl"`), SPIR-V лежит в `han/render/generated/`.

## Как собрать и подключить свой модуль (для B)
**Сборка с нуля** (нужны Visual Studio с C++, CMake, Git LFS и vcpkg в `C:\vcpkg`; `JOIN_HAN.ps1` ставит всё сам):
```
cmake --preset windows-release            # Visual Studio 2026; с Visual Studio 2022 — пресет vs2022-release
cmake --build --preset windows-release
ctest --preset windows-release
```
Первая конфигурация долгая (vcpkg собирает SDL3, ImGui, fastgltf и др.), дальше берётся из кэша. Пресеты: `windows-*` (VS 2026) и `vs2022-*` (VS 2022).

**Новый модуль** в своей зоне (например, боевой модуль игры или анимация движка): корень CMake подхватывает его сам, если в папке модуля есть `CMakeLists.txt`.
Пример для модуля `combat` (так же устроены все модули зоны A, см. `han/core`):
```
game/combat/CMakeLists.txt        han_module(combat DEPENDS han_core)   # цель han_combat; исходники src/*.cpp, заголовки include/han/combat/
tests/combat/CMakeLists.txt       find_package(doctest CONFIG); add_executable(han_tests_combat …); add_test(NAME combat COMMAND han_tests_combat)
```
Зависимости: игра зависит от движка, движок — не от игры (`han_core`, `han_platform` и др. из зоны A). С зоной A общаетесь только через `contracts/`:
заголовки, заглушки в `contracts/stubs/`, список — `contracts/registry.md`. Нужна библиотека из vcpkg — Issue для A (корневой `vcpkg.json` ведёт A).

**Проверка кода:** CI (`build`) собирает и гоняет тесты на каждый PR; слияние в `main` — автоматически, если всё зелёное.
