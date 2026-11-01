#include "han/tools/cook.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace han::tools {

SourceKind classifySource(std::string_view path) {
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return SourceKind::Unknown;
    std::string ext(path.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "gltf" || ext == "glb") return SourceKind::Gltf;
    if (ext == "fbx") return SourceKind::Fbx;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "exr" || ext == "hdr") return SourceKind::Image;
    if (ext == "wav" || ext == "ogg" || ext == "flac") return SourceKind::Audio;
    return SourceKind::Unknown;
}

const char* sourceKindName(SourceKind k) {
    switch (k) {
        case SourceKind::Gltf: return "gltf";
        case SourceKind::Fbx: return "fbx";
        case SourceKind::Image: return "image";
        case SourceKind::Audio: return "audio";
        case SourceKind::Unknown: break;
    }
    return "unknown";
}

}  // namespace han::tools
