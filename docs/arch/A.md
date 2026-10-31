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
