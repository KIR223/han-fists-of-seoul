#pragma once
// Текстура на стороне CPU (E3): RGBA8 с цепочкой мип-уровней. Готовится han_cooker, хранится в .ktx2, грузится рендерером.
#include <cstdint>
#include <vector>

namespace han::render {

struct TextureData {
    std::uint32_t width = 0, height = 0;
    bool srgb = true;  // цвет (альбедо) — sRGB; нормали, шероховатость и т. п. — линейные
    // mips[0] — полный размер (width*height*4 байта), каждый следующий вдвое меньше по сторонам, до 1x1.
    std::vector<std::vector<std::uint8_t>> mips;

    bool valid() const;  // размеры положительны и размер каждого уровня сходится с шириной и высотой
};

// Число уровней полной цепочки для заданных размеров (до 1x1).
std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height);
// Создаёт текстуру с одним уровнем из пикселей RGBA8 (width*height*4 байта).
TextureData makeTexture(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> rgba, bool srgb);
// Достраивает цепочку до 1x1 усреднением блоков 2x2; для sRGB усредняет в линейном свете. Нулевой уровень не меняется.
void generateMips(TextureData& texture);
// Шахматная доска для тестовых сцен; клетка — cell пикселей.
TextureData makeCheckerTexture(std::uint32_t size, std::uint32_t cell, std::uint32_t colorA, std::uint32_t colorB);

}  // namespace han::render
// Пример: auto t = makeCheckerTexture(256, 32, 0xFFB0B0B0, 0xFF404040); generateMips(t); renderer->uploadTexture(t);
