#include "han/tools/gltf_import.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <tuple>

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
    std::map<std::tuple<std::size_t, int, float, bool>, int> imageSlots;  // изображение, роль, множитель, режим мипов → слот

    // Номер текстуры базового цвета материала в result.textures или -1; texCoord — какой набор UV использовать.
    int textureSlot(const fastgltf::TextureInfo& info, int role, float factor = 1.f, bool masked = false) {
        if (info.textureIndex >= gltf.textures.size()) return -1;
        const fastgltf::Texture& texture = gltf.textures[info.textureIndex];
        if (!texture.imageIndex.has_value() || texture.imageIndex.value() >= gltf.images.size()) return -1;
        const std::size_t imageIndex = texture.imageIndex.value();

        const auto key = std::make_tuple(imageIndex, role, factor, masked);
        if (const auto it = imageSlots.find(key); it != imageSlots.end()) return it->second;
        int slot = -1;
#ifdef HAN_WITH_STB
        std::vector<std::uint8_t> fileStorage;
        ByteSpan bytes;
        std::string why;
        DecodedImage decoded;
        if (imageBytes(gltf, gltf.images[imageIndex], baseDir, fileStorage, bytes, why) && decodeImage(bytes.data, bytes.size, decoded, why)) {
            slot = static_cast<int>(result.textures.size());
            if (role == 0 && masked)
                for (std::size_t i = 3; i < decoded.rgba.size(); i += 4) decoded.rgba[i] = static_cast<std::uint8_t>(std::lround(decoded.rgba[i] * factor));
            if (role == 2) {  // glTF: шероховатость в G, металлическость в B; движок читает серую карту из R.
                for (std::size_t i = 0; i < decoded.rgba.size(); i += 4) {
                    const auto value = static_cast<std::uint8_t>(std::lround(decoded.rgba[i + 1] * factor));
                    decoded.rgba[i] = decoded.rgba[i + 1] = decoded.rgba[i + 2] = value;
                    decoded.rgba[i + 3] = 255;
                }
            }
            result.textures.push_back({decoded.width, decoded.height, std::move(decoded.rgba), role == 0 || role == 3, masked});
        } else {
            result.warnings.push_back("изображение " + std::to_string(imageIndex) + " пропущено: " + why);
        }
#else
        result.warnings.push_back("собрано без stb_image: текстуры не читаются");
#endif
        imageSlots[key] = slot;
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

        float color[3] = {1.f, 1.f, 1.f};
        float metallic = 0.0f, roughness = 0.5f;
        int slot = -1, normalSlot = -1, roughSlot = -1, emissiveSlot = -1;
        float opacity = 1.f;
        float emissive[3] = {};
        render::AlphaMode alphaMode = render::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        std::size_t texCoord = 0;
        if (prim.materialIndex.has_value() && prim.materialIndex.value() < gltf.materials.size()) {
            const fastgltf::Material& material = gltf.materials[prim.materialIndex.value()];
            alphaMode = material.alphaMode == fastgltf::AlphaMode::Blend ? render::AlphaMode::Blend :
                (material.alphaMode == fastgltf::AlphaMode::Mask ? render::AlphaMode::Mask : render::AlphaMode::Opaque);
            alphaCutoff = material.alphaCutoff;
            doubleSided = material.doubleSided;
            const auto& f = material.pbrData.baseColorFactor;
            opacity = f[3];
            for (int channel = 0; channel < 3; ++channel) emissive[channel] = material.emissiveFactor[channel] * material.emissiveStrength;
            if (alphaMode == render::AlphaMode::Mask && f[3] < alphaCutoff) return true;
            color[0] = f[0];
            color[1] = f[1];
            color[2] = f[2];
            metallic = std::clamp(material.pbrData.metallicFactor, 0.0f, 1.0f);
            roughness = std::clamp(material.pbrData.roughnessFactor, 0.0f, 1.0f);
            if (material.pbrData.baseColorTexture.has_value()) {
                texCoord = material.pbrData.baseColorTexture->texCoordIndex;
                slot = textureSlot(*material.pbrData.baseColorTexture, 0, f[3], alphaMode == render::AlphaMode::Mask);
            } else if (material.normalTexture.has_value()) texCoord = material.normalTexture->texCoordIndex;
            else if (material.pbrData.metallicRoughnessTexture.has_value()) texCoord = material.pbrData.metallicRoughnessTexture->texCoordIndex;
            else if (material.emissiveTexture.has_value()) texCoord = material.emissiveTexture->texCoordIndex;
            if (material.emissiveTexture.has_value()) {
                if (material.emissiveTexture->texCoordIndex == texCoord) emissiveSlot = textureSlot(*material.emissiveTexture, 3);
                else result.warnings.push_back("карта свечения пропущена: другой набор UV");
            }
            if (material.normalTexture.has_value()) {
                if (material.normalTexture->texCoordIndex == texCoord && material.normalTexture->scale == 1.f) normalSlot = textureSlot(*material.normalTexture, 1);
                else result.warnings.push_back("карта нормалей пропущена: другой набор UV или неподдерживаемая сила normal.scale");
            }
            if (material.pbrData.metallicRoughnessTexture.has_value()) {
                if (material.pbrData.metallicRoughnessTexture->texCoordIndex == texCoord) roughSlot = textureSlot(*material.pbrData.metallicRoughnessTexture, 2, roughness);
                else result.warnings.push_back("карта шероховатости пропущена: другой набор UV");
            }
        }

        const fastgltf::Accessor* uvAccessor = nullptr;
        if (slot >= 0 || normalSlot >= 0 || roughSlot >= 0 || emissiveSlot >= 0) {
            const auto* uvAttr = prim.findAttribute("TEXCOORD_" + std::to_string(texCoord));
            if (uvAttr != prim.attributes.end() && gltf.accessors[uvAttr->accessorIndex].count == posAccessor.count) {
                uvAccessor = &gltf.accessors[uvAttr->accessorIndex];
            } else {
                result.warnings.push_back("у примитива нет UV для текстуры: текстура пропущена");
                slot = normalSlot = roughSlot = emissiveSlot = -1;
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
        const auto* colorAttr = prim.findAttribute("COLOR_0");
        if (colorAttr != prim.attributes.end()) {
            const auto& accessor = gltf.accessors[colorAttr->accessorIndex];
            if (accessor.count != posAccessor.count ||
                (accessor.type != fastgltf::AccessorType::Vec3 && accessor.type != fastgltf::AccessorType::Vec4)) {
                error = "COLOR_0: ожидается VEC3/VEC4 с числом элементов POSITION";
                return false;
            }
            const auto apply = [&](const auto& value, std::size_t i) {
                auto& vertex = out.vertices[base + i];
                for (int channel = 0; channel < 3; ++channel) vertex.color[channel] *= value[channel];
            };
            // glTF хранит цвета в линейном пространстве; iterateAccessor разворачивает normalized UNORM.
            // Альфа вершины пока не представлена в Vertex, RGB сохраняется и у VEC4.
            if (accessor.type == fastgltf::AccessorType::Vec3)
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(gltf, accessor, apply);
            else
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, accessor, apply);
        }
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
        if (!out.submeshes.empty() && out.submeshes.back().texture == slot && out.submeshes.back().metallic == metallic &&
            out.submeshes.back().roughness == roughness && out.submeshes.back().normalTexture == normalSlot && out.submeshes.back().roughnessTexture == roughSlot &&
            out.submeshes.back().alphaMode == alphaMode && out.submeshes.back().alphaCutoff == alphaCutoff && out.submeshes.back().doubleSided == doubleSided &&
            out.submeshes.back().opacity == opacity && out.submeshes.back().emissiveTexture == emissiveSlot &&
            std::equal(std::begin(emissive), std::end(emissive), std::begin(out.submeshes.back().emissive)))
            out.submeshes.back().indexCount += count;  // соседний примитив с тем же материалом — одна часть
        else {
            out.submeshes.push_back({static_cast<std::uint32_t>(firstIndex), count, slot, metallic, roughness, normalSlot, roughSlot, alphaCutoff, alphaMode, doubleSided});
            auto& part = out.submeshes.back();
            part.opacity = opacity;
            part.emissiveTexture = emissiveSlot;
            std::copy(std::begin(emissive), std::end(emissive), std::begin(part.emissive));
        }
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
    fastgltf::Parser parser(fastgltf::Extensions::KHR_materials_emissive_strength);
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
    // Если все части с материалом по умолчанию и без текстуры, список частей не нужен: весь меш — одна часть.
    bool anyCustom = false;
    for (const render::Submesh& s : result.mesh.submeshes)
        anyCustom = anyCustom || s.texture >= 0 || s.normalTexture >= 0 || s.roughnessTexture >= 0 || s.emissiveTexture >= 0 ||
            s.emissive[0] > 0 || s.emissive[1] > 0 || s.emissive[2] > 0 || s.metallic != 0.0f || s.roughness != 0.5f || s.doubleSided ||
            s.alphaMode == render::AlphaMode::Mask || s.alphaMode == render::AlphaMode::Blend;
    if (!anyCustom) result.mesh.submeshes.clear();
    out = std::move(result);
    return true;
}

}  // namespace han::tools
