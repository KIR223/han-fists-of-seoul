#include "han/tools/gltf_import.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <cmath>
#include <string>

#include "han/tools/mesh_tools.h"

namespace han::tools {
namespace {

constexpr std::size_t kMaxVertices = 1u << 26;

// Определитель верхней матрицы 3x3 (столбцы m[0..2]); отрицательный — зеркальное отражение, обход граней надо перевернуть.
float determinant3(const fastgltf::math::fmat4x4& m) {
    const auto a = m[0], b = m[1], c = m[2];
    return a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
}

bool appendPrimitive(const fastgltf::Asset& gltf, const fastgltf::Primitive& prim, const fastgltf::math::fmat4x4& world,
                     render::MeshData& out, std::string& error) {
    if (prim.type != fastgltf::PrimitiveType::Triangles) return true;  // линии и точки пропускаем

    const auto* posAttr = prim.findAttribute("POSITION");
    if (posAttr == prim.attributes.end()) {
        error = "у примитива нет атрибута POSITION";
        return false;
    }
    const fastgltf::Accessor& posAccessor = gltf.accessors[posAttr->accessorIndex];
    if (out.vertices.size() + posAccessor.count > kMaxVertices) {
        error = "слишком много вершин";
        return false;
    }

    float color[3] = {0.8f, 0.8f, 0.8f};
    if (prim.materialIndex.has_value() && prim.materialIndex.value() < gltf.materials.size()) {
        const auto& f = gltf.materials[prim.materialIndex.value()].pbrData.baseColorFactor;
        color[0] = f[0];
        color[1] = f[1];
        color[2] = f[2];
    }

    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    out.vertices.resize(base + posAccessor.count);
    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, posAccessor, [&](fastgltf::math::fvec3 p, std::size_t i) {
        render::Vertex& v = out.vertices[base + i];
        v.position[0] = world[0][0] * p[0] + world[1][0] * p[1] + world[2][0] * p[2] + world[3][0];
        v.position[1] = world[0][1] * p[0] + world[1][1] * p[1] + world[2][1] * p[2] + world[3][1];
        v.position[2] = world[0][2] * p[0] + world[1][2] * p[1] + world[2][2] * p[2] + world[3][2];
        v.normal[0] = v.normal[1] = v.normal[2] = 0;
        v.color[0] = color[0];
        v.color[1] = color[1];
        v.color[2] = color[2];
    });

    const auto* nrmAttr = prim.findAttribute("NORMAL");
    const bool hasNormals = nrmAttr != prim.attributes.end() && gltf.accessors[nrmAttr->accessorIndex].count == posAccessor.count;
    if (hasNormals) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            gltf, gltf.accessors[nrmAttr->accessorIndex], [&](fastgltf::math::fvec3 n, std::size_t i) {
                const float x = world[0][0] * n[0] + world[1][0] * n[1] + world[2][0] * n[2];
                const float y = world[0][1] * n[0] + world[1][1] * n[1] + world[2][1] * n[2];
                const float z = world[0][2] * n[0] + world[1][2] * n[1] + world[2][2] * n[2];
                const float len = std::sqrt(x * x + y * y + z * z);
                render::Vertex& v = out.vertices[base + i];
                v.normal[0] = len > 0 ? x / len : 0;
                v.normal[1] = len > 0 ? y / len : 0;
                v.normal[2] = len > 0 ? z / len : 0;
            });
    }

    const std::size_t firstIndex = out.indices.size();
    if (prim.indicesAccessor.has_value()) {
        const fastgltf::Accessor& idxAccessor = gltf.accessors[prim.indicesAccessor.value()];
        out.indices.reserve(firstIndex + idxAccessor.count);
        bool outOfRange = false;
        fastgltf::iterateAccessor<std::uint32_t>(gltf, idxAccessor, [&](std::uint32_t idx) {
            if (idx >= posAccessor.count) outOfRange = true;
            out.indices.push_back(base + idx);
        });
        if (outOfRange) {
            error = "индекс примитива указывает за пределы вершин";
            return false;
        }
    } else {
        for (std::size_t i = 0; i < posAccessor.count; ++i) out.indices.push_back(base + static_cast<std::uint32_t>(i));
    }
    if ((out.indices.size() - firstIndex) % 3 != 0) {
        error = "число индексов примитива не кратно трём";
        return false;
    }

    if (determinant3(world) < 0) {
        for (std::size_t i = firstIndex; i + 2 < out.indices.size(); i += 3) std::swap(out.indices[i + 1], out.indices[i + 2]);
    }
    return true;
}

}  // namespace

bool importGltf(const std::filesystem::path& path, render::MeshData& out, std::string& error) {
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        error = "не удалось прочитать " + path.string() + ": " + std::string(fastgltf::getErrorMessage(data.error()));
        return false;
    }
    fastgltf::Parser parser;
    auto parsed = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers);
    if (parsed.error() != fastgltf::Error::None) {
        error = "ошибка разбора glTF: " + std::string(fastgltf::getErrorMessage(parsed.error()));
        return false;
    }
    fastgltf::Asset& gltf = parsed.get();  // не const: iterateSceneNodes передаёт узлы по неконстантной ссылке
    if (gltf.scenes.empty()) {
        error = "в файле нет сцен";
        return false;
    }
    const std::size_t sceneIndex = gltf.defaultScene.has_value() ? gltf.defaultScene.value() : 0;
    if (sceneIndex >= gltf.scenes.size()) {
        error = "неверный индекс сцены по умолчанию";
        return false;
    }

    render::MeshData result;
    bool needNormals = false;
    bool failed = false;
    fastgltf::iterateSceneNodes(gltf, sceneIndex, fastgltf::math::fmat4x4(), [&](fastgltf::Node& node, const fastgltf::math::fmat4x4& world) {
        if (failed || !node.meshIndex.has_value()) return;
        const fastgltf::Mesh& mesh = gltf.meshes[node.meshIndex.value()];
        for (const fastgltf::Primitive& prim : mesh.primitives) {
            const auto* nrm = prim.findAttribute("NORMAL");
            if (nrm == prim.attributes.end()) needNormals = true;
            if (!appendPrimitive(gltf, prim, world, result, error)) {
                failed = true;
                return;
            }
        }
    });
    if (failed) return false;
    if (result.vertices.empty() || result.indices.empty()) {
        error = "в сцене нет треугольных мешей";
        return false;
    }
    if (needNormals) computeSmoothNormals(result);  // v1: пересчёт для всего меша, если хотя бы у одного примитива нет нормалей
    out = std::move(result);
    return true;
}

}  // namespace han::tools
