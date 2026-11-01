# Ассеты роли A

| Ассет | Автор | Источник (URL) | Лицензия | Где используется | Изменён? |
|---|---|---|---|---|---|
| tests/engine/data/blender_cube_cone.glb | Проект HAN (собственная работа: куб и конус, созданы в Blender) | https://github.com/KIR223/han-fists-of-seoul | CC0 | Тест импорта glTF в tests/engine | нет |
| tests/engine/data/blender_textured_cube.glb | Проект HAN (собственная работа: куб с шахматной текстурой 64x64, создан скриптом make_textured_fixture.py в Blender) | https://github.com/KIR223/han-fists-of-seoul | CC0 | Тест импорта glTF с текстурой в tests/engine | нет |
| PR_Lantern01 (assets_bin/cooked/PR_Lantern01.hmesh) | Rajil Jose Macatangay | https://polyhaven.com/a/Lantern_01 | CC0 | пропсы храмов и праздник фонарей (G54), витрины и лавки | да — упрощён до бюджета пропса, пивот в центре основания, LOD1–3, коллизия; покраска и корёизация — позже |
| assets_src/osm/ (выгрузки OpenStreetMap: Мапо и др.) | участники OpenStreetMap | https://www.openstreetmap.org/copyright | ODbL 1.0 (share-alike только для производной базы данных; атрибуция «© участники OpenStreetMap» в игре обязательна) | геометрия дорог, зданий, парков и воды для генератора города (E10); в Git не хранятся, скачиваются han_osm_fetch.py | да — берётся только геометрия, теги названий и брендов не используются |
