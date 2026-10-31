#!/usr/bin/env python3
"""Проверка сообщения коммита: первая строка обязательно по-русски.
Вызывается хуком .githooks/commit-msg:  python tools/commit_msg_check.py <файл с сообщением>
Используется также ship.py и CI (проверка заголовка Pull Request).
"""
import re
import sys

SKIP = ("Merge ", "Revert ", "fixup!", "squash!", "amend!")


def is_russian(subject):
    s = subject.strip()
    if not s or s.startswith(SKIP):
        return True
    cyr = len(re.findall(r"[А-Яа-яЁё]", s))
    lat = len(re.findall(r"[A-Za-z]", s))
    return cyr >= 3 and cyr >= lat * 0.5  # допускаем английские термины (CMake, Jolt, PR), но не целиком английский текст


MSG = ("✖ Сообщение коммита должно быть на русском языке.\n"
       "  Пример: «Бой: добавлена кадровая машина ударов (G1)».\n"
       "  Получено: «{s}»")

if __name__ == "__main__":
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read() if len(sys.argv) > 1 else sys.stdin.read()
    lines = [l for l in text.splitlines() if l.strip() and not l.startswith("#")]
    subject = lines[0] if lines else ""
    if not is_russian(subject):
        print(MSG.format(s=subject), file=sys.stderr)
        sys.exit(1)
