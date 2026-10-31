#include "han/render/mesh.h"

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

MeshData makePlane(float halfSize, core::Vec3 color, float uvRepeat) {
    MeshData m;
    addFace(m, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {halfSize, 0, halfSize}, color, uvRepeat);
    return m;
}

}  // namespace han::render
