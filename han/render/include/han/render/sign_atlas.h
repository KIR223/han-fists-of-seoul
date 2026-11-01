// Вывески вымышленных брендов (G38): текстура витрин первого этажа из data/city/brands.json.
// Логотипы — свои SVG (прямоугольники, круги, эллипсы, многоугольники в viewBox 64 x 64), названия — шрифтом (stb_truetype), по секции 4,8 м
// на бренд; альфа 0 = «светится ночью» (неон). Без шрифта вывески рисуются без текста, без брендов — не строится.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "han/render/texture.h"

namespace han::render {

struct SignBrand {
    std::string id, text;                  // text — название на вывеске (корейское)
    std::uint32_t background = 0xFF444444, foreground = 0xFFFFFFFF;  // 0xAABBGGRR
    std::vector<std::uint8_t> logo;        // RGBA 48 x 48 (растеризованный SVG)
};

// Читает brands.json (массив объектов схемы brand.schema.json) и SVG-логотипы из svgRoot (assets_src/). nameOf(name_key) — текст вывески.
bool loadSignBrands(const std::string& brandsJson, const std::string& svgRoot, const std::function<std::string(const std::string&)>& nameOf,
                    std::vector<SignBrand>& out, std::string& error);

// Растеризует упрощённый SVG (rect, circle, ellipse, polygon; fill #RRGGBB, opacity) в квадрат size x size RGBA с суперсэмплингом.
bool rasterizeSvg(const std::string& svgText, int size, std::vector<std::uint8_t>& rgba, std::string& error);

// Строит текстуру витрин: bays секций по 256 px (ширина bays * 256, высота 256 = 3,6 м), бренды раздаются по кругу. fontPath — .ttf с кириллицей и хангылем.
TextureData buildStorefrontAtlas(const std::vector<SignBrand>& brands, const std::string& fontPath, int bays = 16);

}  // namespace han::render
// Пример: std::vector<SignBrand> b; loadSignBrands("data/city/brands.json", "assets_src", nameOf, b, err); auto t = buildStorefrontAtlas(b, "NotoSansKR.ttf");
