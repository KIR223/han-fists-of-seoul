# Библиотека ассетов и новые папки — 2026-11-04

Точка входа из корня проекта: [ASSETS_LIBRARY.md](../../ASSETS_LIBRARY.md). Локальные бинарники и полный каталог: `assets_bin/library/`, вне Git. Манифест `manifest.json`, таблица `catalog.csv`, у каждого пакета `source/`, `content/`, `asset.json`. Материалы и зависимости сохранены вместе с моделями. Это исходники для дальнейшей подготовки, не массовая установка в runtime.

## Что проверено

Весь H:\new: **250 файлов**, включая ранее проверенные 179. Две новые папки: **71 архив** — 16 комплектов декора/еды/растительности и 55 комплектов материалов. В новых архивах **20 файлов моделей и 620 изображений**. Рекурсивная распаковка, проверка изображений, импорт всех 20 моделей Blender с отключённым autoexec, метаданные UV/материалов и обзорные листы выполнены без ошибок импорта/декодирования. Просмотрены лист моделей и восемь листов изображений. Большинство серий включает альтернативные Normal DirectX/OpenGL, gloss/roughness и дополнительные карты; это не разные материалы.

Обзор старых 179 исходников и ограничения C4D/SKP/FBX-копии: [ASSETS_HNEW_2026-11-04.md](ASSETS_HNEW_2026-11-04.md). Нативные ограничения не устранены и не скрыты при переносе.

## Практическое применение новых комплектов

| Группа | Где полезна | Подготовка |
|---|---|---|
| Кресла, диван, стол | Квартиры, кафе, офисы | Снять избыточную геометрию, запечь складки/швы, LOD и коллизии; Replica — проверить узнаваемый дизайн и заменить детали |
| Булочки, виноград, сырная тарелка | Витрины, пекарни, кафе и кухни | Снизить геометрию, объединить материалы/атласы; это декор, игровые механики еды не добавлялись |
| Агава, фикус, монстера, орхидея, суккулент, замиокулькас | Интерьеры, террасы, витрины | Пересобрать тяжёлую листву и грунт; комнатные растения не считать уличной флорой Сеула на все сезоны |
| Камень, бревно, пень | Парки, окраины, природные участки | LOD, простая коллизия, разнообразие масштаба и ориентации |
| Асфальт, кирпич, бетон, штукатурка, камень | Дороги и фасады | Масштаб тайлинга, UV и тип поверхности; не путать декоративную кладку с дорожным покрытием |
| Металл, древесина, пластик, ткани | Мебель, ограждения, навесы, интерьеры | Выбрать корректные PBR-каналы и цветовое пространство |
| Плитка, терраццо, мозаика | Кафе, вестибюли, санузлы, бассейн по дизайну | Выдержать размер плитки, roughness и мокрые варианты |
| Грязь на стекле, царапины, отпечатки и капли | Окна, витрины и поверхностные маски | Использовать как маски смешивания, а не равномерно покрывать весь город |

## Лицензии

Новые серии по именам/структуре соответствуют семейству Poliigon, но не все индивидуальные карточки и права аккаунта подтверждены. В архивных проверках не найдено достаточного свободного допуска. Все 71 комплект сохранены локально в **pending_rights**, без присвоения CC0 и без подключения к сборке.

[Условия Poliigon](https://www.poliigon.com/terms) и [официальное пояснение лицензий](https://help.poliigon.com/en/articles/8749749-asset-use-licensing) допускают коммерческие проекты в рамках соответствующего типа лицензии, однако ограничивают передачу исходных ассетов. Бесплатная лицензия распространяется на ограниченный набор бесплатных позиций; право на каждый предоставленный комплект нельзя вывести из наличия ZIP. Реплики мебели дополнительно требуют проверки брендов/узнаваемых форм по правилам HAN. Полное техническое описание не означает юридический допуск.

Подтверждённые новые кандидаты предыдущего аудита — Stone Atelier, дорожные модули, Modern Night Building и взрослые Quaternius — внесены в docs/credits/A.md; физическая библиотека хранит их отдельно в approved. Наличие в approved означает проверенную лицензию, а не завершённую игровую подготовку.

## Геометрия новых моделей

Треугольники рассчитаны по импортированной сетке сцены аудита. Это не стоимость конкретного кадра HAN и не замер производительности.

| Исходник | Файл модели | Треугольники | Mesh-объекты |
|---|---|---:|---:|
| `ArmchairLeyaWingbackReplica001.zip` | `ArmchairLeyaWingbackReplica001_Blender_Cycles.blend` | 903,390 | 2 |
| `ArmchairSlopeReplica001.zip` | `ArmchairSlopeReplica001_Blender_Cycles.blend` | 53,742 | 1 |
| `CoffeeTableNimbusGoldReplica001.zip` | `CoffeeTableNimbusGoldReplica001_Blender_Cycles.blend` | 42,032 | 1 |
| `FoodCinnamonRolls001.zip` | `FoodCinnamonRolls001_Blender_Cycles.blend` | 9,134 | 2 |
| `FoodGrapes001.zip` | `FoodGrapes001_Collection01_Cycles.blend` | 52,820 | 2 |
| `FoodGrapes001.zip` | `FoodGrapes001_Grape001_Cycles.blend` | 802 | 1 |
| `FoodGrapes001.zip` | `FoodGrapes001_Grape002_Cycles.blend` | 856 | 1 |
| `FoodGrapes001.zip` | `FoodGrapes001_Grape003_Cycles.blend` | 960 | 1 |
| `FoodGrapes001.zip` | `FoodGrapes001_Vine001_Cycles.blend` | 16,326 | 1 |
| `FoodPlatter006.zip` | `FoodPlatter006.blend` | 143,626 | 16 |
| `PlantAgave002.zip` | `PlantAgave002_Blender_Cycles.blend` | 560,649 | 2 |
| `PlantBanjoFig001.zip` | `PlantBanjoFig001_Blender_Cycles.blend` | 1,109,738 | 2 |
| `PlantMonstera001.zip` | `PlantMonstera001_Blender_Cycles.blend` | 369,872 | 2 |
| `PlantOrchid001.zip` | `PlantOrchid001_Blender_Cycles.blend` | 263,960 | 2 |
| `PlantSucculentEcheveria001.zip` | `PlantSucculentEcheveria001_Blender_Cycles.blend` | 328,787 | 2 |
| `PlantZZ001.zip` | `PlantZZ001_Blender_Cycles.blend` | 958,694 | 2 |
| `RockBoulderLarge054.zip` | `RockBoulderLarge054_Blender_Cycles.blend` | 50,373 | 1 |
| `SofaReversiReplica001.zip` | `SofaReversiReplica001_Blender_Cycles.blend` | 237,924 | 1 |
| `TreeLog008.zip` | `TreeLog008_Blender_Cycles.blend` | 49,999 | 1 |
| `TreeStump024.zip` | `TreeStump024_Blender_Cycles.blend` | 50,000 | 1 |

Фикус — 1,11 млн, кресло Wingback — 903 тыс., замиокулькас — 959 тыс. треугольников. Сохранять как высокодетальные исходники, перед игрой делать облегчённые версии. У винограда пять файлов моделей: общий комплект и отдельные составляющие; не считать их пятью разными гроздьями.

## Полный перечень новых комплектов

Для всех ниже статус — pending_rights. Оригиналы и распакованный комплект сохранены вместе; точный путь — в локальном catalog.csv/manifest.json.

| Исходник | МиБ | Файлы моделей / изображения | SHA-256 |
|---|---:|---:|---|
| `decor and food/ArmchairLeyaWingbackReplica001.zip` | 216.06 | 1 / 15 | `851d7bdae1621238ef9f0a5cc98dcd72a908c4ead5792370f18f3d8b798fc1ca` |
| `decor and food/ArmchairSlopeReplica001.zip` | 55.66 | 1 / 6 | `c2d095f236496694325bb3c6e928182aef1be7b0adebdd44f0a6e4428967eb50` |
| `decor and food/CoffeeTableNimbusGoldReplica001.zip` | 35.49 | 1 / 5 | `19eac4f1c3a3983040dc8d5e7284b378ac34da819f0521c1ea674b4b5b1d032d` |
| `decor and food/FoodCinnamonRolls001.zip` | 42.17 | 1 / 10 | `86087c07a8cf7d596a888364366c6ff8b81deb001d1d030f07aacbccc872f329` |
| `decor and food/FoodGrapes001.zip` | 25.95 | 5 / 22 | `149c9b8a6f400cf8acdced44854062d320b3e042bf123faa45b0abc80acedafa` |
| `decor and food/FoodPlatter006.zip` | 142.11 | 1 / 37 | `be207852dae7dcd5ef1e03f46d28c38d2bd6eecbb170b9ea6ee27b2c3421d9ed` |
| `decor and food/PlantAgave002.zip` | 97.37 | 1 / 6 | `0c47c20bafab64fa22a9b9842195ec2679ad4dfa77ccb030e6548c61a3ec84fb` |
| `decor and food/PlantBanjoFig001.zip` | 183.88 | 1 / 10 | `95ae0ec4727cf1dc7c64d3ed1e8423839cbf7497cd8f02d3694728afaf66a772` |
| `decor and food/PlantMonstera001.zip` | 119.89 | 1 / 10 | `cb8d5c2f30bae3b07a31f097ffb75b0c102dec16fffa58a45139d7f4dccfbdfc` |
| `decor and food/PlantOrchid001.zip` | 68.51 | 1 / 6 | `6151a9a6b002e31acf0fc20c36d8efbd553a6eebb5cec91440ed3c3353df39da` |
| `decor and food/PlantSucculentEcheveria001.zip` | 43.61 | 1 / 6 | `b2bad9462599ee9aa31c787a1295212653e5bdc23f99318b418ed869503f848e` |
| `decor and food/PlantZZ001.zip` | 108.94 | 1 / 10 | `c0faf6ccd981e79d3f00399dba47c6d6a9407bb91c8134aea95fee08248c9a8b` |
| `decor and food/RockBoulderLarge054.zip` | 186.02 | 1 / 12 | `0a05b0f6cb316ce413b1d58c7ba60cb66c72954d6989906332e92effabc91e59` |
| `decor and food/SofaReversiReplica001.zip` | 117.65 | 1 / 7 | `c68562af6e04a9c65b18a8148adf9ecb6e21a1b76784fc8e2da2a11a6f2dd81d` |
| `decor and food/TreeLog008.zip` | 164.97 | 1 / 12 | `c9698fd888932cec3394dbc95f16fc2414c00dd2ee0d65c2feaadd235197c40a` |
| `decor and food/TreeStump024.zip` | 192.90 | 1 / 12 | `3ed178b1aa46e45891c7a0acecb22b648e2d0b580ed2bbd07bfa9ee4a7dffc39` |
| `new material and texture/BarkPoplar001.zip` | 57.33 | 0 / 8 | `b7ad22af9753d2c2b6d972e3708bb590a05f399b633798e29e5ad8280f4d809b` |
| `new material and texture/BricksDragfacedRunning008.zip` | 129.11 | 0 / 10 | `a9b2e41ada4ff0f95c3fb5fd6c3a288bc84d16f7161911948b14e262bb64c8e6` |
| `new material and texture/BricksLongThinRunningExtruded001.zip` | 204.57 | 0 / 10 | `9cecb3172937fd94bc7664fb958cd83e591e13ffa0a044ce53c92dc540af5cad` |
| `new material and texture/CityStreetAsphaltGenericClean001.zip` | 28.91 | 0 / 8 | `87b5e4b62c6246cc39cacd35751ad6b553fd11671099a6b40e963edf8dee2d25` |
| `new material and texture/DirtWindowStains005.zip` | 35.99 | 0 / 6 | `6acef3709f35632c2a0524f91f11fc157405489a8c5054309086f415a2afd023` |
| `new material and texture/FabricPlainNaturalSheer009.zip` | 28.08 | 0 / 7 | `721956a643909921bad5f8e620bf469f1502a99e529c523d0b33af8d091ecbc0` |
| `new material and texture/GroundDirtRocky020.zip` | 306.17 | 0 / 10 | `11fa6c5edc85176040ceb112a3a555ba2421569d2a2761abe0163a9ef02a8402` |
| `new material and texture/GroundSand005.zip` | 40.79 | 0 / 10 | `e64ee3cc55f6a658418747302f1ede756a1bf194e497b19711e465c691429989` |
| `new material and texture/GroundWoodChips001.zip` | 150.26 | 0 / 10 | `4c04fd3d0b0f3e64bc80f2361e675eeecc89040b007c3fe3fe262e45e2ede2d0` |
| `new material and texture/MetalCorrodedHeavy001.zip` | 44.62 | 0 / 8 | `859ccb6f2d17fddf3b11ad30237b63b4cde071bb405691b6d82652479477b756` |
| `new material and texture/MetalGalvanizedSteelWorn001.zip` | 26.88 | 0 / 6 | `8c8173dc22258f98878d95c1d6fc1b89edd5e446c870226dcb414b75ecf576d1` |
| `new material and texture/Poliigon_BoucleFabricBubbly_7827.zip` | 24.18 | 0 / 7 | `3ee9aa6e3bc7cba5a826f9e8e83f49c02f82511e866be9893e754a98b48126a0` |
| `new material and texture/Poliigon_BrickReclaimedRunning_7787.zip` | 96.38 | 0 / 7 | `0a74d3a97bc0f5b4f80360aaaa030e40323658287b582a304de79fb2c1b8268d` |
| `new material and texture/Poliigon_BrickWallReclaimed_8320.zip` | 79.16 | 0 / 7 | `5eb75f895f7cefb30e0c4449557ebd56177159a393631e43f9b800042afe37ef` |
| `new material and texture/Poliigon_CeramicPotteryGlazed_10828.zip` | 183.28 | 0 / 7 | `94c1e0ea1db5e0d27d85b49aed458550479f453ab8b488e31c6c4f3ddb0ba44e` |
| `new material and texture/Poliigon_CeramicPotteryGlazed_10861.zip` | 11.52 | 0 / 7 | `e07416fa008c31eba1d2205329cec04a1afb6fd7afd237998b5c1b29be625715` |
| `new material and texture/Poliigon_ConcreteFloorPoured_7656.zip` | 11.86 | 0 / 7 | `5ae654b0aa938fb74a51360f8b162730d32d6e027cb6ae021413ee897232bca0` |
| `new material and texture/Poliigon_ConcretePaversSquare_7100.zip` | 18.47 | 0 / 7 | `2973dfdbb05e2b91b869902fe889659d4e706a865af6e58bc7ee69e83cabd1cc` |
| `new material and texture/Poliigon_ConcreteWallCladding_7856.zip` | 45.53 | 0 / 7 | `fc8f8033b944e4cb1b0f5a66025a10f54e3a64e203501e3a035c25a3eee6e6c1` |
| `new material and texture/Poliigon_ConcreteWorn_8690.zip` | 19.35 | 0 / 7 | `63d36ad0159e12f2bf10f8891f54ffa81d49044c548347351fc3eadf51b62fd5` |
| `new material and texture/Poliigon_FoodCoffee_10827.zip` | 14.14 | 0 / 8 | `b519a171c4d9f9b04da49c5ae5dad70eda12eea46bbcb926aad21b0df61d35d9` |
| `new material and texture/Poliigon_FoodGlazeStrawberry_10739.zip` | 181.08 | 0 / 8 | `e8a7d0b84c1948ee665582f798b79e9629d95f6a0d2b4a421c6ef0d48eecde23` |
| `new material and texture/Poliigon_FoodPastryDonut_10737.zip` | 21.12 | 0 / 8 | `a9d3ec7e6eefd4a8312641a33ff1a9e0dd7d2fbe682bc987c8e95d99b8a5c923` |
| `new material and texture/Poliigon_GrassPatchyGround_4585.zip` | 32.50 | 0 / 7 | `5cfd7363f3ad9db5efb2b17c3e5e538161058ba99dd86e61ca9256b68df22cf4` |
| `new material and texture/Poliigon_GroundFieldAgriculture_12105.zip` | 355.44 | 0 / 8 | `81bcfd17cf10612be24d4d2326326a07f55dd06be47a3e8019b9bd62c675645a` |
| `new material and texture/Poliigon_MetalBronzeWorn_7248.zip` | 14.74 | 0 / 7 | `52ed6d269cf6b6fe63eb5bc3fbf43a6271e825e488cf672f417400ff61fbb740` |
| `new material and texture/Poliigon_MetalGalvanizedZinc_7184.zip` | 10.86 | 0 / 7 | `038f36ff6091e590005dd6a6c926fda4ae5971c5ca6bf9d66bdb69b18621cc8b` |
| `new material and texture/Poliigon_MetalGoldPaint_7253 (1).zip` | 10.87 | 0 / 7 | `b9bb82be132e68fb177c095886c645d7a61127a03e9b35d3a53e4fb780d0d5d1` |
| `new material and texture/Poliigon_MetalGoldPaint_7253.zip` | 10.87 | 0 / 7 | `0e841a438e8ceb97447573cf3270e917c920fecaf7e351be0cea43ee30fb4cff` |
| `new material and texture/Poliigon_MetalPaintedMatte_7037.zip` | 10.84 | 0 / 7 | `9075d14aa181cfa8f62ea4954420dcb67824a12d071e194c798cf2bff8332566` |
| `new material and texture/Poliigon_MetalRust_7642.zip` | 67.44 | 0 / 7 | `96dfb5a62926c96e0116a6744a44965c37de408308b30be5f1bfdab675bbb1d2` |
| `new material and texture/Poliigon_MetalSteelBrushed_7174.zip` | 126.77 | 0 / 7 | `98b3b642b8afa4004bdb28463e704cb5ca9922b6d942825eca4e2677a6a564a0` |
| `new material and texture/Poliigon_PlasterPainted_7664.zip` | 15.17 | 0 / 7 | `c8781094d4f684e0c10a223dae834bd2c381162f9c490e947c6bf7386acd8779` |
| `new material and texture/Poliigon_PlasticMoldDryBlast_7495.zip` | 17.65 | 0 / 7 | `ea3f96db88a1c86b3c26577bcf6c871cab24279dd72b536e9122eee60ee220ea` |
| `new material and texture/Poliigon_PlasticMoldWorn_7486.zip` | 8.68 | 0 / 7 | `021ee90fc00580c60f44a40d77b8e0fa31496bba0c737d93a4dbd0036f720b83` |
| `new material and texture/Poliigon_RattanWeave_6945.zip` | 16.62 | 0 / 7 | `cf0a331effe342de639ac0e9ffd16cac3d7999315c6d793ae0a27e4059dbc52e` |
| `new material and texture/Poliigon_SlateFloorTile_7657.zip` | 36.92 | 0 / 7 | `dff576aae788e33a8619275ec595571810da340ea7b55ae5b3cf4e69e8a3b324` |
| `new material and texture/Poliigon_StoneQuartzite_8060.zip` | 15.31 | 0 / 7 | `dd84b55c01048c1aa037e148a3fa0be3d49c161e83ed69bc9e3d6685764843ec` |
| `new material and texture/Poliigon_TerrazzoTilePolished_4818.zip` | 12.38 | 0 / 7 | `db649402900561f8bb4913755beb3a2e991b05fa6d32e3502ac54bb899798d3f` |
| `new material and texture/Poliigon_WoodFloorAsh_4186.zip` | 21.31 | 0 / 7 | `04553f8fc8765976cb5a7006f58e07c891a3a6e9a61566348f6a467afa7d4144` |
| `new material and texture/Poliigon_WoodRoofShingle_7834.zip` | 36.37 | 0 / 7 | `1b425e244d1010bb95edde4b2a10645c4e22841dc69ce199a6c00a5d327e7ddb` |
| `new material and texture/Poliigon_WoodVeneerOak_7760.zip` | 7.33 | 0 / 7 | `0a7a6f1abcca836fc6035843b1127d55b180a2ff466d108f11e01ee5588cee5f` |
| `new material and texture/RammedEarth006.zip` | 106.04 | 0 / 8 | `a418defe070d2e4cdc1b627b029c6b2bbac154367fbd1350c5f29a713de58014` |
| `new material and texture/RammedEarth018.zip` | 28.91 | 0 / 8 | `a3a8ce7728eed8f5970162e7c1580c9f568f70d365e905974d13c1cfbebe9457` |
| `new material and texture/ScratchesLight001.zip` | 10.39 | 0 / 4 | `92de555a27c6a91481663d94d072cc0e49e250592fe2e2ecf6d33d386545d8b3` |
| `new material and texture/SmudgesLarge001.zip` | 8.57 | 0 / 3 | `c62a0da742b65d5f4356e57759bd023216ea34c4879cf9eb987414c54e36c2dd` |
| `new material and texture/StoneBricksSplitface001.zip` | 182.55 | 0 / 12 | `6c4f57c3a74929bfb32757dfee325ed3f477bfb7a7375813c3951beacf90c479` |
| `new material and texture/TerrazzoSlab028.zip` | 24.80 | 0 / 11 | `8d5f7ed9438c99a9d7d55ce8fca9d865cea4097bb330b115b258f3f3c35640a3` |
| `new material and texture/TilesMosaicPennyround001.zip` | 31.39 | 0 / 11 | `a9900e2196deab4247002110dd2001b98601676ae5a9c0358c3e263e7374b2ff` |
| `new material and texture/TilesMosaicYubi003 (1).zip` | 67.86 | 0 / 11 | `87b7455b4b52d8b01548a4a575c23fc67007c4adb0a4c0da73ce5e51004b148f` |
| `new material and texture/TilesSquarePoolMixed001.zip` | 30.47 | 0 / 8 | `f887ae92b6fb76f6e1c3931e70475412e5c52711812d29e8e3c3c73bbe5051b5` |
| `new material and texture/TilesTerracottaBeigeSquareStacked001.zip` | 41.92 | 0 / 10 | `0091540e5bdcf764e01ed8a6d7e2170df4a9455719a4665db31075b2d8f5b097` |
| `new material and texture/TilesTravertine001.zip` | 38.45 | 0 / 9 | `e6ec32837cc78ae07d8b9130fa5ab57dcec3cbd2dbdb01ee2d2901a65baf1c6a` |
| `new material and texture/TowelCotton001.zip` | 18.85 | 0 / 16 | `1503f6c34c731dfd82516492544936bfb58f03ebaf3da8324c7bcc79ba2bf045` |
| `new material and texture/WaterDropletsMixedBubbled001.zip` | 27.78 | 0 / 9 | `e0964a5e93c9e027166c9a4c588d960fb1665fa682d48c2564973e16d52ab156` |

## Отбор всех исходников

Выбрано **241 уникальных пакетов** (15,758,434,609 байт исходников), из них 9 approved и 232 pending_rights. **Девять файлов оставлены в H:\new**: шесть точных дубликатов и три невыбранных пакета.

- `assets, material, texture,building,house,world/city.fbx` — Точный дубликат assets, material, texture,building,house,world/city 1/city.fbx
- `assets, material, texture,building,house,world/city3ds.3DS` — Точный дубликат assets, material, texture,building,house,world/city 1/city3ds.3DS
- `assets, material, texture,building,house,world/Footprint_Sandy_Terrain_2k.rar` — Точный дубликат assets, material, texture,building,house,world/Footprint_Sandy_Terrain_2k (1).rar
- `assets, material, texture,building,house,world/medieval-npc-animation-pack-free-pack.zip` — Три фантазийных движения; не выбраны для взрослых жителей Сеула.
- `assets, material, texture,building,house,world/seoul-city-gameready-render-blender-pbr.zip` — Ранее отклонён: Standard/Megascans без подтверждённого свободного допуска.
- `assets, material, texture,building,house,world/stylised-spire-tower-trellis-vs-supavoxel.zip` — Фантастическая башня 1,48 млн tris; не выбрана для застройки Сеула.
- `assets, material, texture,building,house,world/texture.rar` — Точный дубликат assets, material, texture,building,house,world/city 1/texture.rar
- `assets, material, texture,building,house,world/Textured_Pink_Concrete_Tile_Pattern_01_2k.rar` — Точный дубликат assets, material, texture,building,house,world/Textured_Pink_Concrete_Tile_Pattern_01_2k (1).rar
- `assets, material, texture,building,house,world/The+City.obj` — Точный дубликат assets, material, texture,building,house,world/city 1/The+City.obj

## Состояние переноса

Перенос завершён. Все копии архивов и файлов распакованного содержимого сверены SHA-256 с исходниками. Затем исходные выбранные файлы удалены из H:\new отдельным проходом с повторной проверкой SHA-256. Журналы: copy_verification.json, move_verification.json, move_journal.txt. Оставшиеся девять файлов не удалялись.

Сборка и игровые тесты не запускались: код игры не менялся. Выполнены проверки архивов/изображений, Blender-импорт, сверка копий, целостности каталога и git diff --check. Следующее: закрыть права pending_rights и готовить контрольный квартал/интерьер с LOD, PBR и коллизиями.
