#include "han/tools/mesh_tools.h"

#include <cstring>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef HAN_WITH_MESHOPT
#include <meshoptimizer.h>
#endif

namespace han::tools {

void weldVertices(render::MeshData& mesh) {
    std::unordered_map<std::string, std::uint32_t> seen;
    seen.reserve(mesh.vertices.size());
    std::vector<render::Vertex> unique;
    std::vector<std::uint32_t> remap(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        std::string key(reinterpret_cast<const char*>(&mesh.vertices[i]), sizeof(render::Vertex));
        const auto [it, inserted] = seen.emplace(std::move(key), static_cast<std::uint32_t>(unique.size()));
        if (inserted) unique.push_back(mesh.vertices[i]);
        remap[i] = it->second;
    }
    for (std::uint32_t& index : mesh.indices) index = remap[index];
    mesh.vertices = std::move(unique);
}

void computeSmoothNormals(render::MeshData& mesh) {
    std::vector<core::Vec3> accum(mesh.vertices.size());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        const auto pos = [&](std::uint32_t k) {
            const auto& p = mesh.vertices[k].position;
            return core::Vec3{p[0], p[1], p[2]};
        };
        // Векторное произведение не нормируем: его длина пропорциональна площади, что и даёт вес.
        const core::Vec3 n = core::cross(pos(ib) - pos(ia), pos(ic) - pos(ia));
        accum[ia] = accum[ia] + n;
        accum[ib] = accum[ib] + n;
        accum[ic] = accum[ic] + n;
    }
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const core::Vec3 n = core::normalize(accum[i]);
        mesh.vertices[i].normal[0] = n.x;
        mesh.vertices[i].normal[1] = n.y;
        mesh.vertices[i].normal[2] = n.z;
    }
}

void optimizeMesh(render::MeshData& mesh) {
    weldVertices(mesh);
#ifdef HAN_WITH_MESHOPT
    if (mesh.indices.empty() || mesh.vertices.empty()) return;
    // Кэш оптимизируем внутри каждой части меша отдельно: части (материалы) должны остаться непрерывными диапазонами индексов.
    std::vector<std::uint32_t> optimized(mesh.indices.size());
    for (const render::Submesh& part : render::effectiveSubmeshes(mesh)) {
        if (part.indexCount == 0) continue;
        meshopt_optimizeVertexCache(optimized.data() + part.firstIndex, mesh.indices.data() + part.firstIndex, part.indexCount,
                                    mesh.vertices.size());
    }
    std::vector<render::Vertex> fetched(mesh.vertices.size());
    const std::size_t used = meshopt_optimizeVertexFetch(fetched.data(), optimized.data(), optimized.size(), mesh.vertices.data(),
                                                         mesh.vertices.size(), sizeof(render::Vertex));
    fetched.resize(used);
    mesh.vertices = std::move(fetched);
    mesh.indices = std::move(optimized);
#endif
}

bool partitionMesh(render::MeshData& mesh, float cellSize) {
    if (!std::isfinite(cellSize) || cellSize < 1.f || cellSize > 10000.f || mesh.indices.size() % 3 != 0) return false;
    for (const auto index : mesh.indices) if (index >= mesh.vertices.size()) return false;
    for (const auto& vertex : mesh.vertices)
        for (const float value : vertex.position) if (!std::isfinite(value) || std::abs(value) / cellSize > 1000000.f) return false;
    std::vector<std::uint32_t> indices;
    std::vector<render::Submesh> parts;
    indices.reserve(mesh.indices.size());
    for (const auto& part : render::effectiveSubmeshes(mesh)) {
        if (part.firstIndex > mesh.indices.size() || part.indexCount > mesh.indices.size() - part.firstIndex || part.indexCount % 3) return false;
        std::map<std::pair<int, int>, std::vector<std::uint32_t>> cells;
        for (std::size_t i = part.firstIndex; i < static_cast<std::size_t>(part.firstIndex) + part.indexCount; i += 3) {
            float x = 0, z = 0;
            for (int corner = 0; corner < 3; ++corner) {
                const auto& vertex = mesh.vertices[mesh.indices[i + corner]];
                x += vertex.position[0] / 3.f; z += vertex.position[2] / 3.f;
            }
            auto& list = cells[{static_cast<int>(std::floor(x / cellSize)), static_cast<int>(std::floor(z / cellSize))}];
            list.insert(list.end(), mesh.indices.begin() + i, mesh.indices.begin() + i + 3);
        }
        for (const auto& [cell, list] : cells) {
            auto result = part;
            result.firstIndex = static_cast<std::uint32_t>(indices.size());
            result.indexCount = static_cast<std::uint32_t>(list.size());
            indices.insert(indices.end(), list.begin(), list.end());
            parts.push_back(result);
        }
    }
    if (indices.size() != mesh.indices.size() || parts.size() > (1u << 16)) return false;
    mesh.indices = std::move(indices);
    mesh.submeshes = std::move(parts);
    return true;
}

}  // namespace han::tools
