# Что установить перед работой (каждому, один раз)

Система: Windows 10/11 (64 бита), свежий драйвер видеокарты (Vulkan 1.3). 8+ ГБ ОЗУ для работы с Claude Code достаточно (требование Claude Code — 4 ГБ+),
для сборки движка лучше 16 ГБ.

| Что | Зачем | Где взять | Кому |
|---|---|---|---|
| **Git for Windows** (с Git LFS) | код; Git Bash нужен Claude Code для команд | https://git-scm.com/downloads/win | все |
| **Claude Code** | работа с ИИ | PowerShell: `irm https://claude.ai/install.ps1 \| iex`; проверка `claude --version`, `claude doctor`; вход — при первом запуске `claude` | все |
| Подписка Claude (Pro, Max, Team или Console) | Claude Code не входит в бесплатный план | claude.ai | у каждого своя |
| **Python 3.11+** | инструменты команды (`tools/`, валидаторы) | https://www.python.org/downloads/ (галочка «Add to PATH») | все |
| **GitHub CLI (`gh`)** | Claude читает задачи и открывает PR | https://cli.github.com/ → `gh auth login` | все |
| **Visual Studio 2022 Community** с компонентом «Разработка классических приложений на C++» | компилятор C++20, отладчик, CMake и Ninja внутри | https://visualstudio.microsoft.com/ (условия бесплатного использования Community проверь по лицензии) | A, B (для сборки) |
| **CMake** (свежий) | сборка | https://cmake.org/download/ | A, B |
| **vcpkg** | библиотеки движка | `git clone https://github.com/microsoft/vcpkg`, `bootstrap-vcpkg.bat`, переменная `VCPKG_ROOT` | A (остальные — после E0) |
| **Vulkan SDK (LunarG)** | заголовки, слои валидации, компилятор шейдеров | https://vulkan.lunarg.com/ | A |
| **Blender** | конвертация и доводка моделей | https://www.blender.org/download/ | A, B |
| **RenderDoc** | отладка кадра | https://renderdoc.org/ | A |
| **Lua 5.4** (консольный) | прогон миссий без движка | https://luabinaries.sourceforge.net/ или `winget`-пакет Lua; пригодится B | B |
| `pip install jsonschema` | проверка данных по схемам | pip | B (и все, кто правит данные) |

По желанию: VS Code (лёгкий редактор), Krita/GIMP (текстуры), Audacity (звук), NVIDIA Nsight Graphics (профилирование видеокарты).
Библиотеки движка (Tracy, SDL3, Jolt и т. д.) руками не ставить: их подтянет vcpkg.

## После установки
```text
git clone <ссылка на репозиторий>
cd han-fists-of-seoul
python tools/join.py                          (роль по логину GitHub; или make_personal.py --role B)
python tools/setup_local.py                    (хуки Git, LFS, проверка программ; повторить с --check)
claude                                         (первый вход через браузер) → напиши: Начинай
```
Если после `setup_local.py` есть строки ✖ — исправь их по подсказкам и запусти снова.

## Частые проблемы
- `claude` не найден после установки → открой новое окно PowerShell; затем `claude doctor`.
- Claude Code не видит Bash → поставь Git for Windows; при необходимости путь к `bash.exe` задаётся в настройках (`CLAUDE_CODE_GIT_BASH_PATH`).
- Хук не срабатывает → `git config core.hooksPath` должен вернуть `.githooks`.
- Python не найден хуком → в PATH должен быть `python` или `py`.
