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

bool validRanges(const GeoMesh& m) {
    std::uint64_t covered = 0;
    for (const GeoRange& r : m.ranges) {
        if (static_cast<unsigned>(r.material) >= kGeoMaterialCount || r.firstIndex != covered || r.indexCount % 3 != 0 ||
            r.firstIndex > m.indices.size() || r.indexCount > m.indices.size() - r.firstIndex) return false;
        covered += r.indexCount;
    }
    return m.ranges.empty() || covered == m.indices.size();
}

bool validMesh(const GeoMesh& m, float x0, float z0, bool& wide) {
    if (m.vertices.size() > 4000000 || m.indices.size() > 20000000 || m.ranges.size() > 100000 || m.indices.size() % 3 != 0 || !validRanges(m)) return false;
    for (const auto idx : m.indices)
        if (idx >= m.vertices.size()) return false;
    for (const GeoVertex& v : m.vertices) {
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(v.position[k]) || !std::isfinite(v.normal[k]) || !std::isfinite(v.color[k]) ||
                v.normal[k] < -1.f || v.normal[k] > 1.f || v.color[k] < 0.f || v.color[k] > 1.f) return false;
            const float local = v.position[k] - (k == 0 ? x0 : (k == 2 ? z0 : 0.f));
            if (!std::isfinite(local)) return false;
            const float cm = (local + (k == 1 ? kOffsetY : kOffsetXZ)) * 100.f;
            wide = wide || !std::isfinite(cm) || cm < 0.f || cm > 65535.f;
        }
        if (!std::isfinite(v.uv[0]) || !std::isfinite(v.uv[1])) return false;
    }
    return true;
}

bool hasBytes(std::ifstream& in, std::uint64_t bytes) {
    const auto pos = in.tellg();
    if (pos < 0) return false;
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    in.seekg(pos);
    return in && end >= pos && bytes <= static_cast<std::uint64_t>(end - pos);
}

bool validObjects(const CityChunk& chunk) {
    for (const auto& tree : chunk.trees) {
        for (const float f : {tree.position.x, tree.position.y, tree.position.z, tree.height, tree.yaw})
            if (!std::isfinite(f)) return false;
        if (tree.height <= 0 || tree.height > 300) return false;
    }
    for (const auto& l : chunk.lamps)
        if (!std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z)) return false;
    for (const auto& w : chunk.walls) {
        for (const float f : {w.center.x, w.center.y, w.center.z, w.half.x, w.half.y, w.half.z, w.yaw})
            if (!std::isfinite(f)) return false;
        if (w.half.x <= 0.f || w.half.y <= 0.f || w.half.z <= 0.f) return false;
    }
    return true;
}

void writeMesh(std::ofstream& out, const GeoMesh& m, float x0, float z0, bool wide) {
    put<std::uint32_t>(out, static_cast<std::uint32_t>(m.vertices.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(m.indices.size()));
    put<std::uint32_t>(out, static_cast<std::uint32_t>(m.ranges.size()));
    for (const GeoVertex& v : m.vertices) {
        if (wide) {
            put(out, v.position[0] - x0);
            put(out, v.position[1]);
            put(out, v.position[2] - z0);
        } else {
            put(out, quantize(v.position[0] - x0, kOffsetXZ));
            put(out, quantize(v.position[1], kOffsetY));
            put(out, quantize(v.position[2] - z0, kOffsetXZ));
        }
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

bool readMesh(std::ifstream& in, GeoMesh& m, float x0, float z0, bool wide) {
    std::uint32_t nv = 0, ni = 0, nr = 0;
    if (!get(in, nv) || !get(in, ni) || !get(in, nr) || nv > 4000000 || ni > 20000000 || nr > 100000 || ni % 3 != 0 ||
        !hasBytes(in, static_cast<std::uint64_t>(nv) * (wide ? 26 : 20) + static_cast<std::uint64_t>(ni) * 4 + static_cast<std::uint64_t>(nr) * 9)) return false;
    m.vertices.resize(nv);
    for (GeoVertex& v : m.vertices) {
        std::int8_t n[3];
        std::uint8_t c[3];
        if (wide) {
            if (!get(in, v.position[0]) || !get(in, v.position[1]) || !get(in, v.position[2])) return false;
            v.position[0] += x0;
            v.position[2] += z0;
        } else {
            std::uint16_t px, py, pz;
            if (!get(in, px) || !get(in, py) || !get(in, pz)) return false;
            v.position[0] = dequantize(px, kOffsetXZ) + x0;
            v.position[1] = dequantize(py, kOffsetY);
            v.position[2] = dequantize(pz, kOffsetXZ) + z0;
        }
        for (auto& k : n)
            if (!get(in, k)) return false;
        for (auto& k : c)
            if (!get(in, k)) return false;
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
        if (!get(in, r.firstIndex) || !get(in, r.indexCount) || !get(in, mat) || mat >= kGeoMaterialCount || r.firstIndex > ni || r.indexCount > ni - r.firstIndex) return false;
        r.material = static_cast<GeoMaterial>(mat);
        m.ranges.push_back(r);
    }
    bool unused = false;
    return validMesh(m, x0, z0, unused);
}
}  // namespace

std::string cellFileName(CellCoord cell) { return "c_" + std::to_string(cell.x) + "_" + std::to_string(cell.z) + ".hcell"; }

std::future<CellReadResult> loadCellAsync(std::string file, CellCoord expected, float expectedSize) {
    return std::async(std::launch::async, [file = std::move(file), expected, expectedSize] {
        CellReadResult result;
        CellCoord actual;
        float size = 0.f;
        result.ok = loadCell(file, actual, size, result.chunk, result.error);
        if (result.ok && (!(actual == expected) || size != expectedSize)) {
            result.ok = false;
            result.error = file + ": координаты или размер не совпадают с запрошенной ячейкой";
        }
        return result;
    });
}

bool saveCell(const std::string& file, CellCoord cell, float cellSize, const CityChunk& chunk, std::string& error) {
    const float x0 = static_cast<float>(cell.x) * cellSize, z0 = static_cast<float>(cell.z) * cellSize;
    bool wide = false;
    if (!std::isfinite(cellSize) || cellSize <= 1.f || !std::isfinite(x0) || !std::isfinite(z0) ||
        !validMesh(chunk.ground, x0, z0, wide) || !validMesh(chunk.buildings, x0, z0, wide) ||
        chunk.lamps.size() > 1000000 || chunk.walls.size() > 4000000 || chunk.trees.size() > 1000000 || !validObjects(chunk)) {
        error = file + ": некорректная геометрия или размер ячейки";
        return false;
    }
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        error = "не удалось создать " + file;
        return false;
    }
    const bool withTrees = !chunk.trees.empty();
    out.write(withTrees ? "HCL3" : (wide ? "HCL2" : "HCL1"), 4);
    put<std::int32_t>(out, cell.x);
    put<std::int32_t>(out, cell.z);
    put(out, cellSize);
    if (withTrees) put<std::uint32_t>(out, wide ? 1u : 0u);
    writeMesh(out, chunk.ground, x0, z0, wide);
    writeMesh(out, chunk.buildings, x0, z0, wide);
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
    if (withTrees) {
        put<std::uint32_t>(out, static_cast<std::uint32_t>(chunk.trees.size()));
        for (const auto& tree : chunk.trees)
            for (float f : {tree.position.x, tree.position.y, tree.position.z, tree.height, tree.yaw}) put(out, f);
    }
    if (!out) {
        error = "ошибка записи " + file;
        return false;
    }
    return true;
}

static bool readCell(const std::string& file, CellCoord& cell, float& cellSize, CityChunk& chunk, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "нет файла " + file;
        return false;
    }
    char magic[4]{};
    in.read(magic, 4);
    bool wide = std::memcmp(magic, "HCL2", 4) == 0;
    const bool withTrees = std::memcmp(magic, "HCL3", 4) == 0;
    if (!in || (!wide && !withTrees && std::memcmp(magic, "HCL1", 4) != 0)) {
        error = file + ": не ячейка HCL1/HCL2/HCL3";
        return false;
    }
    std::int32_t cx = 0, cz = 0;
    if (!get(in, cx) || !get(in, cz) || !get(in, cellSize) || !std::isfinite(cellSize) || !(cellSize > 1.f)) {
        error = file + ": повреждён заголовок";
        return false;
    }
    cell = {cx, cz};
    if (withTrees) {
        std::uint32_t flags = 0;
        if (!get(in, flags) || flags > 1) { error = file + ": повреждены флаги HCL3"; return false; }
        wide = flags != 0;
    }
    chunk = CityChunk();
    const float x0 = static_cast<float>(cx) * cellSize, z0 = static_cast<float>(cz) * cellSize;
    if (!std::isfinite(x0) || !std::isfinite(z0) || !readMesh(in, chunk.ground, x0, z0, wide) || !readMesh(in, chunk.buildings, x0, z0, wide)) {
        error = file + ": повреждены меши";
        return false;
    }
    std::uint32_t n = 0;
    if (!get(in, n) || n > 1000000 || !hasBytes(in, static_cast<std::uint64_t>(n) * 12)) {
        error = file + ": повреждены фонари";
        return false;
    }
    chunk.lamps.resize(n);
    for (Vec3& l : chunk.lamps)
        if (!get(in, l.x) || !get(in, l.y) || !get(in, l.z)) {
            error = file + ": обрезаны фонари";
            return false;
        }
    if (!get(in, n) || n > 4000000 || !hasBytes(in, static_cast<std::uint64_t>(n) * 28)) {
        error = file + ": повреждены коллайдеры";
        return false;
    }
    chunk.walls.resize(n);
    for (WallCollider& w : chunk.walls)
        if (!get(in, w.center.x) || !get(in, w.center.y) || !get(in, w.center.z) || !get(in, w.half.x) || !get(in, w.half.y) || !get(in, w.half.z) || !get(in, w.yaw)) {
            error = file + ": обрезаны коллайдеры";
            return false;
        }
    if (withTrees) {
        if (!get(in, n) || n > 1000000 || !hasBytes(in, static_cast<std::uint64_t>(n) * 20)) {
            error = file + ": повреждены экземпляры деревьев"; return false;
        }
        chunk.trees.resize(n);
        for (auto& tree : chunk.trees)
            if (!get(in, tree.position.x) || !get(in, tree.position.y) || !get(in, tree.position.z) || !get(in, tree.height) || !get(in, tree.yaw)) {
                error = file + ": обрезаны экземпляры деревьев"; return false;
            }
    }
    if (!validObjects(chunk) || in.peek() != std::char_traits<char>::eof()) {
        error = file + ": некорректные объекты или лишние данные";
        return false;
    }
    return true;
}

bool loadCell(const std::string& file, CellCoord& cell, float& cellSize, CityChunk& chunk, std::string& error) {
    CellCoord loadedCell;
    float loadedSize = 0.f;
    CityChunk loaded;
    if (!readCell(file, loadedCell, loadedSize, loaded, error)) return false;
    cell = loadedCell;
    cellSize = loadedSize;
    chunk = std::move(loaded);
    return true;
}

}  // namespace han::world
