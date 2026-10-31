#pragma once
// Связка с Blender (E3): FBX и прочие форматы переводятся в GLB фоновым запуском Blender со скриптом han/tools/blender/.
#include <filesystem>
#include <string>

namespace han::tools {

// Ищет blender.exe: переменная HAN_BLENDER, затем "Program Files/Blender Foundation/Blender *" (берётся новейшая версия).
// Пустой путь — не найден.
std::filesystem::path findBlender();
// Ищет скрипт конвертации: в каталоге исходников (на момент сборки), затем относительно текущей папки.
std::filesystem::path findBlenderScript(const std::string& name);

// Запускает Blender без окна: скрипт открывает input и сохраняет GLB в output.
bool convertWithBlender(const std::filesystem::path& blender, const std::filesystem::path& script, const std::filesystem::path& input,
                        const std::filesystem::path& output, std::string& error);

}  // namespace han::tools
// Пример: auto b = findBlender(); convertWithBlender(b, findBlenderScript("fbx_to_glb.py"), "tower.fbx", "tower.glb", err);
