#include "han/render/mesh_io.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace han::render {
namespace {

constexpr char kMagic[4] = {'H', 'M', 'S', 'H'};
constexpr std::uint32_t kMaxElements = 1u << 28;  // защита от заведомо повреждённых заголовков
constexpr std::uint32_t kMaxSubmeshes = 1u << 16;
constexpr std::uint32_t kMaxTextures = 1u << 12;

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void putFloat(std::vector<std::uint8_t>& out, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(out, v);
}

// Чтение с проверкой границ: любая попытка выйти за конец данных отмечается и дальше возвращает нули.
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    bool ok() const { return ok_; }
    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return size_ - pos_; }
    std::uint32_t u32() {
        if (!take(4)) return 0;
        const std::uint8_t* p = data_ + pos_ - 4;
        return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 |
               static_cast<std::uint32_t>(p[3]) << 24;
    }
    std::uint32_t u16() {
        if (!take(2)) return 0;
        const std::uint8_t* p = data_ + pos_ - 2;
        return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8;
    }
    const std::uint8_t* bytes(std::size_t n) { return take(n) ? data_ + pos_ - n : nullptr; }
    bool skip(std::size_t n) { return take(n); }

private:
    bool take(std::size_t n) {
        if (!ok_ || n > size_ - pos_) {
            ok_ = false;
            return false;
        }
        pos_ += n;
        return true;
    }
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

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
    static_assert(sizeof(Vertex) == 44, "формат .hmesh v2 рассчитан на вершину в 44 байта");
    std::vector<std::uint8_t> out;
    out.reserve(kMeshHeaderSize + mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * 4 + 64);
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

    put32(out, static_cast<std::uint32_t>(mesh.submeshes.size()));
    for (const Submesh& s : mesh.submeshes) {
        put32(out, s.firstIndex);
        put32(out, s.indexCount);
        put32(out, static_cast<std::uint32_t>(s.texture));
    }
    put32(out, static_cast<std::uint32_t>(mesh.textures.size()));
    for (const std::string& name : mesh.textures) {
        const auto len = static_cast<std::uint16_t>(std::min<std::size_t>(name.size(), 0xFFFF));
        out.push_back(static_cast<std::uint8_t>(len & 0xFF));
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        out.insert(out.end(), name.begin(), name.begin() + len);
    }
    return out;
}

bool deserializeMesh(const std::uint8_t* data, std::size_t size, MeshData& out, std::string& error) {
    if (size < kMeshHeaderSize) {
        error = "файл короче заголовка";
        return false;
    }
    Reader r(data, size);
    const std::uint8_t* magic = r.bytes(4);
    if (!magic || std::memcmp(magic, kMagic, 4) != 0) {
        error = "это не .hmesh (неверная магия)";
        return false;
    }
    const std::uint32_t version = r.u32();
    if (version != kMeshFormatVersion) {
        error = "неподдерживаемая версия формата: " + std::to_string(version) + " (нужна " + std::to_string(kMeshFormatVersion) +
                "), сварите меш заново";
        return false;
    }
    const std::uint32_t vertexCount = r.u32(), indexCount = r.u32(), stride = r.u32();
    r.skip(24);  // ограничивающий бокс: при чтении пересчитывается из вершин
    if (stride != sizeof(Vertex)) {
        error = "размер вершины не совпадает с ожидаемым";
        return false;
    }
    if (vertexCount > kMaxElements || indexCount > kMaxElements || indexCount % 3 != 0) {
        error = "некорректное число вершин или индексов";
        return false;
    }
    const std::uint64_t payload = static_cast<std::uint64_t>(vertexCount) * stride + static_cast<std::uint64_t>(indexCount) * 4;
    if (payload > r.remaining()) {
        error = "файл обрезан: не хватает вершин или индексов";
        return false;
    }

    MeshData mesh;
    mesh.vertices.resize(vertexCount);
    if (vertexCount) std::memcpy(mesh.vertices.data(), r.bytes(static_cast<std::size_t>(vertexCount) * stride), static_cast<std::size_t>(vertexCount) * stride);
    mesh.indices.resize(indexCount);
    for (std::uint32_t i = 0; i < indexCount; ++i) {
        mesh.indices[i] = r.u32();
        if (mesh.indices[i] >= vertexCount) {
            error = "индекс " + std::to_string(i) + " указывает за пределы вершин";
            return false;
        }
    }

    const std::uint32_t submeshCount = r.u32();
    if (!r.ok() || submeshCount > kMaxSubmeshes || static_cast<std::uint64_t>(submeshCount) * 12 > r.remaining()) {
        error = "повреждён список частей меша";
        return false;
    }
    mesh.submeshes.resize(submeshCount);
    for (Submesh& s : mesh.submeshes) {
        s.firstIndex = r.u32();
        s.indexCount = r.u32();
        s.texture = static_cast<std::int32_t>(r.u32());
    }
    const std::uint32_t textureCount = r.u32();
    if (!r.ok() || textureCount > kMaxTextures) {
        error = "повреждён список текстур";
        return false;
    }
    for (std::uint32_t i = 0; i < textureCount; ++i) {
        const std::uint32_t len = r.u16();
        const std::uint8_t* name = r.bytes(len);
        if (!r.ok()) {
            error = "обрезано имя текстуры";
            return false;
        }
        mesh.textures.emplace_back(reinterpret_cast<const char*>(name), len);
    }
    if (r.remaining() != 0) {
        error = "лишние данные в конце файла";
        return false;
    }

    std::uint64_t covered = 0;
    for (const Submesh& s : mesh.submeshes) {
        if (s.indexCount % 3 != 0 || s.firstIndex != covered || static_cast<std::uint64_t>(s.firstIndex) + s.indexCount > indexCount) {
            error = "части меша не покрывают индексы подряд";
            return false;
        }
        if (s.texture < -1 || s.texture >= static_cast<std::int32_t>(textureCount)) {
            error = "часть меша ссылается на несуществующую текстуру";
            return false;
        }
        covered += s.indexCount;
    }
    if (!mesh.submeshes.empty() && covered != indexCount) {
        error = "части меша не покрывают все индексы";
        return false;
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
