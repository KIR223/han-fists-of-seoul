#include "han/world/terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace han::world {

bool Heightmap::load(const std::string& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "нет файла " + file + " (python han/tools/pipeline/han_terrain_fetch.py)";
        return false;
    }
    char magic[4];
    std::int32_t w = 0, h = 0;
    float header[3];
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&w), 4);
    in.read(reinterpret_cast<char*>(&h), 4);
    in.read(reinterpret_cast<char*>(header), 12);
    if (!in || std::memcmp(magic, "HHT1", 4) != 0 || w < 2 || h < 2 || w > 20000 || h > 20000 || !(header[2] > 0.1f)) {
        error = file + ": это не файл рельефа HHT1";
        return false;
    }
    std::vector<float> data(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(float)));
    if (!in) {
        error = file + ": файл рельефа обрезан";
        return false;
    }
    *this = fromGrid(header[0], header[1], header[2], w, h, std::move(data));
    return true;
}

Heightmap Heightmap::fromGrid(float minX, float minZ, float cell, int width, int height, std::vector<float> heights) {
    Heightmap m;
    if (width < 2 || height < 2 || heights.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return m;
    m.minX_ = minX;
    m.minZ_ = minZ;
    m.cell_ = cell;
    m.width_ = width;
    m.height_ = height;
    m.data_ = std::move(heights);
    m.minH_ = *std::min_element(m.data_.begin(), m.data_.end());
    m.maxH_ = *std::max_element(m.data_.begin(), m.data_.end());
    return m;
}

float Heightmap::heightAt(float x, float z) const {
    if (!valid()) return 0.f;
    const float fx = std::clamp((x - minX_) / cell_, 0.f, static_cast<float>(width_ - 1) - 1e-3f);
    const float fz = std::clamp((z - minZ_) / cell_, 0.f, static_cast<float>(height_ - 1) - 1e-3f);
    const int i = static_cast<int>(fx), j = static_cast<int>(fz);
    const float tx = fx - static_cast<float>(i), tz = fz - static_cast<float>(j);
    const float a = sample(i, j), b = sample(i + 1, j), c = sample(i, j + 1), d = sample(i + 1, j + 1);
    return (a * (1.f - tx) + b * tx) * (1.f - tz) + (c * (1.f - tx) + d * tx) * tz;
}

void Heightmap::normalAt(float x, float z, float& nx, float& ny, float& nz) const {
    const float e = cell_;
    const float dx = heightAt(x + e, z) - heightAt(x - e, z), dz = heightAt(x, z + e) - heightAt(x, z - e);
    const float inv = 1.f / std::sqrt(dx * dx + dz * dz + 4.f * e * e);
    nx = -dx * inv;
    ny = 2.f * e * inv;
    nz = -dz * inv;
}

GeoMesh buildTerrainCell(const Heightmap& map, CellCoord cell, const TerrainSettings& s) {
    GeoMesh mesh;
    if (!map.valid()) return mesh;
    const float x0 = static_cast<float>(cell.x) * s.cellSize, z0 = static_cast<float>(cell.z) * s.cellSize;
    if (x0 + s.cellSize < map.minX() || x0 > map.maxX() || z0 + s.cellSize < map.minZ() || z0 > map.maxZ()) return mesh;
    const int n = std::max(1, static_cast<int>(std::round(s.cellSize / s.spacing)));
    const float step = s.cellSize / static_cast<float>(n);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float x = x0 + static_cast<float>(i) * step, z = z0 + static_cast<float>(j) * step;
            GeoVertex v{};
            v.position[0] = x;
            v.position[1] = map.heightAt(x, z) - s.sink;
            v.position[2] = z;
            map.normalAt(x, z, v.normal[0], v.normal[1], v.normal[2]);
            v.color[0] = v.color[1] = v.color[2] = 1.f;
            v.uv[0] = x / s.uvTile;
            v.uv[1] = z / s.uvTile;
            mesh.vertices.push_back(v);
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const auto a = static_cast<std::uint32_t>(j * (n + 1) + i), b = a + 1, c = a + static_cast<std::uint32_t>(n + 1), d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});  // против часовой, если смотреть сверху (ось z — на юг)
        }
    mesh.tag(GeoMaterial::Terrain, 0);
    return mesh;
}

}  // namespace han::world
