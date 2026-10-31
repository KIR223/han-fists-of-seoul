#include "han/tools/gltf_import.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <cmath>
#include <fstream>
#include <map>
#include <string>

#include "han/tools/image_decode.h"
#include "han/tools/mesh_tools.h"

namespace han::tools {
namespace {

constexpr std::size_t kMaxVertices = 1u << 26;

// Определитель верхней матрицы 3x3 (столбцы m[0..2]); отрицательный — зеркальное отражение, обход граней надо перевернуть.
float determinant3(const fastgltf::math::fmat4x4& m) {
    const auto a = m[0], b = m[1], c = m[2];
    return a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
}

// Байты изображения glTF: из буфера (GLB), из встроенных данных или из внешнего файла рядом.
struct ByteSpan {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

ByteSpan spanOf(const fastgltf::sources::Array& a) { return {reinterpret_cast<const std::uint8_t*>(a.bytes.data()), a.bytes.size()}; }
ByteSpan spanOf(const fastgltf::sources::Vector& v) { return {reinterpret_cast<const std::uint8_t*>(v.bytes.data()), v.bytes.size()}; }
ByteSpan spanOf(const fastgltf::sources::ByteView& b) { return {reinterpret_cast<const std::uint8_t*>(b.bytes.data()), b.bytes.size()}; }

// Байты буфера, уже загруженного в память разборщиком (GLB, внешние буферы, data-URI); иначе пусто.
ByteSpan bufferBytes(const fastgltf::DataSource& source) {
    return std::visit(fastgltf::visitor{[](const fastgltf::sources::Array& a) { return spanOf(a); },
                                        [](const fastgltf::sources::Vector& v) { return spanOf(v); },
                                        [](const fastgltf::sources::ByteView& b) { return spanOf(b); },
                                        [](const auto&) { return ByteSpan{}; }},
                      source);
}

bool imageBytes(const fastgltf::Asset& gltf, const fastgltf::Image& image, const std::filesystem::path& baseDir,
                std::vector<std::uint8_t>& fileStorage, ByteSpan& out, std::string& why) {
    out = std::visit(
        fastgltf::visitor{
            [&](const fastgltf::sources::BufferView& bv) {
                const fastgltf::BufferView& view = gltf.bufferViews[bv.bufferViewIndex];
                const ByteSpan whole = bufferBytes(gltf.buffers[view.bufferIndex].data);
                if (!whole.data || view.byteOffset + view.byteLength > whole.size) return ByteSpan{};
                return ByteSpan{whole.data + view.byteOffset, view.byteLength};
            },
            [&](const fastgltf::sources::URI& u) {
                if (!u.uri.isLocalPath()) return ByteSpan{};
                std::ifstream file(baseDir / u.uri.fspath(), std::ios::binary | std::ios::ate);
                if (!file) {
                    why = "файл изображения не найден: " + (baseDir / u.uri.fspath()).string();
                    return ByteSpan{};
                }
                const std::streamsize size = file.tellg();
                file.seekg(0);
                fileStorage.resize(static_cast<std::size_t>(size));
                if (size > 0 && !file.read(reinterpret_cast<char*>(fileStorage.data()), size)) return ByteSpan{};
                return ByteSpan{fileStorage.data(), fileStorage.size()};
            },
            [](const fastgltf::sources::Array& a) { return spanOf(a); },
            [](const fastgltf::sources::Vector& v) { return spanOf(v); },
            [](const fastgltf::sources::ByteView& b) { return spanOf(b); },
            [](const auto&) { return ByteSpan{}; }},
        image.data);
    if (!out.data && why.empty()) why = "не удалось получить данные изображения";
    return out.data != nullptr;
}

struct Importer {
    fastgltf::Asset& gltf;
    std::filesystem::path baseDir;
    ImportResult& result;
    std::map<std::size_t, int> imageSlots;  // индекс изображения glTF → номер в result.textures (-1 — не удалось)

    // Номер текстуры базового цвета материала в result.textures или -1; texCoord — какой набор UV использовать.
    int baseColorSlot(const fastgltf::Material& material, std::size_t& texCoord) {
        if (!material.pbrData.baseColorTexture.has_value()) return -1;
        const fastgltf::TextureInfo& info = material.pbrData.baseColorTexture.value();
        texCoord = info.texCoordIndex;
        if (info.textureIndex >= gltf.textures.size()) return -1;
        const fastgltf::Texture& texture = gltf.textures[info.textureIndex];
        if (!texture.imageIndex.has_value() || texture.imageIndex.value() >= gltf.images.size()) return -1;
        const std::size_t imageIndex = texture.imageIndex.value();

        if (const auto it = imageSlots.find(imageIndex); it != imageSlots.end()) return it->second;
        int slot = -1;
#ifdef HAN_WITH_STB
        std::vector<std::uint8_t> fileStorage;
        ByteSpan bytes;
        std::string why;
        DecodedImage decoded;
        if (imageBytes(gltf, gltf.images[imageIndex], baseDir, fileStorage, bytes, why) && decodeImage(bytes.data, bytes.size, decoded, why)) {
            slot = static_cast<int>(result.textures.size());
            result.textures.push_back({decoded.width, decoded.height, std::move(decoded.rgba)});
        } else {
            result.warnings.push_back("изображение " + std::to_string(imageIndex) + " пропущено: " + why);
        }
#else
        result.warnings.push_back("собрано без stb_image: текстуры не читаются");
#endif
        imageSlots[imageIndex] = slot;
        return slot;
    }

    bool appendPrimitive(const fastgltf::Primitive& prim, const fastgltf::math::fmat4x4& world, std::string& error) {
        if (prim.type != fastgltf::PrimitiveType::Triangles) return true;  // линии и точки пропускаем
        render::MeshData& out = result.mesh;

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
        int slot = -1;
        std::size_t texCoord = 0;
        if (prim.materialIndex.has_value() && prim.materialIndex.value() < gltf.materials.size()) {
            const fastgltf::Material& material = gltf.materials[prim.materialIndex.value()];
            const auto& f = material.pbrData.baseColorFactor;
            color[0] = f[0];
            color[1] = f[1];
            color[2] = f[2];
            slot = baseColorSlot(material, texCoord);
        }

        const fastgltf::Accessor* uvAccessor = nullptr;
        if (slot >= 0) {
            const auto* uvAttr = prim.findAttribute("TEXCOORD_" + std::to_string(texCoord));
            if (uvAttr != prim.attributes.end() && gltf.accessors[uvAttr->accessorIndex].count == posAccessor.count) {
                uvAccessor = &gltf.accessors[uvAttr->accessorIndex];
            } else {
                result.warnings.push_back("у примитива нет UV для текстуры: текстура пропущена");
                slot = -1;
            }
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
            v.uv[0] = v.uv[1] = 0;
        });
        if (uvAccessor) {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(gltf, *uvAccessor, [&](fastgltf::math::fvec2 uv, std::size_t i) {
                out.vertices[base + i].uv[0] = uv[0];
                out.vertices[base + i].uv[1] = uv[1];
            });
        }

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

        const auto count = static_cast<std::uint32_t>(out.indices.size() - firstIndex);
        if (!out.submeshes.empty() && out.submeshes.back().texture == slot)
            out.submeshes.back().indexCount += count;  // соседний примитив с тем же материалом — одна часть
        else
            out.submeshes.push_back({static_cast<std::uint32_t>(firstIndex), count, slot});
        return true;
    }
};

}  // namespace

bool importGltf(const std::filesystem::path& path, ImportResult& out, std::string& error) {
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

    ImportResult result;
    Importer importer{gltf, path.parent_path(), result, {}};
    bool needNormals = false;
    bool failed = false;
    fastgltf::iterateSceneNodes(gltf, sceneIndex, fastgltf::math::fmat4x4(), [&](fastgltf::Node& node, const fastgltf::math::fmat4x4& world) {
        if (failed || !node.meshIndex.has_value()) return;
        const fastgltf::Mesh& mesh = gltf.meshes[node.meshIndex.value()];
        for (const fastgltf::Primitive& prim : mesh.primitives) {
            if (prim.findAttribute("NORMAL") == prim.attributes.end()) needNormals = true;
            if (!importer.appendPrimitive(prim, world, error)) {
                failed = true;
                return;
            }
        }
    });
    if (failed) return false;
    if (result.mesh.vertices.empty() || result.mesh.indices.empty()) {
        error = "в сцене нет треугольных мешей";
        return false;
    }
    if (needNormals) computeSmoothNormals(result.mesh);  // v1: пересчёт для всего меша, если хотя бы у одного примитива нет нормалей
    // Если ни одна часть не использует текстуру, список частей не нужен: весь меш — одна часть без текстуры.
    bool anyTexture = false;
    for (const render::Submesh& s : result.mesh.submeshes) anyTexture = anyTexture || s.texture >= 0;
    if (!anyTexture) result.mesh.submeshes.clear();
    out = std::move(result);
    return true;
}

}  // namespace han::tools
