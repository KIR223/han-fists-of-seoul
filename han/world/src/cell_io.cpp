#include "han/world/cell_io.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace han::world {
namespace {
constexpr float kOffsetXZ = 10.f, kOffsetY = 100.f;

template <class T>
void put(std::ofstream& out, T v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T>
bool get(std::ifstream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}
std::uint16_t quantize(float meters, float offset) {
    const float cm = (meters + offset) * 100.f;
    return static_cast<std::uint16_t>(std::clamp(std::lround(cm), 0l, 65535l));
}
float dequantize(std::uint16_t cm, float offset) { return static_cast<float>(cm) * 0.01f - offset; }

void writeMesh(std::ofstream& out, const GeoMesh& m, float x0, float z0) {
    put<std::uint32_t>(out, static_cast<std::uint32_t>(m.vertices.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(m.indices.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(m.ranges.size()));
    for (const GeoVertex& v : m.vertices) {
        put(out, quantize(v.position[0] - x0, kOffsetXZ));
        put(out, quantize(v.position[1], kOffsetY));
        put(out, quantize(v.position[2] - z0, kOffsetXZ));
        for (int k = 0; k < 3; ++k) put<std::int8_t>(out, static_cast<std::int8_t>(std::clamp(std::lround(v.normal[k] * 127.f), -127l, 127l)));
        for (int k = 0; k < 3; ++k) put<std::uint8_t>(out, static_cast<std::uint8_t>(std::clamp(std::lround(v.color[k] * 255.f), 0l, 255l)));
        put(out, v.uv[0]);
        put(out, v.uv[1]);
    }
    out.write(reinterpret_cast<const char*>(m.indices.data()), static_cast<std::streamsize>(m.indices.size() * sizeof(std::uint32_t)));
    for (const GeoRange& r : m.ranges) {
        put<std::uint32_t>(out, r.firstIndex);
        put<std::uint32_t>(out, r.indexCount);
        put<std::uint8_t>(out, static_cast<std::uint8_t>(r.material));
    }
}

bool readMesh(std::ifstream& in, GeoMesh& m, float x0, float z0) {
    std::uint32_t nv = 0, ni = 0, nr = 0;
    if (!get(in, nv) || !get(in, ni) || !get(in, nr) || nv > 4000000 || ni > 20000000 || nr > 100000) return false;
    m.vertices.resize(nv);
    for (GeoVertex& v : m.vertices) {
        std::uint16_t px, py, pz;
        std::int8_t n[3];
        std::uint8_t c[3];
        if (!get(in, px) || !get(in, py) || !get(in, pz)) return false;
        for (auto& k : n)
            if (!get(in, k)) return false;
        for (auto& k : c)
            if (!get(in, k)) return false;
        v.position[0] = dequantize(px, kOffsetXZ) + x0;
        v.position[1] = dequantize(py, kOffsetY);
        v.position[2] = dequantize(pz, kOffsetXZ) + z0;
        for (int k = 0; k < 3; ++k) {
            v.normal[k] = static_cast<float>(n[k]) / 127.f;
            v.color[k] = static_cast<float>(c[k]) / 255.f;
        }
        if (!get(in, v.uv[0]) || !get(in, v.uv[1])) return false;
    }
    m.indices.resize(ni);
    in.read(reinterpret_cast<char*>(m.indices.data()), static_cast<std::streamsize>(ni * sizeof(std::uint32_t)));
    if (!in) return false;
    for (std::uint32_t idx : m.indices)
        if (idx >= nv) return false;
    for (std::uint32_t i = 0; i < nr; ++i) {
        GeoRange r;
        std::uint8_t mat = 0;
        if (!get(in, r.firstIndex) || !get(in, r.indexCount) || !get(in, mat) || mat >= kGeoMaterialCount || r.firstIndex + r.indexCount > ni) return false;
        r.material = static_cast<GeoMaterial>(mat);
        m.ranges.push_back(r);
    }
    return true;
}
}  // namespace

std::string cellFileName(CellCoord cell) { return "c_" + std::to_string(cell.x) + "_" + std::to_string(cell.z) + ".hcell"; }

bool saveCell(const std::string& file, CellCoord cell, float cellSize, const CityChunk& chunk, std::string& error) {
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        error = "не удалось создать " + file;
        return false;
    }
    out.write("HCL1", 4);
    put<std::int32_t>(out, cell.x);
    put<std::int32_t>(out, cell.z);
    put(out, cellSize);
    const float x0 = static_cast<float>(cell.x) * cellSize, z0 = static_cast<float>(cell.z) * cellSize;
    writeMesh(out, chunk.ground, x0, z0);
    writeMesh(out, chunk.buildings, x0, z0);
    put<std::uint32_t>(out, static_cast<std::uint32_t>(chunk.lamps.size()));
    for (const Vec3& l : chunk.lamps) {
        put(out, l.x);
        put(out, l.y);
        put(out, l.z);
    }
    put<std::uint32_t>(out, static_cast<std::uint32_t>(chunk.walls.size()));
    for (const WallCollider& w : chunk.walls) {
        for (float f : {w.center.x, w.center.y, w.center.z, w.half.x, w.half.y, w.half.z, w.yaw}) put(out, f);
    }
    if (!out) {
        error = "ошибка записи " + file;
        return false;
    }
    return true;
}

bool loadCell(const std::string& file, CellCoord& cell, float& cellSize, CityChunk& chunk, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "нет файла " + file;
        return false;
    }
    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, "HCL1", 4) != 0) {
        error = file + ": не ячейка HCL1";
        return false;
    }
    std::int32_t cx = 0, cz = 0;
    if (!get(in, cx) || !get(in, cz) || !get(in, cellSize) || !(cellSize > 1.f)) {
        error = file + ": повреждён заголовок";
        return false;
    }
    cell = {cx, cz};
    chunk = CityChunk();
    const float x0 = static_cast<float>(cx) * cellSize, z0 = static_cast<float>(cz) * cellSize;
    if (!readMesh(in, chunk.ground, x0, z0) || !readMesh(in, chunk.buildings, x0, z0)) {
        error = file + ": повреждены меши";
        return false;
    }
    std::uint32_t n = 0;
    if (!get(in, n) || n > 1000000) {
        error = file + ": повреждены фонари";
        return false;
    }
    chunk.lamps.resize(n);
    for (Vec3& l : chunk.lamps)
        if (!get(in, l.x) || !get(in, l.y) || !get(in, l.z)) {
            error = file + ": обрезаны фонари";
            return false;
        }
    if (!get(in, n) || n > 4000000) {
        error = file + ": повреждены коллайдеры";
        return false;
    }
    chunk.walls.resize(n);
    for (WallCollider& w : chunk.walls)
        if (!get(in, w.center.x) || !get(in, w.center.y) || !get(in, w.center.z) || !get(in, w.half.x) || !get(in, w.half.y) || !get(in, w.half.z) || !get(in, w.yaw)) {
            error = file + ": обрезаны коллайдеры";
            return false;
        }
    return true;
}

}  // namespace han::world
