#include "han/world/far_skyline.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace han::world {
namespace {
// Четырёхугольник/треугольник: порядок индексов подбирается по заданной внешней нормали.
void pushTriangle(GeoMesh& m, const float a[3], const float b[3], const float c[3], const float uv[3][2], const float n[3], const float color[3]) {
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    const float* p[3] = {a, b, c};
    for (int k = 0; k < 3; ++k) {
        GeoVertex v{};
        for (int i = 0; i < 3; ++i) {
            v.position[i] = p[k][i];
            v.normal[i] = n[i];
            v.color[i] = color[i];
        }
        v.uv[0] = uv[k][0];
        v.uv[1] = uv[k][1];
        m.vertices.push_back(v);
    }
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float cross[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    const bool forward = cross[0] * n[0] + cross[1] * n[1] + cross[2] * n[2] > 0.f;
    if (forward) m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
    else m.indices.insert(m.indices.end(), {base, base + 2, base + 1});
}
}  // namespace

FarSkylineTiles buildFarSkyline(const OsmData& data, const GeoProjection& projection, const CityBuildSettings& settings, const Heightmap* terrain,
                                const FarSkylineSettings& skyline, FarSkylineStats* stats) {
    FarSkylineTiles tiles;
    FarSkylineStats local;
    const auto groundAt = [&](float x, float z) { return terrain && terrain->valid() ? terrain->heightAt(x, z) : 0.f; };
    for (const OsmWay& way : data.ways) {
        const std::string_view building = way.tag("building");
        if (building.empty() || building == "no" || !way.closed()) continue;
        std::vector<Vec2> pts;
        for (const std::int64_t id : way.nodes) {
            const auto it = data.nodes.find(id);
            if (it == data.nodes.end()) continue;
            Vec2 p;
            projection.toLocal(it->second.lat, it->second.lon, p.x, p.y);
            pts.push_back(p);
        }
        if (pts.size() >= 2 && pts.front().x == pts.back().x && pts.front().y == pts.back().y) pts.pop_back();
        if (pts.size() < 3) continue;
        const float area = std::fabs(signedArea(pts));
        const float height = buildingHeight(way, settings, area);
        if (height < skyline.minHeight) {
            ++local.skippedLow;
            continue;
        }
        if (area < skyline.minArea) {
            ++local.skippedSmall;
            continue;
        }
        float cx = 0.f, cz = 0.f, lowest = 1e9f, highest = -1e9f;
        for (const Vec2& p : pts) {
            cx += p.x;
            cz += p.y;
            const float g = groundAt(p.x, p.y);
            lowest = std::min(lowest, g);
            highest = std::max(highest, g);
        }
        cx /= static_cast<float>(pts.size());
        cz /= static_cast<float>(pts.size());
        const float bottom = lowest - 0.5f, top = highest + height;
        const std::pair<int, int> key{static_cast<int>(std::floor(cx / skyline.tileSize)), static_cast<int>(std::floor(cz / skyline.tileSize))};
        GeoMesh& mesh = tiles[key];
        const float gray[3] = {0.78f, 0.78f, 0.80f};
        const bool ccw = signedArea(pts) > 0.f;
        const auto wallFirst = static_cast<std::uint32_t>(mesh.indices.size());
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const Vec2 a = pts[i], b = pts[(i + 1) % pts.size()];
            const float dx = b.x - a.x, dz = b.y - a.y, length = std::hypot(dx, dz);
            if (length < 0.1f) continue;
            // Внешняя нормаль: для обхода против часовой (в осях x, y карты) — вправо от направления ребра.
            float nx = dz / length, nz = -dx / length;
            if (!ccw) {
                nx = -nx;
                nz = -nz;
            }
            const float n[3] = {nx, 0.f, nz};
            const float p0[3] = {a.x, bottom, a.y}, p1[3] = {b.x, bottom, b.y}, p2[3] = {b.x, top, b.y}, p3[3] = {a.x, top, a.y};
            const float u1 = length / skyline.tile, v1 = (top - bottom) / skyline.tile;
            const float uvA[3][2] = {{0, 0}, {u1, 0}, {u1, v1}}, uvB[3][2] = {{0, 0}, {u1, v1}, {0, v1}};
            pushTriangle(mesh, p0, p1, p2, uvA, n, gray);
            pushTriangle(mesh, p0, p2, p3, uvB, n, gray);
        }
        mesh.tag(GeoMaterial::WallConcrete, wallFirst);
        const auto roofFirst = static_cast<std::uint32_t>(mesh.indices.size());
        const auto tri = triangulate(pts);
        const float up[3] = {0.f, 1.f, 0.f}, roofColor[3] = {0.45f, 0.45f, 0.47f};
        for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
            const Vec2 a = pts[tri[t]], b = pts[tri[t + 1]], c = pts[tri[t + 2]];
            const float pa[3] = {a.x, top, a.y}, pb[3] = {b.x, top, b.y}, pc[3] = {c.x, top, c.y};
            const float uv[3][2] = {{a.x / skyline.tile, a.y / skyline.tile}, {b.x / skyline.tile, b.y / skyline.tile}, {c.x / skyline.tile, c.y / skyline.tile}};
            pushTriangle(mesh, pa, pb, pc, uv, up, roofColor);
        }
        mesh.tag(GeoMaterial::Roof, roofFirst);
        ++local.buildings;
    }
    for (auto& [key, mesh] : tiles) {
        mesh.groupByMaterial();
        local.triangles += static_cast<int>(mesh.indices.size() / 3);
    }
    local.tiles = static_cast<int>(tiles.size());
    if (stats) *stats = local;
    return tiles;
}

}  // namespace han::world
