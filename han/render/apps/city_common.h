#pragma once
// Общее для демо города (city_demo, crowd_demo): текстуры материалов из assets_bin/cooked (han_fetch) и перевод GeoMesh в MeshData.
// Нет текстур (ассеты не скачаны) — город рисуется плоскими цветами материалов, как раньше.
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/ktx2_io.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#include "han/world/geometry.h"

namespace han::render::citytex {
struct Slot {
    world::GeoMaterial material;
    const char* folder;     // assets_bin/cooked/<folder>/diff.ktx2
    float fallback[3];      // цвет, если текстуры нет
    float roughness;
};

inline const std::array<Slot, 6>& slots() {
    static const std::array<Slot, 6> s = {{
        {world::GeoMaterial::Road, "tx_road_asphalt", {0.22f, 0.22f, 0.23f}, 0.9f},
        {world::GeoMaterial::Park, "tx_ground_grass", {0.30f, 0.46f, 0.26f}, 0.95f},
        {world::GeoMaterial::WallPlaster, "tx_wall_plaster_beige", {0.72f, 0.69f, 0.64f}, 0.85f},
        {world::GeoMaterial::WallBrick, "tx_wall_brick_brown", {0.62f, 0.38f, 0.32f}, 0.85f},
        {world::GeoMaterial::WallConcrete, "tx_wall_concrete", {0.60f, 0.60f, 0.62f}, 0.8f},
        {world::GeoMaterial::Roof, "tx_roof_bitumen", {0.30f, 0.30f, 0.32f}, 0.9f},
    }};
    return s;
}

// Загружает альбедо материалов в GPU. Возвращает id текстур по номерам slots() (0 — нет файла).
inline std::vector<TextureId> load(Renderer& renderer, const std::filesystem::path& cooked = "assets_bin/cooked") {
    std::vector<TextureId> ids(slots().size(), 0);
    int loaded = 0;
    for (std::size_t i = 0; i < slots().size(); ++i) {
        TextureData texture;
        std::string error;
        if (!loadKtx2File(cooked / slots()[i].folder / "diff.ktx2", texture, error)) continue;
        ids[i] = renderer.uploadTexture(texture);
        loaded += ids[i] ? 1 : 0;
    }
    std::printf("текстуры города: %d из %zu (python han/tools/pipeline/han_fetch.py process <id> — если 0)\n", loaded, slots().size());
    return ids;
}

inline int slotOf(world::GeoMaterial m) {
    for (std::size_t i = 0; i < slots().size(); ++i)
        if (slots()[i].material == m) return static_cast<int>(i);
    return -1;
}

// GeoMesh → MeshData: по части на материал; материал с текстурой берёт её, без текстуры — плоский цвет-запас.
inline MeshData toMesh(const world::GeoMesh& g, const std::vector<TextureId>& textureIds, float defaultRoughness) {
    MeshData m;
    m.vertices.resize(g.vertices.size());
    for (std::size_t i = 0; i < g.vertices.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            m.vertices[i].position[k] = g.vertices[i].position[k];
            m.vertices[i].normal[k] = g.vertices[i].normal[k];
            m.vertices[i].color[k] = g.vertices[i].color[k];
        }
        m.vertices[i].uv[0] = g.vertices[i].uv[0];
        m.vertices[i].uv[1] = g.vertices[i].uv[1];
    }
    m.indices = g.indices;
    for (const std::string_view name : {"road", "park", "plaster", "brick", "concrete", "roof"}) m.textures.emplace_back(name);
    if (g.ranges.empty()) {
        m.submeshes = {{0, static_cast<std::uint32_t>(m.indices.size()), -1, 0.0f, defaultRoughness}};
        return m;
    }
    for (const world::GeoRange& r : g.ranges) {
        const int slot = slotOf(r.material);
        std::int32_t texture = -1;
        float roughness = defaultRoughness;
        if (slot >= 0) {
            roughness = slots()[static_cast<std::size_t>(slot)].roughness;
            if (static_cast<std::size_t>(slot) < textureIds.size() && textureIds[static_cast<std::size_t>(slot)] != 0) {
                texture = slot;
            } else {  // нет текстуры: цвет-запас вместо белого
                for (std::uint32_t k = 0; k < r.indexCount; ++k) {
                    Vertex& v = m.vertices[m.indices[r.firstIndex + k]];
                    for (int c = 0; c < 3; ++c) v.color[c] = v.color[c] * slots()[static_cast<std::size_t>(slot)].fallback[c];
                }
            }
        }
        m.submeshes.push_back({r.firstIndex, r.indexCount, texture, 0.0f, roughness});
    }
    return m;
}
}  // namespace han::render::citytex
