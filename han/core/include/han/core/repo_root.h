#pragma once
// Демо и утилиты читают данные по относительным путям (data/..., assets_src/...). Чтобы exe запускался двойным щелчком из любой папки,
// enterRepoRoot() ищет вверх от exe папку репозитория (где лежат CLAUDE.md и data) и делает её текущей.
#include <filesystem>
#include <system_error>

namespace han::core {
// Возвращает true, если корень найден (текущая папка уже корень — тоже true). exePath — argv[0].
inline bool enterRepoRoot(const char* exePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto isRoot = [&](const fs::path& p) { return fs::exists(p / "CLAUDE.md", ec) && fs::is_directory(p / "data", ec); };
    if (isRoot(fs::current_path(ec))) return true;
    fs::path dir = exePath ? fs::weakly_canonical(fs::path(exePath), ec).parent_path() : fs::path();
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        if (isRoot(dir)) {
            fs::current_path(dir, ec);
            return !ec;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return false;
}
}  // namespace han::core
// Пример: int main(int, char** argv) { han::core::enterRepoRoot(argv[0]); ... }
