#pragma once
// Общее для демо города (city_demo, crowd_demo): текстуры материалов из assets_bin/cooked (han_fetch) и перевод GeoMesh в MeshData.
// Нет текстур (ассеты не скачаны) — город рисуется плоскими цветами материалов, как раньше.
#include <algorithm>
#include <array>
#include "han/render/texture.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/ktx2_io.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#include "han/render/sign_atlas.h"
#include "han/ui/localization.h"
#include "han/world/geometry.h"

namespace han::render::citytex {
struct Slot {
    world::GeoMaterial material;
    const char* folder;     // assets_bin/cooked/<folder>/diff.ktx2; nullptr — текстура строится кодом
    float fallback[3];      // цвет, если текстуры нет
    float roughness;
};

inline const std::array<Slot, 8>& slots() {
    static const std::array<Slot, 8> s = {{
        {world::GeoMaterial::Road, "tx_road_asphalt", {0.22f, 0.22f, 0.23f}, 0.9f},
        {world::GeoMaterial::Park, "tx_ground_grass", {0.30f, 0.46f, 0.26f}, 0.95f},
        {world::GeoMaterial::WallPlaster, "tx_wall_plaster_beige", {0.72f, 0.69f, 0.64f}, 0.85f},
        {world::GeoMaterial::WallBrick, "tx_wall_brick_brown", {0.62f, 0.38f, 0.32f}, 0.85f},
        {world::GeoMaterial::WallConcrete, "tx_wall_concrete", {0.60f, 0.60f, 0.62f}, 0.8f},
        {world::GeoMaterial::Roof, "tx_roof_bitumen", {0.30f, 0.30f, 0.32f}, 0.9f},
        {world::GeoMaterial::Sidewalk, "tx_sidewalk_brick", {0.50f, 0.46f, 0.42f}, 0.9f},
        {world::GeoMaterial::Storefront, nullptr, {0.35f, 0.30f, 0.28f}, 0.5f},  // процедурная (makeStorefront), файла нет
    }};
    return s;
}

// Окна на фасаде: плитка стены 6,4 м = два этажа по 3,2 м и две секции в ширину. Окно 1,6 x 1,3 м с рамой и стеклом, рисуется поверх
// альбедо стены (процедурно, наша собственная работа). Мипы пересчитываются после рисования.
inline void addWindows(TextureData& t) {
    const std::uint32_t cell = t.width / 2;  // секция 3,2 м
    const auto blend = [&](std::uint32_t x, std::uint32_t y, float r, float g, float b, float a) {
        std::uint8_t* p = &t.mips[0][(static_cast<std::size_t>(y) * t.width + x) * 4];
        p[0] = static_cast<std::uint8_t>(p[0] * (1.f - a) + r * a);
        p[1] = static_cast<std::uint8_t>(p[1] * (1.f - a) + g * a);
        p[2] = static_cast<std::uint8_t>(p[2] * (1.f - a) + b * a);
    };
    for (std::uint32_t cy = 0; cy < 2; ++cy)
        for (std::uint32_t cx = 0; cx < 2; ++cx) {
            const std::uint32_t x0 = cx * cell + cell / 4, x1 = cx * cell + cell * 3 / 4;                 // 1,6 м по центру секции
            const std::uint32_t y0 = cy * cell + cell * 15 / 100, y1 = cy * cell + cell * 56 / 100;       // 1,3 м, выше середины этажа
            const std::uint32_t frame = std::max<std::uint32_t>(3, cell / 64);
            for (std::uint32_t y = y0 - frame; y < y1 + frame; ++y)
                for (std::uint32_t x = x0 - frame; x < x1 + frame; ++x) {
                    const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
                    if (!inside) {
                        blend(x, y, 215.f, 212.f, 205.f, 0.9f);  // светлая рама
                        continue;
                    }
                    const float k = static_cast<float>(y - y0) / static_cast<float>(y1 - y0);  // стекло темнее внизу
                    const bool mullion = x == (x0 + x1) / 2 || (x + 1) == (x0 + x1) / 2;       // средний импост
                    if (mullion) blend(x, y, 215.f, 212.f, 205.f, 0.9f);
                    else {
                        blend(x, y, 52.f - 20.f * k, 74.f - 24.f * k, 96.f - 26.f * k, 0.95f);
                        t.mips[0][(static_cast<std::size_t>(y) * t.width + x) * 4 + 3] = 0;  // альфа 0 — стекло может светиться ночью
                    }
                }
            // Подоконник: тёмная полоска тени под окном
            for (std::uint32_t y = y1 + frame; y < y1 + frame + frame; ++y)
                for (std::uint32_t x = x0 - frame; x < x1 + frame; ++x) blend(x, y, 60.f, 55.f, 50.f, 0.5f);
        }
    t.mips.resize(1);
    generateMips(t);
}

// Вывески и витрины первого этажа: четыре секции по 4,8 м (1024 x 256 = 19,2 x 3,6 м). Вывески — вымышленные яркие цвета без текста
// (чужие бренды запрещены); стекло витрины и полоса вывески имеют альфу 0 — ночью светятся («неон»). Собственная работа проекта.
inline TextureData makeStorefront() {
    constexpr std::uint32_t W = 1024, H = 256, bay = 256;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(W) * H * 4);
    const float wallTone[4][3] = {{150, 140, 128}, {120, 118, 124}, {160, 128, 110}, {112, 126, 120}};
    const float sign[4][3] = {{210, 40, 50}, {30, 150, 170}, {235, 190, 40}, {150, 60, 170}};
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            const std::uint32_t b = x / bay, lx = x % bay;
            float r = wallTone[b][0], g = wallTone[b][1], bl = wallTone[b][2];
            std::uint8_t a = 255;
            if (y < 52) {  // вывеска: короб с яркой полосой
                if (lx > 10 && lx < bay - 10 && y > 8 && y < 44) {
                    r = sign[b][0];
                    g = sign[b][1];
                    bl = sign[b][2];
                    if (y > 16 && y < 36 && lx > 24 && lx < bay - 24) {  // «буквы»: светлая полоса с чёрточками
                        const bool glyph = (lx / 12) % 2 == 0;
                        r = glyph ? 250.f : sign[b][0];
                        g = glyph ? 245.f : sign[b][1];
                        bl = glyph ? 235.f : sign[b][2];
                        a = 0;
                    }
                } else {
                    r = 60;
                    g = 60;
                    bl = 64;
                }
            } else if (y < 70) {  // навес над витриной
                r = sign[b][0] * 0.55f;
                g = sign[b][1] * 0.55f;
                bl = sign[b][2] * 0.55f;
            } else if (lx > 22 && lx < bay - 22 && y > 84 && y < 238) {  // витрина: стекло с тёплой глубиной и импостом
                const float k = static_cast<float>(y - 84) / 154.f;
                const bool mullion = lx > bay / 2 - 3 && lx < bay / 2 + 3;
                if (mullion) {
                    r = 200;
                    g = 198;
                    bl = 190;
                } else {
                    r = 70 - 25 * k;
                    g = 82 - 25 * k;
                    bl = 92 - 25 * k;
                    a = 0;
                }
            } else if (y > 238) {  // цоколь
                r *= 0.6f;
                g *= 0.6f;
                bl *= 0.6f;
            }
            std::uint8_t* p = &px[(static_cast<std::size_t>(y) * W + x) * 4];
            p[0] = static_cast<std::uint8_t>(std::min(255.f, r));
            p[1] = static_cast<std::uint8_t>(std::min(255.f, g));
            p[2] = static_cast<std::uint8_t>(std::min(255.f, bl));
            p[3] = a;
        }
    TextureData t = makeTexture(W, H, std::move(px), true);
    generateMips(t);
    return t;
}

// Вывески вымышленных брендов (data/city/brands.json + SVG + шрифт): если всё есть, витрины строятся из них (16 секций), иначе — общие цветные.
inline const char* kBrandFont = "assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf";
inline bool brandAtlasAvailable() { return std::filesystem::exists("data/city/brands.json") && std::filesystem::exists("assets_src/brands"); }
inline float shopTileMeters() { return brandAtlasAvailable() ? 76.8f : 19.2f; }  // 16 секций по 4,8 м или 4 секции

inline TextureData makeBrandStorefront() {
    ui::Localization loc;
    std::string error;
    loc.loadTable("contracts/loc_requests/A.csv", error);
    loc.setLanguage("ko");  // вывески в Корее — хангылем
    std::vector<SignBrand> brands;
    if (!loadSignBrands("data/city/brands.json", "assets_src", [&](const std::string& key) { return loc.get(key); }, brands, error)) {
        std::fprintf(stderr, "бренды: %s\n", error.c_str());
        return makeStorefront();
    }
    return buildStorefrontAtlas(brands, std::filesystem::exists(kBrandFont) ? kBrandFont : "", 16);
}

// Загружает альбедо материалов в GPU. Возвращает id текстур по номерам slots() (0 — нет файла).
inline std::vector<TextureId> load(Renderer& renderer, bool windows = true, const std::filesystem::path& cooked = "assets_bin/cooked") {
    // Номера: 0..5 — альбедо, 6..11 — нормали, 12..17 — шероховатость (по порядку slots()).
    const std::size_t n = slots().size();
    std::vector<TextureId> ids(n * 3, 0);
    int loaded = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!slots()[i].folder) {
            TextureData generated = brandAtlasAvailable() ? makeBrandStorefront() : makeStorefront();
            ids[i] = renderer.uploadTexture(generated);
            loaded += ids[i] ? 1 : 0;
            continue;
        }
        for (std::size_t kind = 1; kind <= 2; ++kind) {
            TextureData map;
            std::string mapError;
            if (loadKtx2File(cooked / slots()[i].folder / (kind == 1 ? "nor.ktx2" : "rough.ktx2"), map, mapError)) ids[i + kind * n] = renderer.uploadTexture(map);
        }
        TextureData texture;
        std::string error;
        if (!loadKtx2File(cooked / slots()[i].folder / "diff.ktx2", texture, error)) continue;
        const world::GeoMaterial m = slots()[i].material;
        if (windows && texture.width >= 256 && (m == world::GeoMaterial::WallPlaster || m == world::GeoMaterial::WallBrick || m == world::GeoMaterial::WallConcrete)) addWindows(texture);
        ids[i] = renderer.uploadTexture(texture);
        loaded += ids[i] ? 1 : 0;
    }
    std::printf("текстуры города: %d из %zu (python han/tools/pipeline/han_fetch.py process <id> — если 0)\n", loaded, slots().size());
    return ids;
}

// Земля между дорогами и домами: мощёная плитка (тротуар) с картой нормалей и шероховатости; нет текстур — плоский цвет.
inline MeshId uploadGround(Renderer& renderer, float halfSize = 4000.f, const std::filesystem::path& cooked = "assets_bin/cooked") {
    TextureId ids[3] = {0, 0, 0};
    const char* files[3] = {"diff.ktx2", "nor.ktx2", "rough.ktx2"};
    for (int i = 0; i < 3; ++i) {
        TextureData t;
        std::string error;
        if (loadKtx2File(cooked / "tx_sidewalk_brick" / files[i], t, error)) ids[i] = renderer.uploadTexture(t);
    }
    const bool textured = ids[0] != 0;
    constexpr float kTile = 4.f;  // метров на плитку
    MeshData g = makePlane(halfSize, textured ? core::Vec3{0.8f, 0.8f, 0.8f} : core::Vec3{0.20f, 0.23f, 0.18f}, textured ? halfSize * 2.f / kTile : 1.f);
    g.textures = {"ground_albedo", "ground_normal", "ground_rough"};
    g.submeshes = {{0, static_cast<std::uint32_t>(g.indices.size()), textured ? 0 : -1, 0.0f, 0.9f, ids[1] ? 1 : -1, ids[2] ? 2 : -1}};
    return renderer.uploadMesh(g, ids);
}

inline int slotOf(world::GeoMaterial m) {
    if (m == world::GeoMaterial::Terrain) m = world::GeoMaterial::Sidewalk;  // земля — мощёная плитка, как раньше плоскость
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
    for (const std::string_view kind : {"albedo", "normal", "rough"})
        for (std::size_t s = 0; s < slots().size(); ++s) m.textures.push_back(std::string(kind) + "_" + std::to_string(s));
    if (g.ranges.empty()) {
        m.submeshes = {{0, static_cast<std::uint32_t>(m.indices.size()), -1, 0.0f, defaultRoughness}};
        return m;
    }
    for (const world::GeoRange& r : g.ranges) {
        const int slot = slotOf(r.material);
        std::int32_t texture = -1, normal = -1, rough = -1;
        float roughness = defaultRoughness;
        if (r.material == world::GeoMaterial::Water) roughness = 0.06f;  // вода: почти зеркало неба
        if (slot >= 0) {
            roughness = slots()[static_cast<std::size_t>(slot)].roughness;
            if (static_cast<std::size_t>(slot) < textureIds.size() && textureIds[static_cast<std::size_t>(slot)] != 0) {
                texture = slot;
                const std::size_t n = slots().size();
                if (textureIds.size() >= 3 * n) {
                    if (textureIds[static_cast<std::size_t>(slot) + n] != 0) normal = static_cast<std::int32_t>(static_cast<std::size_t>(slot) + n);
                    if (textureIds[static_cast<std::size_t>(slot) + 2 * n] != 0) rough = static_cast<std::int32_t>(static_cast<std::size_t>(slot) + 2 * n);
                }
            } else {  // нет текстуры: цвет-запас вместо белого
                for (std::uint32_t k = 0; k < r.indexCount; ++k) {
                    Vertex& v = m.vertices[m.indices[r.firstIndex + k]];
                    for (int c = 0; c < 3; ++c) v.color[c] = v.color[c] * slots()[static_cast<std::size_t>(slot)].fallback[c];
                }
            }
        }
        m.submeshes.push_back({r.firstIndex, r.indexCount, texture, 0.0f, roughness, normal, rough});
    }
    return m;
}
}  // namespace han::render::citytex
