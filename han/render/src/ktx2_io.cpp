#include "han/render/ktx2_io.h"

#include <cstring>
#include <fstream>

namespace han::render {
namespace {

constexpr std::uint8_t kIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::uint32_t kVkFormatRgba8Unorm = 37;
constexpr std::uint32_t kVkFormatRgba8Srgb = 43;
constexpr std::size_t kHeaderSize = 80;
constexpr std::size_t kLevelIndexEntry = 24;
constexpr std::uint32_t kDfdSize = 92;  // 4 (общий размер) + 24 (заголовок блока) + 4 * 16 (каналы)
constexpr std::uint32_t kMaxDimension = 1u << 16;

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void put64(std::vector<std::uint8_t>& v, std::uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}
void put16(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
}
std::uint32_t get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 |
           static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint64_t get64(const std::uint8_t* p) { return static_cast<std::uint64_t>(get32(p)) | static_cast<std::uint64_t>(get32(p + 4)) << 32; }

// Описатель формата данных (DFD) для RGBA8 по спецификации Khronos Data Format: четыре канала по 8 бит.
void putDfd(std::vector<std::uint8_t>& v, bool srgb) {
    put32(v, kDfdSize);
    put32(v, 0);                                  // vendorId (17 бит) = 0 (Khronos), descriptorType (15 бит) = 0 (basic)
    put16(v, 2);                                  // versionNumber = 2
    put16(v, kDfdSize - 4);                       // descriptorBlockSize = 88
    v.push_back(1);                               // colorModel = RGBSDA
    v.push_back(1);                               // colorPrimaries = BT.709
    v.push_back(srgb ? 2 : 1);                    // transferFunction: 2 — sRGB, 1 — линейная
    v.push_back(0);                               // flags: альфа не домножена
    for (int i = 0; i < 4; ++i) v.push_back(0);   // texelBlockDimension 1x1x1x1 (значения минус один)
    v.push_back(4);                               // bytesPlane0 = 4
    for (int i = 0; i < 7; ++i) v.push_back(0);   // остальные плоскости
    const std::uint8_t channels[4] = {0, 1, 2, 15};  // R, G, B, A
    for (int c = 0; c < 4; ++c) {
        put16(v, static_cast<std::uint32_t>(c) * 8);  // bitOffset
        v.push_back(7);                               // bitLength - 1
        std::uint8_t type = channels[c];
        if (srgb && c == 3) type |= 0x10;             // альфа в sRGB-формате линейная
        v.push_back(type);
        for (int i = 0; i < 4; ++i) v.push_back(0);   // samplePosition
        put32(v, 0);                                  // sampleLower
        put32(v, 255);                                // sampleUpper
    }
}

}  // namespace

std::vector<std::uint8_t> serializeKtx2(const TextureData& texture) {
    std::vector<std::uint8_t> out;
    if (!texture.valid()) return out;
    const auto levels = static_cast<std::uint32_t>(texture.mips.size());

    const std::uint64_t dfdOffset = kHeaderSize + static_cast<std::uint64_t>(levels) * kLevelIndexEntry;
    std::uint64_t dataStart = dfdOffset + kDfdSize;
    dataStart = (dataStart + 7) & ~std::uint64_t{7};

    // Данные уровней лежат от самого маленького к самому большому (рекомендация спецификации); индекс — от большого к малому.
    std::vector<std::uint64_t> offsets(levels);
    std::uint64_t cursor = dataStart;
    for (std::uint32_t i = levels; i-- > 0;) {
        offsets[i] = cursor;
        cursor += texture.mips[i].size();
        cursor = (cursor + 7) & ~std::uint64_t{7};
    }

    out.insert(out.end(), kIdentifier, kIdentifier + 12);
    put32(out, texture.srgb ? kVkFormatRgba8Srgb : kVkFormatRgba8Unorm);
    put32(out, 1);               // typeSize
    put32(out, texture.width);
    put32(out, texture.height);
    put32(out, 0);               // pixelDepth
    put32(out, 0);               // layerCount
    put32(out, 1);               // faceCount
    put32(out, levels);
    put32(out, 0);               // supercompressionScheme: нет
    put32(out, static_cast<std::uint32_t>(dfdOffset));
    put32(out, kDfdSize);
    put32(out, 0);               // kvdByteOffset
    put32(out, 0);               // kvdByteLength
    put64(out, 0);               // sgdByteOffset
    put64(out, 0);               // sgdByteLength

    for (std::uint32_t i = 0; i < levels; ++i) {
        put64(out, offsets[i]);
        put64(out, texture.mips[i].size());
        put64(out, texture.mips[i].size());
    }
    putDfd(out, texture.srgb);
    out.resize(static_cast<std::size_t>(dataStart), 0);
    for (std::uint32_t i = levels; i-- > 0;) {
        out.insert(out.end(), texture.mips[i].begin(), texture.mips[i].end());
        out.resize(static_cast<std::size_t>((out.size() + 7) & ~std::size_t{7}), 0);
    }
    return out;
}

bool deserializeKtx2(const std::uint8_t* data, std::size_t size, TextureData& out, std::string& error) {
    if (size < kHeaderSize || std::memcmp(data, kIdentifier, 12) != 0) {
        error = "это не KTX2 (неверная сигнатура)";
        return false;
    }
    const std::uint32_t vkFormat = get32(data + 12), width = get32(data + 20), height = get32(data + 24), depth = get32(data + 28),
                        layers = get32(data + 32), faces = get32(data + 36), levelCount = get32(data + 40), supercompression = get32(data + 44);
    if (supercompression != 0 || vkFormat == 0) {
        error = "сжатые текстуры KTX2 (Basis, суперсжатие) пока не поддерживаются загрузчиком";
        return false;
    }
    if (vkFormat != kVkFormatRgba8Srgb && vkFormat != kVkFormatRgba8Unorm) {
        error = "неподдерживаемый формат пикселей KTX2: " + std::to_string(vkFormat);
        return false;
    }
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension || depth > 1 || layers > 1 || faces != 1) {
        error = "неподдерживаемые размеры KTX2 (нужна плоская 2D-текстура)";
        return false;
    }
    const std::uint32_t expectedLevels = mipLevelCount(width, height);
    const std::uint32_t levels = levelCount == 0 ? 1 : levelCount;
    if (levels > expectedLevels) {
        error = "слишком много уровней мипов";
        return false;
    }
    if (static_cast<std::uint64_t>(kHeaderSize) + static_cast<std::uint64_t>(levels) * kLevelIndexEntry > size) {
        error = "обрезан индекс уровней KTX2";
        return false;
    }

    TextureData tex;
    tex.width = width;
    tex.height = height;
    tex.srgb = vkFormat == kVkFormatRgba8Srgb;
    std::uint32_t w = width, h = height;
    for (std::uint32_t i = 0; i < levels; ++i) {
        const std::uint8_t* entry = data + kHeaderSize + static_cast<std::size_t>(i) * kLevelIndexEntry;
        const std::uint64_t offset = get64(entry), length = get64(entry + 8);
        const std::uint64_t expected = static_cast<std::uint64_t>(w) * h * 4;
        if (length != expected) {
            error = "размер уровня " + std::to_string(i) + " не совпадает с размерами текстуры";
            return false;
        }
        if (offset > size || length > size - offset) {
            error = "уровень " + std::to_string(i) + " выходит за пределы файла";
            return false;
        }
        tex.mips.emplace_back(data + offset, data + offset + length);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    out = std::move(tex);
    return true;
}

bool saveKtx2File(const std::filesystem::path& path, const TextureData& texture, std::string& error) {
    const std::vector<std::uint8_t> bytes = serializeKtx2(texture);
    if (bytes.empty()) {
        error = "текстура некорректна (размеры или уровни не сходятся)";
        return false;
    }
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

bool loadKtx2File(const std::filesystem::path& path, TextureData& out, std::string& error) {
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
    return deserializeKtx2(bytes.data(), bytes.size(), out, error);
}

}  // namespace han::render
