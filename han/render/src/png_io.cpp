#include "han/render/png_io.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace han::render {
namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {  // PNG хранит числа от старшего байта к младшему
    for (int i = 3; i >= 0; --i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

void putChunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    const auto& table = crcTable();
    std::uint32_t c = crc ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
    constexpr std::uint32_t kMod = 65521;
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

std::vector<std::uint8_t> encodePng(std::uint32_t width, std::uint32_t height, std::uint32_t channels, const std::uint8_t* pixels) {
    if (width == 0 || height == 0 || (channels != 3 && channels != 4) || !pixels) return {};
    // Каждая строка начинается с байта фильтра 0 («без фильтра»).
    std::vector<std::uint8_t> raw;
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    raw.reserve((stride + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels + y * stride, pixels + (y + 1) * stride);
    }

    // zlib-поток из несжатых блоков deflate (по 65535 байт): заголовок 78 01, блоки, Adler-32.
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    put32(z, adler32(raw.data(), raw.size()));

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> header;
    put32(header, width);
    put32(header, height);
    header.push_back(8);                      // бит на канал
    header.push_back(channels == 4 ? 6 : 2);  // тип цвета: RGBA или RGB
    header.push_back(0);                      // сжатие
    header.push_back(0);                      // фильтрация
    header.push_back(0);                      // без чересстрочности
    putChunk(png, "IHDR", header);
    putChunk(png, "IDAT", z);
    putChunk(png, "IEND", {});
    return png;
}

bool writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height, std::uint32_t channels, const std::uint8_t* pixels,
              std::string& error) {
    const std::vector<std::uint8_t> png = encodePng(width, height, channels, pixels);
    if (png.empty()) {
        error = "некорректные размеры или данные изображения";
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "не удалось открыть для записи: " + path.string();
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) {
        error = "ошибка записи: " + path.string();
        return false;
    }
    return true;
}

}  // namespace han::render
