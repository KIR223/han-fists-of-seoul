## Часть 64. Технические приложения


### 64.1. Как Claude работает с этим документом

- Документ большой, поэтому на этапе E0 Claude раскладывает GAME_DESIGN.md на файлы **docs/design/NN_название.md** (по одной части в файле) и создаёт **docs/design/INDEX.md** с оглавлением. Владельцу ничего делать не нужно.
- В каждой сессии читаются: CLAUDE.md, PROGRESS.md, INDEX.md, части 0–2 и **части, нужные текущему этапу** (см. таблицу ниже). Остальные части — по необходимости.
- Если владелец меняет решения, Claude обновляет нужную часть в docs/design и отмечает изменение в CHANGELOG_DESIGN.md.
- При противоречии между частями — спросить владельца; до ответа приоритет у более поздней и более подробной части.


### 64.2. Какие части читать на каком этапе

| Этапы | Части документа |
|---|---|
| E0–E8a | Роль и правила; Главное правило (бесплатно); Движок; Производительность; Память; Стандарты кода; Ассеты; Каталог моделей; Меню и интерфейс; Настройки |
| P1–P8 | Физика; Транспорт; Транспорт: физика подробно; Память и производительность |
| A1–A4 | Анимация; Каталог моделей (скелет, блендшейпы); Как дерутся живые люди (порча анимаций) |
| G1–G5, G41, G45, G47 | Управление; Боевая система (ядро, углублённая, в деталях); Как дерутся живые люди; Боевые искусства; Боевая база данных; Игра 18+ |
| G6–G12 | Сеул; Карта; Районы; Живой город; Живые люди; Атмосфера; Графика (библия и свет); Сюжет (пролог, Акт 1); Миссии |
| G13–G16 | Боевые искусства; Обучение и мини-игры; Навыки |
| G17–G22 | Районы; Сюжет; Миссии (все); Катсцены; Озвучка |
| G23–G31 | Экономика; Телефон; Стрим; Кастомизация; Мода; Активности; Просто жить в Сеуле; Мелочи |
| G32–G47 | соответствующие части по названию этапа |
| E17–E20, G51–G56 | Карта целиком; За городом; Храмы, святыни и общественные места; Текстуры и материалы всей карты; Ассеты из интернета; Графика; Каталог моделей; Память |
| E13–E16 | Производительность; Память; Всё для полноценной игры; Авторские права; Главное правило |


### 64.3. Структура данных проекта

```
data/
  combat/   moves/*.json  styles/*.json  combat_rules.json  hit_reactions.json  finishers.json
  ai/       npc_fighter_profiles.json  archetypes.json  personalities.json  rating_100.json
  terrain/  region_layout.json  biomes.json  heightmap_sources.json  vegetation.json  tides.json
  materials/ library.json  seasons.json  decals.json
  world/    districts/*.json  region/*.json  places/{temples,public}.json  weather.json  time_of_day.json  seasons.json  calendar.json
  city/     brands.json  shop_names.json  signs_dictionary.json  interiors.json
  people/   archetypes.json  regulars.json  barks/*.json  reactions_matrix.json
  vehicles/ *.json  traffic.json  police.json
  economy/  prices.json  jobs.json  businesses.json  housing.json  food.json
  progress/ skills.json  talismans.json  xp_curves.json  achievements.json
  story/    missions/*.lua  dialogues/*.json  cutscenes/*.seq  choices.json
  ui/       *.rml  *.rcss  tokens.rcss
  loc/      ru.csv  ko.csv  en.csv
  audio/    music.json  radio/*.json  vo_index.json
CREDITS.md  licenses/  VOICE_RIGHTS.md  AI_USAGE.md  assets_manifest.json
```


### 64.4. Пример: приём (MoveData)

```
{
  "id": "boxing_left_hook",
  "style": "boxing",
  "name_key": "move.boxing.left_hook",
  "unlock_level": 1,
  "input_master": { "button": "LMB", "dir": "left" },
  "simple_rhythm_slots": ["chain1.3", "chain2.3"],
  "trajectory": "arc_left", "limb": "left_hand", "height": "head",
  "movement_variant": "static",
  "frames": { "startup": 11, "active": 3, "recovery": 16 },
  "cancel_window": [14, 22],
  "optimal_range": "mid",
  "damage": 10, "stamina": 6, "balance": 10,
  "on_block_frames": -4, "on_hit_frames": 2,
  "armor": false, "unblockable": false,
  "impulse": 140,
  "hitbox": { "bone": "hand_l", "radius": 0.07, "reach_curve": "curves/hook_l.json" },
  "fx": { "hitstop_ms": 85, "shake": 0.5, "sound": "impact_heavy_face" },
  "anim": "anims/boxing/hook_l.ozz",
  "variants": ["step_in", "retreat", "pivot"]
}
```


### 64.5. Пример: профиль NPC-бойца

```
{
  "archetype": "wrestler",
  "experience": 5, "striking": 2, "grappling": 7, "defense": 4,
  "cardio": 5, "courage": 6, "aggression": 5,
  "sobriety": "tipsy",
  "background": ["school_wrestling", "army"],
  "random_skills": ["tkd_roundhouse_basic"],
  "personality": "aggressor",
  "action_weights": { "sloppy_punch": 0.35, "takedown": 0.45, "clinch": 0.15, "panic": 0.05 },
  "technique_quality": 0.35,
  "mistakes": ["drops_hands", "telegraphs", "leans_forward"],
  "clothing": { "top": "suit_jacket", "shoes": "dress_shoes" },
  "items": ["umbrella"]
}
```


### 64.6. Пример: паспорт района

```
{
  "id": "hongdae",
  "lut": "luts/hongdae_night.cube",
  "facade_kits": ["shophouse_hongdae", "club_block"],
  "sign_themes": ["club", "cafe", "street_food", "streetwear"],
  "npc_mix": {
    "day":   { "students": 0.4, "tourists": 0.2, "workers": 0.2, "locals": 0.2 },
    "night": { "partygoers": 0.5, "students": 0.2, "tourists": 0.2, "couriers": 0.1 }
  },
  "fashion_weights": { "street": 0.4, "y2k": 0.2, "hiphop": 0.15, "minimal": 0.15, "other": 0.1 },
  "ambience": "audio/amb/hongdae",
  "events": ["busker", "dance_battle", "drunk_group", "fight_challenge"],
  "gang": "blue_dragons",
  "respect_start": 20
}
```


### 64.7. Пример: сюжетная миссия на Lua

```
mission "m001_on_air" {
  title = "mission.m001.title",
  start = { district = "hongdae", time = { from = 22, to = 2 }, after = "p03_last_bus" },
  steps = {
    step "walk"   { goal = "goal.m001.walk", trigger = area("hongdae_street", 30) },
    step "ari"    { cutscene = "cs_m001_ari", goal = "goal.m001.protect" },
    step "wave1"  { fight = { group = "drunks_3", teach = { "lmb", "space" } } },
    step "wave2"  { fight = { group = "drunks_4", teach = { "rmb_counter" },
                              slowmo_hint = true } },
    step "bull"   { fight = { boss = "bull", scripted_loss_after = 40 } },
    step "meme"   { cutscene = "cs_m001_meme" },
    step "store"  { cutscene = "cs_m001_minjae", give = { contact = "minjae" } },
  },
  medals = { silver = counter("counters", ">=", 5), gold = no_damage("wave1", "wave2") },
  rewards = { style = "street", xp = { fighter = 200 } },
  fail = { leave_area = "restart_step" },
}
```

Синтаксис — пример; Claude проектирует API скриптов на этапе G11 и документирует его в docs/scripting.md.


### 64.8. Пример: строка локализации

```
key,ru,ko,en
goal.m001.protect,Защитите певицу,가수를 지켜라,Protect the singer
bark.youth.fight_seen.03,Офигеть! Снимай-снимай!,대박! 찍어 찍어!,No way! Film it!
```

Хангыль в файлах локализации хранится в UTF-8; шрифты интерфейса поддерживают все три языка.


### 64.9. Общее определение готовности (Definition of Done)

| Проверка | Требование |
|---|---|
| Сборка | собирается одной командой, без предупреждений, на чистой машине |
| Тесты | юнит-тесты, сценарии и скриншот-тесты зелёные; валидация Vulkan без ошибок |
| Производительность | бенчмарк-маршрут в бюджете на «Средне» (RTX 2050); память в бюджете |
| Мелочи | список мелочей этапа выполнен (PROGRESS.md) |
| Ощущение | владелец поиграл и подтвердил; замечания закрыты или занесены в задачи |
| Юридически | у каждого нового ассета есть запись в CREDITS.md и подтверждение лицензии; бренды — вымышленные |
| Данные | все числа в данных, а не в коде; редактор позволяет их менять |
| Локализация | все новые строки — ключи в ru/ko/en (ko и en могут быть временными) |
| Документация | ARCHITECTURE.md и docs/design обновлены, если изменились решения |
| Сохранения | новые данные сохраняются и загружаются; старые сохранения не ломаются |


### 64.10. Словарь

| Термин | Значение |
|---|---|
| Hitstop | микропауза анимаций при попадании — даёт ощущение веса удара |
| Хитбокс / хёртбокс | зона, которая бьёт / зона, по которой бьют |
| Окно отмены | кадры, в которые можно перейти в следующий приём |
| Преимущество (кадры) | кто начнёт действовать первым после блока или попадания |
| Активный ragdoll | физическое тело, которое моторами пытается следовать анимации и держать равновесие |
| Motion matching | выбор кусочка анимации из большой базы под текущее движение |
| Motion warping | подгонка анимации удара или броска под реальное положение цели |
| LOD / HLOD | уровни детализации объекта / склеенные упрощённые группы дальних объектов |
| Clustered forward+ | способ рендера сотен источников света с разбиением экрана на кластеры |
| FSR | апскейлер: рендер в меньшем разрешении и умное увеличение (открытая лицензия MIT) |
| LUT | таблица цветокоррекции, задающая «цвет» района, времени и погоды |
| XPBD | метод симуляции ткани, верёвок и волос |
| Utility AI | ИИ, который оценивает «полезность» каждого действия и выбирает лучшее |
| Токены атаки | ограничение числа врагов, атакующих одновременно |
| Нунчи | корейское понятие: умение «читать атмосферу» и людей |
| Хан / чон | накопленная горечь несправедливости / тёплая привязанность между людьми |
| Госивон, 옥탑방 | крошечная комната в общежитии / комната на крыше дома |
| Пхочжанмача | уличная палатка-закусочная |
| Хведжик | обязательный ужин с коллегами после работы |
| CC0, CC-BY, OFL, ODbL | лицензии: общественное достояние; с указанием автора; для шрифтов; для баз данных (карта) |
