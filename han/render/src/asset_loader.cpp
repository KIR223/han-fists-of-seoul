#include "han/render/asset_loader.h"

#include <cstdio>

#include "han/render/ktx2_io.h"
#include "han/render/mesh_io.h"

namespace han::render {
namespace {

// Читает меш и текстуры в память, без обращения к GPU.
struct Prepared {
    MeshData mesh;
    std::vector<std::filesystem::path> files;
    std::vector<TextureData> textures;       // по порядку mesh.textures; пустая (width == 0) — не удалось прочесть
};

bool prepare(const std::filesystem::path& path, Prepared& out, std::string& error) {
    if (!loadMeshFile(path, out.mesh, error)) return false;
    out.files.push_back(path);
    const std::filesystem::path dir = path.parent_path();
    for (const std::string& name : out.mesh.textures) {
        const std::filesystem::path file = dir / name;
        out.files.push_back(file);
        TextureData texture;
        std::string textureError;
        if (loadKtx2File(file, texture, textureError)) {
            out.textures.push_back(std::move(texture));
        } else {
            std::fprintf(stderr, "текстура %s не загружена: %s (будет белой)\n", file.string().c_str(), textureError.c_str());
            out.textures.emplace_back();
        }
    }
    return true;
}

std::vector<TextureId> uploadAll(Renderer& renderer, const std::vector<TextureData>& textures) {
    std::vector<TextureId> ids;
    for (const TextureData& t : textures) ids.push_back(t.width ? renderer.uploadTexture(t) : 0);
    return ids;
}

}  // namespace

bool loadMeshAsset(Renderer& renderer, const std::filesystem::path& path, LoadedMesh& out, std::string& error) {
    Prepared p;
    if (!prepare(path, p, error)) return false;
    std::vector<TextureId> ids = uploadAll(renderer, p.textures);
    const MeshId id = renderer.uploadMesh(p.mesh, ids);
    if (!id) {
        for (const TextureId t : ids) renderer.releaseTexture(t);
        error = "не удалось загрузить меш в GPU: " + path.string();
        return false;
    }
    out.id = id;
    out.path = path;
    out.files = std::move(p.files);
    out.textures = std::move(ids);
    out.bounds = computeBounds(p.mesh);
    return true;
}

bool reloadMeshAsset(Renderer& renderer, LoadedMesh& asset, std::string& error) {
    Prepared p;
    if (!prepare(asset.path, p, error)) return false;
    std::vector<TextureId> ids = uploadAll(renderer, p.textures);
    if (!renderer.replaceMesh(asset.id, p.mesh, ids)) {
        for (const TextureId t : ids) renderer.releaseTexture(t);
        error = "не удалось заменить меш в GPU: " + asset.path.string();
        return false;
    }
    for (const TextureId old : asset.textures) renderer.releaseTexture(old);
    asset.textures = std::move(ids);
    asset.files = std::move(p.files);
    asset.bounds = computeBounds(p.mesh);
    return true;
}

}  // namespace han::render
