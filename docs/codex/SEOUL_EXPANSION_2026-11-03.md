# Расширение Сеула и интеграция Кандама — 2026-11-03

Работа в `a/seoul-expansion`, без push и PR. При `git fetch origin` новых коммитов `origin/main` не было. Из предоставленного `kandam_district_v5.zip` извлечён Git bundle, объединён коммитом `fa436fe`. Название района добавлено отдельным контрактным коммитом `e7744de` в ветке `a/kandam-localization-contract`: при публикации нужен отдельный PR с меткой `contract`.

## Что подключено

- Предоставленный район Кандам: 151 здание, 368 уличных фонарей, семь помещений с отдельным светом. Центр в координатах существующего мира `(10020, 6560)`, высота земли около 24,5 м. Удалены внешние декорационные кварталы и огромная подложка исходного превью; рельеф сведён с районом плавным переходом. Помещения подгружаются отдельно при приближении, физическая геометрия позволяет прогулку по полу и столкновения со стенами/мебелью.
- Материалы glTF: цвет вершин, baseColorFactor, прозрачность BLEND, отдельная emissive-карта и `KHR_materials_emissive_strength`. hmesh v6 хранит новые параметры, читает старые v3–v5. Прозрачные части сортируются сзади вперёд, проверяют глубину без записи в неё; стекло не отбрасывает непрозрачную тень.
- Экстерьер разбит на 2458 пространственных частей по 64 м для отсечения; геометрия 1 388 968 треугольников. Семь интерьеров — ещё 21 650. Коллизия экстерьера — 415 076 треугольников. Удалённое отсечение и выгрузка есть, полноценные LOD района пока отсутствуют.
- Четыре вымышленные машины Comrade1280 (CC-BY 4.0): компакт, седан, минивэн, SUV. У каждой четыре уровня LOD и меш коллизии. Перенастроены кузов, стекло, оптика, глянец преобразован в шероховатость, отделён демонстрационный стенд. Источник и атрибуция: `docs/credits/A.md`, профили: `data/city/vehicle_*.json`.
- Общие фасады за пределами Кандама: в области стекла удаляется рельеф кирпича/штукатурки, шероховатость задаётся `window_roughness` в `streetscape.json`.
- Трафик использует дорожную сеть Кандама, учитывает центральную разделительную полосу; отбор мест появления ограничен дорогами рядом с игроком. Ранее при выборе из всей расширенной карты почти все попытки оказывались вне радиуса. Перед показом сцены выполняется симуляция `warmup_seconds` из данных.

## Расширенная карта

Скачан открытый срез [Geofabrik South Korea](https://download.geofabrik.de/asia/south-korea.html) от 2026-11-02, 290 768 655 байт. SHA-256: `523cbb385d81ff81aa299dbcda3202ec8124449330beec1bbde7f0aa7df8c4da`.
Данные OpenStreetMap contributors / Geofabrik, ODbL 1.0; исходные названия и бренды исключаются при извлечении. Сами OSM/PBF и готовые ячейки в Git не включаются.

Область: широта 37.469–37.579, долгота 126.880–127.112. Извлечено 174 715 ways, включая 103 823 здания и 62 014 дорожных объектов; отсутствующих ссылок на узлы нет. Это базовая геометрия выбранной области, не законченная художественная карта всего Сеула.

При соединении с Канда́мом удалено 413 пересекающих его старых зданий, обрезано 230 старых дорог, добавлено 27 ways. Сеть района соединена с периметром и внешними дорогами общими идентификаторами узлов. Тест на реальных локальных данных нашёл маршрут Мапо → Кандам: 79 участков, 15 830,6 м; граф 88 479 направленных рёбер.

В `assets_bin/city/seoul` запечено 3840 непустых ячеек, 103 266 зданий, 60 558 дорог, 3583 парка, около 2,44 ГиБ данных. Демо автоматически выбирает расширенную карту, если эти ячейки и `seoul_connected.osm` существуют; явные `--osm`/`--cooked` сохраняют выбранный источник.

## Повторение подготовки

Все команды запускаются из корня. Нужны собранные инструменты, Blender и указанные в конвейере Python-зависимости; для PBF отдельно `osmium`. В этой сессии osmium и конвертеры установлены изолированно в `build/asset-tools`, а не в зависимости игры.

```powershell
$env:PYTHONUTF8='1'
python han/tools/pipeline/inspect_assets.py C:/Users/insf9/Downloads/new --out assets_bin/review/new --report build/new_assets_review.json
python han/tools/pipeline/han_osm_extract.py assets_src/osm/south-korea-261102.osm.pbf assets_src/osm/seoul.osm --config data/city/seoul_extent.json
python han/tools/pipeline/connect_district.py assets_src/osm/seoul.osm assets_src/osm/seoul_connected.osm --profile data/city/kandam_layout.json
python han/tools/pipeline/prepare_district.py assets_bin/inbox/kandam_v5/package_v5/kandam_district.glb assets_bin/processed/kandam/kandam_district.glb --half-width 365 --half-depth 320 --lights data/city/kandam_lights.json --collision assets_bin/processed/kandam/kandam_collision.glb --interiors-meta assets_bin/inbox/kandam_v5/package_v5/interiors.json
build/windows-release/bin/Release/han_cooker.exe --spatial-size 64 assets_bin/processed/kandam/kandam_district.glb assets_bin/cooked/kandam/kandam_district.hmesh
build/windows-release/bin/Release/han_cooker.exe assets_bin/processed/kandam/kandam_collision.glb assets_bin/cooked/kandam/kandam_collision.hmesh
build/windows-release/bin/Release/han_cooker.exe assets_bin/inbox/kandam_v5/package_v5/kandam_interiors.glb assets_bin/cooked/kandam/kandam_interiors.hmesh
build/windows-release/bin/Release/han_citycook.exe --osm assets_src/osm/seoul_connected.osm --out assets_bin/city/seoul --terrain assets_bin/terrain/seoul.hht --bounds -3000 -2500 17500 9800
python han/tools/pipeline/han_fetch.py process vh_city_compact
python han/tools/pipeline/han_fetch.py process vh_city_sedan
python han/tools/pipeline/han_fetch.py process vh_city_minivan
python han/tools/pipeline/han_fetch.py process vh_city_suv
```

При использовании изолированной установки osmium перед извлечением выставить `PYTHONPATH=build/asset-tools`. Папку `package_v5` сначала извлечь из предоставленного архива; промежуточные файлы локальные, в репозитории хранятся код, профили и атрибуция. Полная генерация Кандама с нуля на Windows ещё не проверена: исправлен выбор Noto вместо жёсткого Linux-пути, готовые предоставленные GLB успешно подготовлены и проверены в HAN.

## Проверки и фактические ограничения

- `cmake --build --preset windows-release --parallel 4`: успешно. `ctest --preset windows-release --no-tests=error`: 3/3 набора успешно (после исправления отбора дорог).
- Конвейер Python: 102 теста успешно, включая подготовку машин через настоящий Blender и выбор шрифта Windows/Linux.
- `python data/_tools/validate_data.py`: 39 файлов успешно. `han_fetch.py verify`: манифест из 24 ассетов в порядке.
- Все десять сцен `tests/engine/screenshots/run.py` совпали с прежними эталонами пиксель в пиксель; эталоны не перезаписывались.
- Внешняя улица и прогулка внутри магазина проверены на RTX 2050, 1280×720, без UI, с Vulkan validation: ноль ошибок и предупреждений. `--camera` задаёт высоту над рельефом, поэтому уличные кадры сняты с высоты 2 м. Превью из входного архива не выдаются за кадры HAN.
- До исправления выбора дорог: день — 27,17 мс среднее / 27,69 мс p95; ночь — 14,54 / 14,57 мс. Магазин ночью с физической прогулкой — 14,62 / 14,51 мс. Эти замеры не подтверждают 60 FPS для всей карты; итог с заполненными улицами указан в прогрессе.
- После исправления выбора дорог: 120 машин и 280 прохожих; день — 27,31 мс среднее / 31,57 мс p95, ночь — 14,65 / 14,35 мс. Снимки: `build/seoul/traffic-10.png`, `traffic-21.png`, магазин — `kandam-interior-walk.png`; просмотрены. Логи: `build/traffic-10.log`, `traffic-21.log`. Замер включает CPU и ожидание GPU, не является отдельным GPU-профилем.
- Просмотрены четыре крупных плана `build/seoul/car-*.png` из `han_scene_demo`: материалы восстановлены, выявлен и исправлен разворот носа машин относительно направления дороги (профиль `yaw_degrees=180`).
- При визуальной проверке также исправлена оптика: используется отдельная исходная `lights.jpg`, а не цвет кузова. Все четыре модели и LOD пересобраны и повторно просмотрены; тест проверяет раздельные карты кузова/оптики. После этого весь Python-конвейер повторно прошёл 102/102. Четыре крупных плана — ноль ошибок/предупреждений Vulkan.
- Проверки зон и атрибуции нового блока: `python tools/han_zones.py check --base e7744de --head HEAD --branch a/seoul-expansion` — OK; `python tools/ci/credits_check.py --base e7744de --head HEAD` — OK (новых отслеживаемых бинарных ассетов нет); `git diff --check` — OK. База выбрана после отдельного контрактного коммита: результат не означает готовность всей ранее накопленной локальной истории к одному PR от `origin/main`. При публикации разделить контракт и предшествующие изменения разных зон согласно `WORKFLOW.md`.

Следующие по плану: LOD/HLOD Кандама и бюджет 16,6 мс; вода Хангана и мосты; перенос объёмных фасадов и разнообразия Кандама в остальные районы; качественные взрослые персонажи и анимации; проход по районам с дневными/ночными кадрами и проверкой маршрутов.

Мультиполигональные отношения OSM пока не преобразуются: Ханган, острова и сложные парки неполны. Высоты мостов и вертикальная навигация не доведены. За пределами Кандама здания преимущественно процедурные и не достигли его детализации. Сохранены существующие географические координаты проекта; сжатие карты из части 7 дизайна не внедрялось. Витрины/манекены и прохожие ещё упрощены, уровень GTA V не достигнут. Загрузка района при первом подходе синхронная, полноценный стриминг его текстур и LOD остаётся задачей. Этапы E4/E9/E10/G6/G21/G48 не закрыты.

Полная таблица исходных ассетов: [ASSETS_NEW_2026-11-03.md](ASSETS_NEW_2026-11-03.md).
