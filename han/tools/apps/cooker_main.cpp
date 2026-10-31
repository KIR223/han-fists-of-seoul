// han_cooker v1 (E3): превращает исходную модель в готовый меш .hmesh.
//   han_cooker [--no-optimize] <вход.gltf|glb|fbx> <выход.hmesh>
//   han_cooker --info <файл.hmesh>
// FBX переводится в GLB через Blender (han/tools/blender/fbx_to_glb.py). Коды выхода: 0 — готово, 1 — ошибка, 2 — неверный запуск.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/mesh_io.h"
#include "han/tools/blender.h"
#include "han/tools/cook.h"
#include "han/tools/gltf_import.h"
#include "han/tools/mesh_tools.h"

namespace fs = std::filesystem;
using namespace han;

namespace {

void printStats(const render::MeshData& mesh, std::size_t bytes) {
    const render::MeshBounds b = render::computeBounds(mesh);
    std::printf("вершин: %zu, треугольников: %zu, размер: %zu байт\n", mesh.vertices.size(), mesh.indices.size() / 3, bytes);
    std::printf("бокс: (%.3f %.3f %.3f) .. (%.3f %.3f %.3f), размеры: %.3f x %.3f x %.3f м\n", b.min.x, b.min.y, b.min.z, b.max.x,
                b.max.y, b.max.z, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z);
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

}  // namespace

int main(int argc, char** argv) {
    bool optimize = true;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-optimize")) optimize = false;
        else args.emplace_back(argv[i]);
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
    const bool imported = tools::importGltf(gltfPath, mesh, error);
#else
    const bool imported = false;
    error = "han_cooker собран без fastgltf (включите vcpkg-функцию assets)";
#endif
    std::error_code ec;
    if (!tempGlb.empty()) fs::remove(tempGlb, ec);
    if (!imported) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    if (optimize) tools::optimizeMesh(mesh);
    if (!output.parent_path().empty()) fs::create_directories(output.parent_path(), ec);
    if (!render::saveMeshFile(output, mesh, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("готово: %s\n", output.string().c_str());
    printStats(mesh, static_cast<std::size_t>(fs::file_size(output)));
    return 0;
}
