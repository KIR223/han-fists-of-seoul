#pragma once
// Декодирование PNG/JPEG/TGA/BMP в RGBA8 (E3) на stb_image. Доступно, если собрано с HAN_WITH_STB.
#ifdef HAN_WITH_STB

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace han::tools {

struct DecodedImage {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4
};

// false — формат не опознан или данные повреждены; причина в error. Размер ограничен (защита от «бомб» вроде 100000x100000).
bool decodeImage(const std::uint8_t* data, std::size_t size, DecodedImage& out, std::string& error);

}  // namespace han::tools
// Пример: DecodedImage img; std::string err; if (decodeImage(bytes.data(), bytes.size(), img, err)) makeTexture(img.width, img.height, img.rgba, true);

#endif  // HAN_WITH_STB
