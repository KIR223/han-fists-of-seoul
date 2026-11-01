#!/usr/bin/env python3
"""Создаёт личные файлы разработчика (они не попадают в Git):
  CLAUDE.local.md              — роль и подключение файлов роли и прогресса
  .claude/settings.local.json  — запрет правки чужих зон для Claude Code

Использование:  python tools/make_personal.py --role B
Роли: A (движок, мир и город), B (бой, сюжет и игра), OWNER (владелец), A+OWNER (владелец, он же A).
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TITLES = {
    "A": "разработчик A: движок, графика, физика мира, транспорт, генератор карты (город и область), жизнь города (толпа, трафик, полиция, банды), материалы и текстуры",
    "B": "разработчик B: бой, анимация, персонаж, ИИ бойцов, обучение и стили + сюжет, миссии, катсцены, озвучка, интерфейс, экономика, телефон, стрим, локализация",
    "OWNER": "владелец проекта: дизайн-документ, правила, юридическая проверка",
    "A+OWNER": "разработчик A и владелец проекта: движок, мир, город и область, транспорт, сборка + дизайн-документ, правила, юридическая проверка",
    "SOLO": "владелец проекта в соло-режиме (временно, пока нет второго разработчика): зоны A и B и владельца",
}


def deny_rules(role):
    roles = set(role.split("+"))
    if "SOLO" in {r.upper() for r in roles}:
        roles = {"A", "B", "OWNER"}
    with open(os.path.join(ROOT, "tools", "zones.json"), encoding="utf-8") as f:
        zones = json.load(f)["zones"]
    rules = []
    mine = {r.lower() for r in roles}
    if "owner" not in mine:  # запрет push в чужие ветки (подкрепляет хук)
        for p in ("a", "b"):
            if p not in mine:
                rules += [f"Bash(git push origin {p}/*)", f"Bash(git push origin HEAD:{p}/*)", f"Bash(git push origin HEAD:refs/heads/{p}/*)"]
    for z in zones:
        if z["owner"] in roles or z["owner"] == "shared":
            continue
        p = z["path"]
        rules.append(f"Edit(/{p}**)" if p.endswith("/") else f"Edit(/{p})")
    return rules


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--role", required=True, choices=list(TITLES))
    ap.add_argument("--out", default=ROOT)
    a = ap.parse_args()
    role = a.role
    out = a.out
    os.makedirs(os.path.join(out, ".claude"), exist_ok=True)

    lines = [f"РОЛЬ: {role}", f"Я — {TITLES[role]}."]
    main = role.split("+")[0]
    if main == "SOLO":
        lines += [
            "Соло-режим владельца (временно): зоны A и B открыты, потому что второго разработчика ещё нет.",
            "@docs/team/ROLE_A.md", "@docs/team/ROLE_B.md",
            "Когда B подключится: python tools/make_personal.py --role A+OWNER (и B получит свой файл у себя).",
        ]
    elif main != "OWNER":
        lines += [
            f"@docs/team/ROLE_{main}.md",
            f"@docs/progress/{main}.md",
            "Правлю только свои папки (список в ROLE_%s.md). Чужое — только Issue для владельца зоны." % main,
            "Отправлять на GitHub могу только свои ветки: %s/… (в main — только через Pull Request)." % main.lower(),
        ]
        if "OWNER" in role:
            lines += ["Я также владелец: общие файлы (CLAUDE.md, docs/design, docs/team, tools) правлю в отдельных ветках o/…, работу A — в ветках a/…"]
    else:
        lines += ["Правлю общие файлы (CLAUDE.md, docs/design, docs/team, tools). Зоны разработчиков — только через их Issues и PR."]
    with open(os.path.join(out, "CLAUDE.local.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    settings = {"permissions": {"deny": deny_rules(role)}}
    with open(os.path.join(out, ".claude", "settings.local.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(settings, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(f"Готово: роль {role}. Файлы CLAUDE.local.md и .claude/settings.local.json созданы в {out}")


if __name__ == "__main__":
    sys.exit(main())
