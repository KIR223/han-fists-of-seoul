#pragma once
// Запись PNG без внешних библиотек (E4: скриншот-тест). Данные кладутся несжатыми блоками deflate: файл крупный, зато кодировщик
// простой и проверяемый. Для эталонов (tests/engine/screenshots) файлы пересжимаются Python-скриптом.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace han::render {

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0);
std::uint32_t adler32(const std::uint8_t* data, std::size_t size);

// Кодирует изображение 8 бит на канал: channels = 3 (RGB) или 4 (RGBA), строки сверху вниз без отступов.
std::vector<std::uint8_t> encodePng(std::uint32_t width, std::uint32_t height, std::uint32_t channels, const std::uint8_t* pixels);
bool writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height, std::uint32_t channels, const std::uint8_t* pixels,
              std::string& error);

}  // namespace han::render
// Пример: std::string err; writePng("frame.png", w, h, 4, rgba.data(), err);
