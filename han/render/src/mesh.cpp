#include "han/render/mesh.h"

#include <algorithm>
#include <cmath>

namespace han::render {
namespace {

// Грань: нормаль n и оси u, v с u × v = n; четыре вершины против часовой стрелки при взгляде вдоль -n.
void addFace(MeshData& mesh, core::Vec3 n, core::Vec3 u, core::Vec3 v, core::Vec3 half, core::Vec3 color, float uvScale) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (const auto& c : corners) {
        const core::Vec3 p = n + u * c[0] + v * c[1];
        const float tu = (c[0] + 1.0f) * 0.5f * uvScale;
        const float tv = (1.0f - (c[1] + 1.0f) * 0.5f) * uvScale;
        mesh.vertices.push_back({{p.x * half.x, p.y * half.y, p.z * half.z}, {n.x, n.y, n.z}, {color.x, color.y, color.z}, {tu, tv}});
    }
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
}

}  // namespace

std::vector<Submesh> effectiveSubmeshes(const MeshData& mesh) {
    if (!mesh.submeshes.empty()) return mesh.submeshes;
    return {Submesh{0, static_cast<std::uint32_t>(mesh.indices.size()), -1}};
}

MeshData makeBox(core::Vec3 half, core::Vec3 color) {
    MeshData m;
    addFace(m, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, half, color, 1.0f);
    addFace(m, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, half, color, 1.0f);
    addFace(m, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, half, color, 1.0f);
    addFace(m, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}, half, color, 1.0f);
    addFace(m, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, half, color, 1.0f);
    addFace(m, {0, 0, -1}, {0, 1, 0}, {1, 0, 0}, half, color, 1.0f);
    return m;
}

MeshData makeSphere(float radius, int segments, int rings, core::Vec3 color) {
    constexpr float kPi = 3.14159265358979f;
    segments = std::max(segments, 3);
    rings = std::max(rings, 2);
    MeshData m;
    for (int i = 0; i <= rings; ++i) {
        const float theta = kPi * static_cast<float>(i) / static_cast<float>(rings);
        for (int j = 0; j <= segments; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(segments);
            const float nx = std::sin(theta) * std::cos(phi), ny = std::cos(theta), nz = std::sin(theta) * std::sin(phi);
            m.vertices.push_back({{nx * radius, ny * radius, nz * radius},
                                  {nx, ny, nz},
                                  {color.x, color.y, color.z},
                                  {static_cast<float>(j) / static_cast<float>(segments), static_cast<float>(i) / static_cast<float>(rings)}});
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const auto a = static_cast<std::uint32_t>(i * (segments + 1) + j), b = a + static_cast<std::uint32_t>(segments + 1);
            if (i != 0) {  // у полюса первая вершина совпадает со второй: треугольник вырожден
                m.indices.insert(m.indices.end(), {a, a + 1, b});
            }
            if (i != rings - 1) {
                m.indices.insert(m.indices.end(), {a + 1, b + 1, b});
            }
        }
    }
    return m;
}

MeshData makePlane(float halfSize, core::Vec3 color, float uvRepeat) {
    MeshData m;
    addFace(m, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {halfSize, 0, halfSize}, color, uvRepeat);
    return m;
}

}  // namespace han::render
