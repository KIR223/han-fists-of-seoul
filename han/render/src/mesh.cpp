#include "han/render/mesh.h"

namespace han::render {
namespace {

// Грань: нормаль n и оси u, v с u × v = n; четыре вершины против часовой стрелки при взгляде вдоль -n.
void addFace(MeshData& mesh, core::Vec3 n, core::Vec3 u, core::Vec3 v, core::Vec3 half, core::Vec3 color) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (const auto& c : corners) {
        const core::Vec3 p = n + u * c[0] + v * c[1];
        mesh.vertices.push_back({{p.x * half.x, p.y * half.y, p.z * half.z}, {n.x, n.y, n.z}, {color.x, color.y, color.z}});
    }
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
}

}  // namespace

MeshData makeBox(core::Vec3 half, core::Vec3 color) {
    MeshData m;
    addFace(m, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, half, color);
    addFace(m, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, half, color);
    addFace(m, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, half, color);
    addFace(m, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}, half, color);
    addFace(m, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, half, color);
    addFace(m, {0, 0, -1}, {0, 1, 0}, {1, 0, 0}, half, color);
    return m;
}

MeshData makePlane(float halfSize, core::Vec3 color) {
    MeshData m;
    addFace(m, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {halfSize, 0, halfSize}, color);
    return m;
}

}  // namespace han::render
