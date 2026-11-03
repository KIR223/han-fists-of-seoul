# Манекен для HAN: установка и следующий этап A1

## Установлено

Quaternius Universal Animation Library **Standard**, бесплатный пакет CC0:
https://quaternius.com/packs/universalanimationlibrary.html
https://quaternius.itch.io/universal-animation-library

Включённый Mannequin — взрослый мужской силуэт без лица. Исходник около 183 см;
подготовленный вариант 180 см, светло-серый PBR (metallic 0, roughness 0.75).
65 костей, включая пальцы обеих рук; 43 исходных клипа. Скелет и mesh нормализуются
одним общим родителем: skin, inverse bind matrices и бинарные ключи не изменяются.
Взята версия **без RM**. Физическое перемещение остаётся у контроллера Jolt;
при подключении проверить дрейф pelvis/root каждого клипа, не двигать капсулу второй раз.

## Воспроизвести на своём ПК

Из корня репозитория (Python 3, интернет):

```sh
python han/tools/pipeline/han_fetch.py fetch quaternius_ual
python han/tools/pipeline/han_fetch.py process quaternius_ual
python han/tools/pipeline/han_fetch.py verify
blender --background --python han/tools/blender/check_mannequin.py -- assets_bin/cooked/quaternius_ual/CH_Mannequin.glb
```

Получится `assets_bin/cooked/quaternius_ual/CH_Mannequin.glb`, рядом лицензия и
`CH_Mannequin.report.json`. Оригинал — в `assets_bin/downloads/quaternius_ual/`.
Сами бинарники исключены из Git; источник, ID бесплатной загрузки и хэш записаны
в `assets_src/manifest.json`, автор — в `docs/credits/A.md`.
При смене файла автором конвейер остановится на несовпадении хэша: сначала сверить
лицензию/состав и лишь потом явно применять `--update-hash`.

Открыть GLB через Blender → File → Import → glTF 2.0. В Action Editor доступны
`Idle_Loop`, `Walk_Loop`, `Jog_Fwd_Loop`, `Sprint_Loop`,
`Jump_Start`, `Jump_Loop`, `Jump_Land`, `Punch_Jab`, `Punch_Cross`, реакции на удары.
Для предпросмотра одной Action отключить NLA-треки других клипов.
Blender добавляет к именам Action суффикс `_Armature`; в исходном GLB имена без него.

**A1, первый рабочий проход:** арена принимает `Boxing.hchar`, считает позы через
ozz и рисует skinned mesh с тенями. Статический `.hmesh` для персонажа не подходит.

```sh
python han/anim/tools/build_boxing.py
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release --no-tests=error
build/windows-release/bin/Release/han_arena_demo.exe --foes 1
```

Если ozz не установлен, CMake скачивает закреплённый ozz 0.16 (MIT) с проверкой SHA256.
Для офлайн-сборки установить его отдельно через CMAKE_PREFIX_PATH или сохранить
FetchContent-кэш; `-DHAN_FETCH_OZZ=OFF` отключает скачивание и, при отсутствии библиотеки,
анимацию. `--primitives` в арене позволяет сравнить со старым отображением.
Если HCH1 отсутствует, арена явно сообщает о возврате к примитивам.

Vulkan-проверка, требующая настоящий запуск (никакого пропуска при отсутствии GPU):

```sh
python tests/animation/check_arena.py --exe build/windows-release/bin/Release/han_arena_demo.exe --require-validation
```

Текущий набор: **104 клипа** на одном скелете (43 исходных UAL1, 2 исходных
UAL2 и 59 авторских клипов). Есть отдельные движения всех 19 ударных техник,
клинч, восьминаправленный футворк, защита, реакции, четыре падения и четыре
подъёма. Подъём основан на `LayToIdle` из UAL2, не на обратном падении.
Общие idle/walk/jog/sprint/jump подключены в `han_player_demo`.

Подробности и управление — [BOXING.md](../../anim/BOXING.md). Полный DoD A1/G2
ещё включает редактор анимаций и игровые экраны тренировок; активный ragdoll
относится к P2. Следующие стили пока не добавляются.

## Формат и библиотеки

Выбран GLB (glTF 2.0): открытая спецификация, JSON + бинарный буфер в одном файле,
явные units/TRS, skin и animation. В HAN уже есть **fastgltf**; расширять его импорт,
не вводить второй парсер cgltf/tinygltf/Assimp. FBX использовать как исходник для
Blender → GLB: FBX сложнее и менее удобен для собственного runtime.

- Mesh: position, normal, UV, indices; дополнительно `JOINTS_0` (4 индекса)
  и `WEIGHTS_0` (4 нормализованных веса). Не разрывать соответствие при оптимизации.
- Skeleton: node hierarchy, локальные bind TRS, skin joint order, inverse bind matrices.
  Сохранять также промежуточные узлы, которые не входят в `skin.joints`.
- Animation: каналы translation/rotation/scale по node, времена в секундах,
  STEP/LINEAR/CUBICSPLINE. Для quaternion — кратчайшая дуга и нормализация;
  кубические тангенсы требуют отдельного корректного импорта.
- **ozz-animation** по плану проекта: offline-конвертация skeleton/clips,
  sampling и blending. Не писать параллельную собственную библиотеку сэмплинга.
- GPU: палитра `inverse(meshWorld) * jointWorld * inverseBind`, затем
  `skinnedPosition = sum(weight * boneMatrix * position)`. Нормали тоже деформировать;
  при неравномерном масштабе требуется корректная normal matrix.
  Использовать skinning и в основном проходе, и в тенях; bounds учитывать анимацию.

## Подключение в HAN, по шагам

1. A: импорт skinned mesh и палитра костей, GPU-скиннинг и тени, тест bind pose.
2. B: ozz-проигрыватель; idle/walk/jog/sprint по **реальной** скорости персонажа,
   переходы с коротким crossfade; jump start/air/land по grounded/vertical velocity.
3. Использовать существующие `PlayerController`, `CameraRig`, Jolt:
   WASD относительно yaw камеры, мышь поворачивает камеру, плавный поворот модели,
   гравитация/прыжок у физики, камера со sweep от стены. Не создавать второй контроллер.
4. Подключить jab/cross к существующим startup/active/recovery из CombatSim.
   Визуальный контакт синхронизировать с боевым кадром; анимация не должна сама
   начислять урон. Проверить кулак, разворот корпуса, контакт и попадание.
5. Для остальных искусств — отдельные лицензированные клипы/собственные движения
   и ретаргет на тот же humanoid. Нынешние 43 клипа **не покрывают все стили**.
   Парные захваты/броски требуют A4 и синхронизации двух скелетов.

## Если создавать с нуля в Blender

Единицы Metric, 1 unit = 1 м; взрослый силуэт 1.8 м, петли геометрии вокруг суставов,
закрытая голова без черт лица. Armature: root → pelvis → spine → neck/head,
clavicle/upperarm/lowerarm/hand + пальцы, thigh/calf/foot/toe. Симметричные имена `.L/.R`.
Parent → Armature Deform → Automatic Weights, затем Weight Paint: тест локтей,
плеч, таза, коленей и сжатого кулака, убрать влияние противоположных конечностей.
Не применять произвольно transforms к уже анимированному rig.
Экспорт GLB: skinning + animations, Y Up, normals/UV, без лишних объектов;
constraints запечь в ключи. Снова импортировать экспорт и проверить масштаб и клипы.

Mixamo — альтернативный источник с бесплатным использованием в играх по условиям
Adobe, **не CC0**; исходные FBX/клипы нельзя просто выкладывать как свободный пакет.
В этом проекте выбран CC0-набор без необходимости аккаунта Adobe.

## Чек-лист принятия A1

- [x] Загружен оригинал, проверены CC0 и хэш; есть воспроизводимая установка.
- [x] Подготовлен GLB 180 см, светлый материал, 65 костей, 43 клипа.
- [x] HAN рисует деформируемый mesh и тени; Vulkan smoke на llvmpipe 0/0.
- [x] Idle/walk/run/sprint подключены к скорости и crossfade; фаза шага зависит от расстояния.
- [x] Боксовый футворк проверен по геометрии: восемь направлений, опорные стопы и ширина стойки.
- [x] Jump start/air/land переключаются по grounded/vertical velocity, есть тест переходов.
- [ ] WASD, мышь и коллизии камеры проверены в живом демо.
- [ ] Jab/cross синхронизированы с боем; GPU/CPU время измерено на RTX 2050.
