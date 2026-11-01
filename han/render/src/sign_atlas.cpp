#include "han/render/sign_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "han/core/json.h"
#ifdef HAN_WITH_STB_TRUETYPE
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#endif

namespace han::render {
namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Значение атрибута name="..." внутри текста тега (пусто — нет).
std::string attr(const std::string& tag, const std::string& name) {
    const std::string key = " " + name + "=\"";
    const std::size_t p = tag.find(key);
    if (p == std::string::npos) return {};
    const std::size_t s = p + key.size(), e = tag.find('"', s);
    return e == std::string::npos ? std::string() : tag.substr(s, e - s);
}
float num(const std::string& tag, const char* name, float fallback = 0.f) {
    const std::string v = attr(tag, name);
    return v.empty() ? fallback : static_cast<float>(std::atof(v.c_str()));
}

struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
};
bool parseColor(const std::string& s, Rgba& c) {
    if (s.size() != 7 || s[0] != '#') return false;
    const auto h = [&](int i) { return static_cast<float>(std::strtol(s.substr(static_cast<std::size_t>(i), 2).c_str(), nullptr, 16)) / 255.f; };
    c = {h(1), h(3), h(5), 1.f};
    return true;
}

struct Shape {
    enum Kind { Rect, Circle, Ellipse, Poly } kind = Rect;
    float a = 0, b = 0, c = 0, d = 0, rx = 0;  // rect: x y w h rx; circle: cx cy r; ellipse: cx cy rx ry
    std::vector<std::pair<float, float>> pts;
    Rgba color;
};

bool inside(const Shape& s, float x, float y) {
    switch (s.kind) {
        case Shape::Rect: {
            if (x < s.a || y < s.b || x > s.a + s.c || y > s.b + s.d) return false;
            if (s.rx <= 0.f) return true;
            const float cx = std::clamp(x, s.a + s.rx, s.a + s.c - s.rx), cy = std::clamp(y, s.b + s.rx, s.b + s.d - s.rx);
            return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= s.rx * s.rx;
        }
        case Shape::Circle: return (x - s.a) * (x - s.a) + (y - s.b) * (y - s.b) <= s.c * s.c;
        case Shape::Ellipse: {
            const float dx = (x - s.a) / std::max(0.001f, s.c), dy = (y - s.b) / std::max(0.001f, s.d);
            return dx * dx + dy * dy <= 1.f;
        }
        case Shape::Poly: {
            bool in = false;
            for (std::size_t i = 0, j = s.pts.size() - 1; i < s.pts.size(); j = i++)
                if ((s.pts[i].second > y) != (s.pts[j].second > y) && x < (s.pts[j].first - s.pts[i].first) * (y - s.pts[i].second) / (s.pts[j].second - s.pts[i].second) + s.pts[i].first)
                    in = !in;
            return in;
        }
    }
    return false;
}

std::uint32_t pack(float r, float g, float b, float a = 1.f) {
    const auto u = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return u(a) << 24 | u(b) << 16 | u(g) << 8 | u(r);
}

float luminance(std::uint32_t c) { return (0.299f * static_cast<float>(c & 255) + 0.587f * static_cast<float>((c >> 8) & 255) + 0.114f * static_cast<float>((c >> 16) & 255)) / 255.f; }

#ifdef HAN_WITH_STB_TRUETYPE
// UTF-8 → кодовые точки.
std::vector<int> decodeUtf8(const std::string& s) {
    std::vector<int> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int cp = 0, n = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6) { cp = c & 31; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 15; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 7; n = 4; }
        for (int k = 1; k < n && i + static_cast<std::size_t>(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 63);
        out.push_back(cp);
        i += static_cast<std::size_t>(n);
    }
    return out;
}
#endif
}  // namespace

bool rasterizeSvg(const std::string& svg, int size, std::vector<std::uint8_t>& rgba, std::string& error) {
    std::vector<Shape> shapes;
    for (std::size_t p = 0; (p = svg.find('<', p)) != std::string::npos;) {
        const std::size_t e = svg.find('>', p);
        if (e == std::string::npos) break;
        const std::string tag = svg.substr(p, e - p + 1);
        p = e + 1;
        Shape s;
        if (tag.rfind("<rect", 0) == 0) {
            s.kind = Shape::Rect;
            s.a = num(tag, "x");
            s.b = num(tag, "y");
            s.c = num(tag, "width");
            s.d = num(tag, "height");
            s.rx = num(tag, "rx");
        } else if (tag.rfind("<circle", 0) == 0) {
            s.kind = Shape::Circle;
            s.a = num(tag, "cx");
            s.b = num(tag, "cy");
            s.c = num(tag, "r");
        } else if (tag.rfind("<ellipse", 0) == 0) {
            s.kind = Shape::Ellipse;
            s.a = num(tag, "cx");
            s.b = num(tag, "cy");
            s.c = num(tag, "rx");
            s.d = num(tag, "ry");
        } else if (tag.rfind("<polygon", 0) == 0) {
            s.kind = Shape::Poly;
            std::istringstream in(attr(tag, "points"));
            std::string pair;
            while (in >> pair) {
                const std::size_t comma = pair.find(',');
                if (comma == std::string::npos) {
                    error = "polygon: точка вида x,y";
                    return false;
                }
                s.pts.emplace_back(static_cast<float>(std::atof(pair.substr(0, comma).c_str())), static_cast<float>(std::atof(pair.substr(comma + 1).c_str())));
            }
            if (s.pts.size() < 3) {
                error = "polygon: минимум три точки";
                return false;
            }
        } else {
            continue;
        }
        if (!parseColor(attr(tag, "fill"), s.color)) {
            error = "нужен fill вида #RRGGBB";
            return false;
        }
        if (!attr(tag, "opacity").empty()) s.color.a = num(tag, "opacity", 1.f);
        shapes.push_back(std::move(s));
    }
    if (shapes.empty()) {
        error = "в SVG нет поддерживаемых фигур";
        return false;
    }
    rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);
    constexpr int kSub = 3;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < kSub; ++sy)
                for (int sx = 0; sx < kSub; ++sx) {
                    const float u = (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / kSub) / static_cast<float>(size) * 64.f;
                    const float v = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kSub) / static_cast<float>(size) * 64.f;
                    float pr = 0, pg = 0, pb = 0, pa = 0;  // сверху вниз по порядку фигур («поверх»)
                    for (const Shape& s : shapes)
                        if (inside(s, u, v)) {
                            pr = pr * (1.f - s.color.a) + s.color.r * s.color.a;
                            pg = pg * (1.f - s.color.a) + s.color.g * s.color.a;
                            pb = pb * (1.f - s.color.a) + s.color.b * s.color.a;
                            pa = pa + s.color.a * (1.f - pa);
                        }
                    r += pr;
                    g += pg;
                    b += pb;
                    a += pa;
                }
            const float n = static_cast<float>(kSub * kSub);
            std::uint8_t* px = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4];
            const std::uint32_t c = pack(r / n, g / n, b / n, a / n);
            std::memcpy(px, &c, 4);
        }
    return true;
}

bool loadSignBrands(const std::string& brandsJson, const std::string& svgRoot, const std::function<std::string(const std::string&)>& nameOf,
                    std::vector<SignBrand>& out, std::string& error) {
    core::Json json;
    std::string err;
    if (!core::loadJsonFile(brandsJson, json, err)) {
        error = err;
        return false;
    }
    if (!json.isArray()) {
        error = brandsJson + ": ожидался массив брендов";
        return false;
    }
    for (const core::Json& b : json.items()) {
        SignBrand s;
        s.id = b["id"].asString();
        if (s.id.empty() || b["fictional"].asBool(false) != true) {
            error = brandsJson + ": у бренда нужны id и fictional: true";
            return false;
        }
        s.text = nameOf(b["name_key"].asString());
        const core::Json& colors = b["colors"];
        Rgba c0, c1;
        if (!parseColor(colors.at(0).asString(), c0)) {
            error = s.id + ": цвета вида #RRGGBB";
            return false;
        }
        c1 = c0;
        if (colors.size() > 1 && !parseColor(colors.at(1).asString(), c1)) {
            error = s.id + ": цвета вида #RRGGBB";
            return false;
        }
        s.background = pack(c0.r, c0.g, c0.b);
        s.foreground = pack(c1.r, c1.g, c1.b);
        if (!rasterizeSvg(readFile(svgRoot + "/" + b["logo"].asString()), 48, s.logo, err)) {
            error = s.id + ": " + err;
            return false;
        }
        out.push_back(std::move(s));
    }
    return true;
}

TextureData buildStorefrontAtlas(const std::vector<SignBrand>& brands, const std::string& fontPath, int bays) {
    constexpr int H = 256, bay = 256;
    const int W = bays * bay;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(W) * H * 4);
    const float wallTone[4][3] = {{150, 140, 128}, {120, 118, 124}, {160, 128, 110}, {112, 126, 120}};
#ifdef HAN_WITH_STB_TRUETYPE
    const std::string fontData = fontPath.empty() ? std::string() : readFile(fontPath);
    stbtt_fontinfo font;
    const bool haveFont = !fontData.empty() && stbtt_InitFont(&font, reinterpret_cast<const unsigned char*>(fontData.data()), 0) != 0;
#else
    (void)fontPath;
    const bool haveFont = false;
#endif
    for (int b = 0; b < bays; ++b) {
        const SignBrand* brand = brands.empty() ? nullptr : &brands[static_cast<std::size_t>(b) % brands.size()];
        const float* tone = wallTone[b % 4];
        // Секция: стена, вывеска, навес, витрина.
        for (int y = 0; y < H; ++y)
            for (int lx = 0; lx < bay; ++lx) {
                float r = tone[0], g = tone[1], bl = tone[2];
                std::uint8_t a = 255;
                const std::uint32_t sign = brand ? brand->background : 0xFF3030D0;
                if (y < 52) {
                    if (lx > 10 && lx < bay - 10 && y > 8 && y < 44) {
                        r = static_cast<float>(sign & 255);
                        g = static_cast<float>((sign >> 8) & 255);
                        bl = static_cast<float>((sign >> 16) & 255);
                        a = 110;  // фон вывески светится слабее текста
                    } else {
                        r = 60;
                        g = 60;
                        bl = 64;
                    }
                } else if (y < 70) {
                    r = static_cast<float>(sign & 255) * 0.55f;
                    g = static_cast<float>((sign >> 8) & 255) * 0.55f;
                    bl = static_cast<float>((sign >> 16) & 255) * 0.55f;
                } else if (lx > 22 && lx < bay - 22 && y > 84 && y < 238) {
                    const float k = static_cast<float>(y - 84) / 154.f;
                    const bool mullion = lx > bay / 2 - 3 && lx < bay / 2 + 3;
                    if (mullion) {
                        r = 200;
                        g = 198;
                        bl = 190;
                    } else {
                        r = 70 - 25 * k;
                        g = 82 - 25 * k;
                        bl = 92 - 25 * k;
                        a = 90;  // витрина светится мягче текста вывески
                    }
                } else if (y > 238) {
                    r *= 0.6f;
                    g *= 0.6f;
                    bl *= 0.6f;
                }
                std::uint8_t* p = &px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(b * bay + lx)) * 4];
                p[0] = static_cast<std::uint8_t>(std::min(255.f, r));
                p[1] = static_cast<std::uint8_t>(std::min(255.f, g));
                p[2] = static_cast<std::uint8_t>(std::min(255.f, bl));
                p[3] = a;
            }
        if (!brand) continue;
        const auto put = [&](int x, int y, std::uint32_t c, float cover) {
            if (x < 0 || y < 0 || x >= W || y >= H || cover <= 0.f) return;
            std::uint8_t* p = &px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)) * 4];
            for (int k = 0; k < 3; ++k) p[k] = static_cast<std::uint8_t>(p[k] * (1.f - cover) + static_cast<float>((c >> (8 * k)) & 255) * cover);
            p[3] = static_cast<std::uint8_t>(static_cast<float>(p[3]) * (1.f - cover));  // под текстом и эмблемой — полное свечение
        };
        // Эмблема: 32 x 32 слева на вывеске.
        const int ex = b * bay + 16, ey = 10;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const int sx = x * 48 / 32, sy = y * 48 / 32;
                const std::uint8_t* lp = &brand->logo[(static_cast<std::size_t>(sy) * 48 + static_cast<std::size_t>(sx)) * 4];
                std::uint32_t c;
                std::memcpy(&c, lp, 4);
                put(ex + x, ey + y, c, static_cast<float>(lp[3]) / 255.f);
            }
#ifdef HAN_WITH_STB_TRUETYPE
        if (haveFont && !brand->text.empty()) {
            // Название: цвет передний, если контрастный к фону вывески, иначе белый или чёрный.
            std::uint32_t textColor = brand->foreground;
            if (std::fabs(luminance(textColor) - luminance(brand->background)) < 0.35f) textColor = luminance(brand->background) > 0.5f ? 0xFF101010u : 0xFFFFFFFFu;
            const std::vector<int> cps = decodeUtf8(brand->text);
            const float availableWidth = static_cast<float>(bay - 20 - 40 - 8);  // вывеска минус поля и эмблема
            float px_h = 26.f, width = 0.f;
            for (int pass = 0; pass < 6; ++pass) {  // подбираем кегль, чтобы название помещалось
                const float scale = stbtt_ScaleForPixelHeight(&font, px_h);
                width = 0.f;
                for (int cp : cps) {
                    int adv, lsb;
                    stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
                    width += static_cast<float>(adv) * scale;
                }
                if (width <= availableWidth) break;
                px_h *= 0.88f;
            }
            const float scale = stbtt_ScaleForPixelHeight(&font, px_h);
            int ascent, descent, gap;
            stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
            const int baseline = 8 + static_cast<int>((36.f + static_cast<float>(ascent - descent) * scale) * 0.5f) - 2 + static_cast<int>(static_cast<float>(descent) * scale * 0.f);
            float x = static_cast<float>(b * bay + 10 + 6 + 36);
            for (int cp : cps) {
                int adv, lsb, x0, y0, x1, y1;
                stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
                stbtt_GetCodepointBitmapBox(&font, cp, scale, scale, &x0, &y0, &x1, &y1);
                const int w = x1 - x0, h = y1 - y0;
                if (w > 0 && h > 0) {
                    std::vector<unsigned char> bmp(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
                    stbtt_MakeCodepointBitmap(&font, bmp.data(), w, h, w, scale, scale, cp);
                    for (int yy = 0; yy < h; ++yy)
                        for (int xx = 0; xx < w; ++xx) {
                            const float cover = static_cast<float>(bmp[static_cast<std::size_t>(yy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(xx)]) / 255.f;
                            put(static_cast<int>(x) + x0 + xx, baseline + y0 + yy, textColor, cover);
                            put(static_cast<int>(x) + x0 + xx + 1, baseline + y0 + yy, textColor, cover * 0.6f);  // слегка «жирнее»
                        }
                }
                x += static_cast<float>(adv) * scale;
            }
        }
#endif
        (void)haveFont;
    }
    TextureData t = makeTexture(static_cast<std::uint32_t>(W), static_cast<std::uint32_t>(H), std::move(px), true);
    generateMips(t);
    return t;
}

}  // namespace han::render
