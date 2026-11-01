#include "han/render/texture.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace han::render {
namespace {

// sRGB → линейный свет по таблице (256 значений) и обратно.
const std::array<float, 256>& srgbToLinearTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            t[static_cast<std::size_t>(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table;
}

std::uint8_t linearToSrgb(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    const float c = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(c * 255.0f));
}

std::uint32_t half(std::uint32_t v) { return std::max<std::uint32_t>(1, v / 2); }

}  // namespace

bool TextureData::valid() const {
    if (width == 0 || height == 0 || mips.empty()) return false;
    std::uint32_t w = width, h = height;
    for (const auto& level : mips) {
        if (level.size() != static_cast<std::size_t>(w) * h * 4) return false;
        w = half(w);
        h = half(h);
    }
    return true;
}

std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height) {
    std::uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = half(width);
        height = half(height);
        ++levels;
    }
    return levels;
}

TextureData makeTexture(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> rgba, bool srgb) {
    TextureData t;
    t.width = width;
    t.height = height;
    t.srgb = srgb;
    t.mips.push_back(std::move(rgba));
    return t;
}

void generateMips(TextureData& texture) {
    if (texture.width == 0 || texture.height == 0 || texture.mips.empty() ||
        texture.mips[0].size() != static_cast<std::size_t>(texture.width) * texture.height * 4)
        return;
    texture.mips.resize(1);
    std::uint32_t w = texture.width, h = texture.height;
    const auto& toLinear = srgbToLinearTable();
    while (w > 1 || h > 1) {
        const std::uint32_t nw = half(w), nh = half(h);
        const std::vector<std::uint8_t>& src = texture.mips.back();
        std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
        for (std::uint32_t y = 0; y < nh; ++y) {
            for (std::uint32_t x = 0; x < nw; ++x) {
                const std::uint32_t x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
                const std::uint32_t y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                const std::uint32_t xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1};
                float sum[4] = {0, 0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    const std::uint8_t* p = &src[(static_cast<std::size_t>(ys[k]) * w + xs[k]) * 4];
                    for (int c = 0; c < 3; ++c) sum[c] += texture.srgb ? toLinear[p[c]] : static_cast<float>(p[c]) / 255.0f;
                    sum[3] += static_cast<float>(p[3]) / 255.0f;  // альфа всегда линейная
                }
                std::uint8_t* q = &dst[(static_cast<std::size_t>(y) * nw + x) * 4];
                for (int c = 0; c < 3; ++c) {
                    const float avg = sum[c] * 0.25f;
                    q[c] = texture.srgb ? linearToSrgb(avg) : static_cast<std::uint8_t>(std::lround(std::clamp(avg, 0.0f, 1.0f) * 255.0f));
                }
                q[3] = static_cast<std::uint8_t>(std::lround(std::clamp(sum[3] * 0.25f, 0.0f, 1.0f) * 255.0f));
            }
        }
        texture.mips.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
}

TextureData makeCheckerTexture(std::uint32_t size, std::uint32_t cell, std::uint32_t colorA, std::uint32_t colorB) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y)
        for (std::uint32_t x = 0; x < size; ++x) {
            const std::uint32_t color = ((x / cell) + (y / cell)) % 2 == 0 ? colorA : colorB;  // 0xAABBGGRR → байты R, G, B, A
            std::uint8_t* p = &px[(static_cast<std::size_t>(y) * size + x) * 4];
            p[0] = static_cast<std::uint8_t>(color & 0xFF);
            p[1] = static_cast<std::uint8_t>((color >> 8) & 0xFF);
            p[2] = static_cast<std::uint8_t>((color >> 16) & 0xFF);
            p[3] = static_cast<std::uint8_t>((color >> 24) & 0xFF);
        }
    return makeTexture(size, size, std::move(px), true);
}

}  // namespace han::render
