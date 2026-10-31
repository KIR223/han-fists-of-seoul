#pragma once
// Загрузка сваренного ассета (E3): .hmesh вместе с текстурами .ktx2, лежащими рядом, и горячая перезагрузка.
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/mesh_io.h"
#include "han/render/renderer.h"

namespace han::render {

struct LoadedMesh {
    MeshId id = 0;
    std::filesystem::path path;                  // файл .hmesh
    std::vector<std::filesystem::path> files;    // все файлы ассета (меш и текстуры) — их стоит отдать AssetWatcher
    std::vector<TextureId> textures;             // загруженные текстуры, по порядку MeshData::textures
    MeshBounds bounds;
};

// Читает меш и его текстуры и загружает в GPU. Текстура, которую не удалось прочесть, заменяется белой с предупреждением в stderr —
// меш всё равно загружается. false — не удалось прочесть сам меш или загрузить его в GPU.
bool loadMeshAsset(Renderer& renderer, const std::filesystem::path& path, LoadedMesh& out, std::string& error);

// Перечитывает ассет и заменяет данные под прежним MeshId; старые текстуры освобождаются. При ошибке прежний ассет остаётся.
bool reloadMeshAsset(Renderer& renderer, LoadedMesh& asset, std::string& error);

}  // namespace han::render
// Пример: LoadedMesh lamp; if (!loadMeshAsset(*renderer, "data/props/lamp.hmesh", lamp, err)) log(err); items.push_back({lamp.id, model});
