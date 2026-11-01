#pragma once
// Каркас han_cooker (E3): определение вида исходного файла. Сама конвертация (glTF, FBX через Blender, KTX2) — этап E3.
#include <string_view>

namespace han::tools {

enum class SourceKind { Unknown, Gltf, Fbx, Image, Audio };

SourceKind classifySource(std::string_view path);
const char* sourceKindName(SourceKind k);

}  // namespace han::tools
// Пример: if (classifySource("lamp.glb") == SourceKind::Gltf) cookGltf(...);
