#include "han/tools/mesh_tools.h"

#include <cstring>
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

}  // namespace han::tools
