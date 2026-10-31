#include "han/tools/image_decode.h"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO        // файлы читаем сами, декодер видит только память
#define STBI_MAX_DIMENSIONS (1 << 14)
#include <stb_image.h>

namespace han::tools {

bool decodeImage(const std::uint8_t* data, std::size_t size, DecodedImage& out, std::string& error) {
    if (size == 0 || size > 0x7FFFFFFF) {
        error = "пустое или слишком большое изображение";
        return false;
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels) {
        error = std::string("не удалось декодировать изображение: ") + stbi_failure_reason();
        return false;
    }
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    out.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}

}  // namespace han::tools
