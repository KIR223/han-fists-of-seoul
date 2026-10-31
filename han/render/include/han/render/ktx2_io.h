#pragma once
// Запись и чтение KTX2 (E3). Поддерживается несжатый RGBA8 (VK_FORMAT_R8G8B8A8_SRGB / _UNORM) с мип-уровнями, без суперсжатия,
// без массивов и кубов. Файлы стандартные и открываются любыми инструментами KTX2. Сжатие Basis Universal добавится отдельно
// (через libktx в han_cooker и транскодирование при загрузке); чтение сжатых файлов здесь отклоняется с понятной ошибкой.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "han/render/texture.h"

namespace han::render {

std::vector<std::uint8_t> serializeKtx2(const TextureData& texture);
bool deserializeKtx2(const std::uint8_t* data, std::size_t size, TextureData& out, std::string& error);

bool saveKtx2File(const std::filesystem::path& path, const TextureData& texture, std::string& error);
bool loadKtx2File(const std::filesystem::path& path, TextureData& out, std::string& error);

}  // namespace han::render
// Пример: TextureData t; std::string err; if (loadKtx2File("lamp.tex0.ktx2", t, err)) renderer->uploadTexture(t);
