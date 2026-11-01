// han_cooker v1 (E3): превращает исходную модель в готовый меш .hmesh.
//   han_cooker [--no-optimize] <вход.gltf|glb|fbx> <выход.hmesh>
//   han_cooker --info <файл.hmesh>
//   han_cooker --texture [--linear] <картинка.jpg|png|tga|bmp> <выход.ktx2>   (текстура с цепочкой мипов; --linear — нормали и шероховатость)
// FBX переводится в GLB через Blender (han/tools/blender/fbx_to_glb.py). Коды выхода: 0 — готово, 1 — ошибка, 2 — неверный запуск.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "han/render/ktx2_io.h"
#include "han/render/mesh_io.h"
#include "han/tools/blender.h"
#include "han/tools/cook.h"
#include "han/tools/gltf_import.h"
#include "han/tools/image_decode.h"
#include "han/tools/mesh_tools.h"

namespace fs = std::filesystem;
using namespace han;

namespace {

void printStats(const render::MeshData& mesh, std::size_t bytes) {
    const render::MeshBounds b = render::computeBounds(mesh);
    std::printf("вершин: %zu, треугольников: %zu, частей: %zu, текстур: %zu, размер: %zu байт\n", mesh.vertices.size(),
                mesh.indices.size() / 3, render::effectiveSubmeshes(mesh).size(), mesh.textures.size(), bytes);
    std::printf("бокс: (%.3f %.3f %.3f) .. (%.3f %.3f %.3f), размеры: %.3f x %.3f x %.3f м\n", b.min.x, b.min.y, b.min.z, b.max.x,
                b.max.y, b.max.z, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z);
}

// Имя файла текстуры по содержимому: размеры и 64-битный хэш FNV-1a пикселей (не криптографический, нужен для дедупликации).
std::string textureFileName(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint8_t byte : rgba) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "tex_%ux%u_%016llx.ktx2", width, height, static_cast<unsigned long long>(hash));
    return buffer;
}

int info(const fs::path& path) {
    render::MeshData mesh;
    std::string error;
    if (!render::loadMeshFile(path, mesh, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    printStats(mesh, static_cast<std::size_t>(fs::file_size(path)));
    return 0;
}

#ifdef HAN_WITH_STB
// Картинка → .ktx2 с полной цепочкой мипов. sRGB для альбедо, линейная для нормалей, шероховатости и AO.
int cookTexture(const fs::path& input, const fs::path& output, bool srgb) {
    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "не удалось открыть %s\n", input.string().c_str());
        return 1;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    tools::DecodedImage image;
    std::string error;
    if (!tools::decodeImage(bytes.data(), bytes.size(), image, error)) {
        std::fprintf(stderr, "%s: %s\n", input.string().c_str(), error.c_str());
        return 1;
    }
    render::TextureData texture = render::makeTexture(image.width, image.height, std::move(image.rgba), srgb);
    render::generateMips(texture);
    std::error_code ec;
    if (!output.parent_path().empty()) fs::create_directories(output.parent_path(), ec);
    if (!render::saveKtx2File(output, texture, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("текстура: %s (%ux%u, %zu мипов, %s)\n", output.string().c_str(), texture.width, texture.height, texture.mips.size(), srgb ? "sRGB" : "линейная");
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    bool optimize = true, textureMode = false, linear = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-optimize")) optimize = false;
        else if (!std::strcmp(argv[i], "--texture")) textureMode = true;
        else if (!std::strcmp(argv[i], "--linear")) linear = true;
        else args.emplace_back(argv[i]);
    }
    if (textureMode) {
#ifdef HAN_WITH_STB
        if (args.size() != 2) {
            std::fprintf(stderr, "использование: han_cooker --texture [--linear] <картинка> <выход.ktx2>\n");
            return 2;
        }
        return cookTexture(args[0], args[1], !linear);
#else
        std::fprintf(stderr, "han_cooker собран без stb_image (включите vcpkg-функцию assets)\n");
        return 1;
#endif
    }
    if (args.size() == 2 && args[0] == "--info") return info(args[1]);
    if (args.size() != 2) {
        std::fprintf(stderr, "использование: han_cooker [--no-optimize] <вход.gltf|glb|fbx> <выход.hmesh>\n"
                             "              han_cooker --info <файл.hmesh>\n");
        return 2;
    }

    const fs::path input = args[0], output = args[1];
    std::string error;
    render::MeshData mesh;
    fs::path tempGlb;

    const tools::SourceKind kind = tools::classifySource(args[0]);
    fs::path gltfPath = input;
    if (kind == tools::SourceKind::Fbx) {
        tempGlb = output;
        tempGlb.replace_extension(".tmp.glb");
        if (!tools::convertWithBlender(tools::findBlender(), tools::findBlenderScript("fbx_to_glb.py"), input, tempGlb, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        gltfPath = tempGlb;
    } else if (kind != tools::SourceKind::Gltf) {
        std::fprintf(stderr, "формат не поддерживается: %s (ожидается .gltf, .glb или .fbx)\n", args[0].c_str());
        return 1;
    }

#ifdef HAN_WITH_GLTF
    tools::ImportResult imported;
    const bool ok = tools::importGltf(gltfPath, imported, error);
#else
    const bool ok = false;
    error = "han_cooker собран без fastgltf (включите vcpkg-функцию assets)";
#endif
    std::error_code ec;
    if (!tempGlb.empty()) fs::remove(tempGlb, ec);
    if (!ok) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

#ifdef HAN_WITH_GLTF
    for (const std::string& warning : imported.warnings) std::fprintf(stderr, "предупреждение: %s\n", warning.c_str());
    mesh = std::move(imported.mesh);
    if (!output.parent_path().empty()) fs::create_directories(output.parent_path(), ec);

    // Текстуры: "tex_<ширина>x<высота>_<хэш содержимого>.ktx2" рядом с мешем, с полной цепочкой мипов (sRGB: усреднение в
    // линейном свете). Имя зависит только от содержимого, поэтому LOD-ы одного ассета и разные ассеты делят один файл.
    for (std::size_t i = 0; i < imported.textures.size(); ++i) {
        const std::string name = textureFileName(imported.textures[i].width, imported.textures[i].height, imported.textures[i].rgba);
        const fs::path file = output.parent_path() / name;
        if (fs::exists(file, ec)) {
            std::printf("текстура: %s (уже есть)\n", name.c_str());
        } else {
            render::TextureData texture = render::makeTexture(imported.textures[i].width, imported.textures[i].height,
                                                              std::move(imported.textures[i].rgba), true);
            render::generateMips(texture);
            if (!render::saveKtx2File(file, texture, error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            std::printf("текстура: %s (%ux%u, %zu мипов)\n", name.c_str(), texture.width, texture.height, texture.mips.size());
        }
        mesh.textures.push_back(name);
    }
#endif

    if (optimize) tools::optimizeMesh(mesh);
    if (!render::saveMeshFile(output, mesh, error)) {  // меш пишется последним: по нему горячая перезагрузка видит готовый ассет
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("готово: %s\n", output.string().c_str());
    printStats(mesh, static_cast<std::size_t>(fs::file_size(output)));
    return 0;
}
