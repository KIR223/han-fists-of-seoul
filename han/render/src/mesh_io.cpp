#include "han/render/mesh_io.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace han::render {
namespace {

constexpr char kMagic[4] = {'H', 'M', 'S', 'H'};
constexpr std::uint32_t kMaxElements = 1u << 28;  // защита от заведомо повреждённых заголовков

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void putFloat(std::vector<std::uint8_t>& out, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(out, v);
}
std::uint32_t get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 |
           static_cast<std::uint32_t>(p[3]) << 24;
}

}  // namespace

MeshBounds computeBounds(const MeshData& mesh) {
    if (mesh.vertices.empty()) return {};
    constexpr float inf = std::numeric_limits<float>::infinity();
    MeshBounds b{{inf, inf, inf}, {-inf, -inf, -inf}};
    for (const Vertex& v : mesh.vertices) {
        b.min = {std::min(b.min.x, v.position[0]), std::min(b.min.y, v.position[1]), std::min(b.min.z, v.position[2])};
        b.max = {std::max(b.max.x, v.position[0]), std::max(b.max.y, v.position[1]), std::max(b.max.z, v.position[2])};
    }
    return b;
}

std::vector<std::uint8_t> serializeMesh(const MeshData& mesh) {
    static_assert(sizeof(Vertex) == 36, "формат .hmesh v1 рассчитан на вершину в 36 байт");
    std::vector<std::uint8_t> out;
    out.reserve(kMeshHeaderSize + mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * 4);
    out.insert(out.end(), kMagic, kMagic + 4);
    put32(out, kMeshFormatVersion);
    put32(out, static_cast<std::uint32_t>(mesh.vertices.size()));
    put32(out, static_cast<std::uint32_t>(mesh.indices.size()));
    put32(out, static_cast<std::uint32_t>(sizeof(Vertex)));
    const MeshBounds b = computeBounds(mesh);
    for (const float f : {b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z}) putFloat(out, f);
    const auto* vb = reinterpret_cast<const std::uint8_t*>(mesh.vertices.data());
    out.insert(out.end(), vb, vb + mesh.vertices.size() * sizeof(Vertex));
    for (const std::uint32_t i : mesh.indices) put32(out, i);
    return out;
}

bool deserializeMesh(const std::uint8_t* data, std::size_t size, MeshData& out, std::string& error) {
    if (size < kMeshHeaderSize) {
        error = "файл короче заголовка";
        return false;
    }
    if (std::memcmp(data, kMagic, 4) != 0) {
        error = "это не .hmesh (неверная магия)";
        return false;
    }
    const std::uint32_t version = get32(data + 4);
    if (version != kMeshFormatVersion) {
        error = "неподдерживаемая версия формата: " + std::to_string(version);
        return false;
    }
    const std::uint32_t vertexCount = get32(data + 8), indexCount = get32(data + 12), stride = get32(data + 16);
    if (stride != sizeof(Vertex)) {
        error = "размер вершины не совпадает с ожидаемым";
        return false;
    }
    if (vertexCount > kMaxElements || indexCount > kMaxElements || indexCount % 3 != 0) {
        error = "некорректное число вершин или индексов";
        return false;
    }
    const std::uint64_t expected =
        kMeshHeaderSize + static_cast<std::uint64_t>(vertexCount) * stride + static_cast<std::uint64_t>(indexCount) * 4;
    if (expected != size) {
        error = "размер файла не совпадает с заголовком";
        return false;
    }

    MeshData mesh;
    mesh.vertices.resize(vertexCount);
    if (vertexCount) std::memcpy(mesh.vertices.data(), data + kMeshHeaderSize, static_cast<std::size_t>(vertexCount) * stride);
    mesh.indices.resize(indexCount);
    const std::uint8_t* ip = data + kMeshHeaderSize + static_cast<std::size_t>(vertexCount) * stride;
    for (std::uint32_t i = 0; i < indexCount; ++i) {
        mesh.indices[i] = get32(ip + static_cast<std::size_t>(i) * 4);
        if (mesh.indices[i] >= vertexCount) {
            error = "индекс " + std::to_string(i) + " указывает за пределы вершин";
            return false;
        }
    }
    out = std::move(mesh);
    return true;
}

bool saveMeshFile(const std::filesystem::path& path, const MeshData& mesh, std::string& error) {
    const std::vector<std::uint8_t> bytes = serializeMesh(mesh);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "не удалось открыть для записи: " + path.string();
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "ошибка записи: " + path.string();
        return false;
    }
    return true;
}

bool loadMeshFile(const std::filesystem::path& path, MeshData& out, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "не удалось открыть: " + path.string();
        return false;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        error = "ошибка чтения: " + path.string();
        return false;
    }
    return deserializeMesh(bytes.data(), bytes.size(), out, error);
}

}  // namespace han::render
