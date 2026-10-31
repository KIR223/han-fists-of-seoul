#include "han/tools/blender.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <vector>

#ifndef HAN_SOURCE_DIR
#define HAN_SOURCE_DIR "."
#endif

namespace han::tools {
namespace fs = std::filesystem;

namespace {
std::string envVar(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t length = 0;
    std::string value;
    if (_dupenv_s(&buffer, &length, name) == 0 && buffer) value = buffer;
    std::free(buffer);
    return value;
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}
}  // namespace

fs::path findBlender() {
    if (const std::string env = envVar("HAN_BLENDER"); !env.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(env, ec)) return env;
    }
    std::vector<fs::path> candidates;
    std::error_code ec;
    for (const char* root : {"C:/Program Files/Blender Foundation", "C:/Program Files (x86)/Blender Foundation"}) {
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            const fs::path exe = entry.path() / "blender.exe";
            if (fs::is_regular_file(exe, ec)) candidates.push_back(exe);
        }
    }
    if (candidates.empty()) return {};
    // Имена вида "Blender 5.2": лексикографически новейшая версия (для разных старших номеров достаточно для разработки).
    std::sort(candidates.begin(), candidates.end());
    return candidates.back();
}

fs::path findBlenderScript(const std::string& name) {
    std::error_code ec;
    for (const fs::path& base : {fs::path(HAN_SOURCE_DIR), fs::current_path(ec)}) {
        const fs::path script = base / "han" / "tools" / "blender" / name;
        if (fs::is_regular_file(script, ec)) return script;
    }
    return {};
}

bool convertWithBlender(const fs::path& blender, const fs::path& script, const fs::path& input, const fs::path& output,
                        std::string& error) {
    if (blender.empty()) {
        error = "Blender не найден: установите его или задайте путь в переменной HAN_BLENDER";
        return false;
    }
    if (script.empty()) {
        error = "не найден скрипт Blender (han/tools/blender/)";
        return false;
    }
    std::error_code ec;
    fs::remove(output, ec);  // чтобы не принять старый результат за новый
    // Внешние кавычки нужны cmd.exe, когда в команде несколько пар кавычек.
    const std::string command = "\"\"" + blender.string() + "\" --background --python \"" + script.string() + "\" -- \"" +
                                input.string() + "\" \"" + output.string() + "\"\"";
    const int code = std::system(command.c_str());
    if (code != 0 || !fs::is_regular_file(output, ec)) {
        error = "Blender завершился с кодом " + std::to_string(code) + ", результат не создан: " + output.string();
        return false;
    }
    return true;
}

}  // namespace han::tools
