# Ассеты роли A

| Ассет | Автор | Источник (URL) | Лицензия | Где используется | Изменён? |
|---|---|---|---|---|---|
| tests/engine/data/blender_cube_cone.glb | Проект HAN (собственная работа: куб и конус, созданы в Blender) | https://github.com/KIR223/han-fists-of-seoul | CC0 | Тест импорта glTF в tests/engine | нет |
| tests/engine/data/blender_textured_cube.glb | Проект HAN (собственная работа: куб с шахматной текстурой 64x64, создан скриптом make_textured_fixture.py в Blender) | https://github.com/KIR223/han-fists-of-seoul | CC0 | Тест импорта glTF с текстурой в tests/engine | нет |
| PR_Lantern01 (assets_bin/cooked/PR_Lantern01.hmesh) | Rajil Jose Macatangay | https://polyhaven.com/a/Lantern_01 | CC0 | пропсы храмов и праздник фонарей (G54), витрины и лавки | да — упрощён до бюджета пропса, пивот в центре основания, LOD1–3, коллизия; покраска и корёизация — позже |
| assets_src/osm/ (выгрузки OpenStreetMap: Мапо и др.) | участники OpenStreetMap | https://www.openstreetmap.org/copyright | ODbL 1.0 (share-alike только для производной базы данных; атрибуция «© участники OpenStreetMap» в игре обязательна) | геометрия дорог, зданий, парков и воды для генератора города (E10); в Git не хранятся, скачиваются han_osm_fetch.py | да — берётся только геометрия, теги названий и брендов не используются |
| TX_road_asphalt (assets_src/tx_road_asphalt) | Rob Tuytel | https://polyhaven.com/a/asphalt_02 | CC0 | проезжая часть всех улиц города (E19) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
| TX_wall_plaster_beige (assets_src/tx_wall_plaster_beige) | Dimitrios Savva, Rico Cilliers | https://polyhaven.com/a/beige_wall_001 | CC0 | фасады жилых домов и вилл (G6, G17) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
| TX_wall_brick_brown (assets_src/tx_wall_brick_brown) | Rob Tuytel | https://polyhaven.com/a/brown_brick_02 | CC0 | кирпичные виллы и низкие дома (G6) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
| TX_sidewalk_brick (assets_src/tx_sidewalk_brick) | Charlotte Baglioni | https://polyhaven.com/a/brick_pavement_02 | CC0 | тротуары и пешеходные зоны (G6) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
| TX_roof_bitumen (assets_src/tx_roof_bitumen) | Rob Tuytel | https://polyhaven.com/a/bitumen | CC0 | плоские крыши домов (G6) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
| TX_wall_metal_sheet (assets_src/tx_wall_metal_sheet) | Amal Kumar | https://polyhaven.com/a/box_profile_metal_sheet | CC0 | гофрированные навесы, склады, промзона (G51) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
| TX_ground_grass (assets_src/tx_ground_grass) | Rob Tuytel | https://polyhaven.com/a/aerial_grass_rock | CC0 | парки и газоны (G6) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
| TX_wall_concrete (assets_src/tx_wall_concrete) | Dario Barresi, Charlotte Baglioni | https://polyhaven.com/a/concrete_wall_008 | CC0 | бетонные фасады офисов и многоэтажек (G6) | да: конвертация JPG в KTX2 с цепочкой мипов; без изменения изображения |
