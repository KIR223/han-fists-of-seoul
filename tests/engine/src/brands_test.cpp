// Тесты вымышленных брендов (G38): данные, SVG-логотипы, названия на трёх языках, запрет реальных брендов, атлас вывесок.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "han/core/json.h"
#include "han/render/sign_atlas.h"
#include "han/ui/localization.h"

using namespace han::render;

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

TEST_CASE("brands: растеризатор SVG — фигуры, порядок слоёв, прозрачность, ошибки") {
    std::vector<std::uint8_t> px;
    std::string error;
    REQUIRE(rasterizeSvg(R"(<svg viewBox="0 0 64 64"><rect x="0" y="0" width="64" height="64" fill="#FF0000"/><circle cx="32" cy="32" r="16" fill="#0000FF"/></svg>)", 32, px, error));
    REQUIRE(px.size() == 32u * 32u * 4u);
    const auto at = [&](int x, int y) { return &px[(static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4]; };
    CHECK(at(1, 1)[0] == 255);   // красный фон
    CHECK(at(1, 1)[2] == 0);
    CHECK(at(16, 16)[2] == 255);  // синий круг поверх
    CHECK(at(16, 16)[0] == 0);
    CHECK(at(1, 1)[3] == 255);
    // многоугольник и эллипс; фон прозрачен вне фигур
    REQUIRE(rasterizeSvg(R"(<svg><polygon points="32,4 60,60 4,60" fill="#00FF00"/><ellipse cx="32" cy="40" rx="10" ry="4" fill="#FFFFFF" opacity="0.5"/></svg>)", 32, px, error));
    CHECK(at(1, 1)[3] == 0);
    CHECK(at(16, 28)[1] > 200);
    CHECK_FALSE(rasterizeSvg("<svg></svg>", 16, px, error));
    CHECK(error.find("нет поддерживаемых фигур") != std::string::npos);
    CHECK_FALSE(rasterizeSvg(R"(<svg><rect x="0" y="0" width="5" height="5" fill="red"/></svg>)", 16, px, error));
    CHECK_FALSE(rasterizeSvg(R"(<svg><polygon points="1,1 2,2" fill="#000000"/></svg>)", 16, px, error));
}

TEST_CASE("brands: данные data/city/brands.json — все бренды вымышленные, ids и названия уникальны, перевод на ru/ko/en, нет слов из списка реальных брендов") {
    han::core::Json json;
    std::string error;
    REQUIRE_MESSAGE(han::core::loadJsonFile(repo("data/city/brands.json"), json, error), error);
    REQUIRE(json.isArray());
    CHECK(json.size() >= 20);
    han::ui::Localization loc;
    REQUIRE_MESSAGE(loc.loadTable(repo("contracts/loc_requests/A.csv"), error), error);
    std::vector<std::string> blocked;
    {
        std::ifstream in(repo("han/tools/pipeline/real_brands.txt"));
        std::string line;
        while (std::getline(in, line))
            if (!line.empty() && line[0] != '#') blocked.push_back(lower(line));
    }
    REQUIRE_FALSE(blocked.empty());
    std::set<std::string> ids, namesEn;
    for (const auto& b : json.items()) {
        const std::string id = b["id"].asString();
        CHECK(b["fictional"].asBool(false));
        CHECK(ids.insert(id).second);
        const std::string key = b["name_key"].asString();
        for (const char* lang : {"ru", "ko", "en"}) CHECK_MESSAGE(loc.has(key, lang), id << ": нет названия на " << lang);
        loc.setLanguage("en");
        const std::string en = loc.get(key);
        CHECK(namesEn.insert(en).second);
        const std::string flat = lower(id + en);
        for (const std::string& word : blocked) CHECK_MESSAGE(flat.find(word) == std::string::npos, id << ": похоже на реальный бренд «" << word << "»");
        CHECK(std::filesystem::exists(repo("assets_src") + "/" + b["logo"].asString()));
        CHECK(b["colors"].size() >= 1);
    }
}

TEST_CASE("brands: загрузка с логотипами — у каждого бренда непрозрачные пиксели эмблемы и цвет вывески из данных") {
    han::ui::Localization loc;
    std::string error;
    REQUIRE(loc.loadTable(repo("contracts/loc_requests/A.csv"), error));
    loc.setLanguage("ko");
    std::vector<SignBrand> brands;
    REQUIRE_MESSAGE(loadSignBrands(repo("data/city/brands.json"), repo("assets_src"), [&](const std::string& k) { return loc.get(k); }, brands, error), error);
    REQUIRE(brands.size() >= 20);
    for (const SignBrand& b : brands) {
        int opaque = 0;
        for (std::size_t i = 3; i < b.logo.size(); i += 4) opaque += b.logo[i] > 128 ? 1 : 0;
        CHECK_MESSAGE(opaque > 200, b.id << ": логотип почти пуст");
        CHECK_MESSAGE(!b.text.empty(), b.id << ": нет названия");
        CHECK_MESSAGE(b.text.rfind("city.brand.", 0) != 0, b.id << ": название не переведено");
    }
    // Плохие данные
    std::vector<SignBrand> bad;
    CHECK_FALSE(loadSignBrands(repo("data/city/нет.json"), repo("assets_src"), [](const std::string& k) { return k; }, bad, error));
    const auto tmp = std::filesystem::temp_directory_path() / "han_brands_bad.json";
    std::ofstream(tmp) << R"([{"id":"x","name_key":"city.brand.x","colors":["#112233"],"logo":"brands/ilsang24.svg","fictional":false}])";
    CHECK_FALSE(loadSignBrands(tmp.string(), repo("assets_src"), [](const std::string& k) { return k; }, bad, error));
    CHECK(error.find("fictional") != std::string::npos);
}

TEST_CASE("brands: атлас витрин — размеры, секции раздаются по кругу, фон вывески цвета бренда, под текстом и стеклом альфа ниже 255") {
    std::vector<SignBrand> brands(2);
    brands[0].id = "a";
    brands[0].background = 0xFF0000FF;  // красный (ABGR)
    brands[0].logo.assign(48 * 48 * 4, 0);
    brands[1].id = "b";
    brands[1].background = 0xFFFF0000;  // синий
    brands[1].logo.assign(48 * 48 * 4, 0);
    const TextureData t = buildStorefrontAtlas(brands, "", 4);
    CHECK(t.width == 1024);
    CHECK(t.height == 256);
    CHECK(t.mips.size() > 3);
    const auto px = [&](int x, int y) { return &t.mips[0][(static_cast<std::size_t>(y) * t.width + static_cast<std::size_t>(x)) * 4]; };
    // Фон вывески первой секции — красный, второй — синий, третьей снова красный (по кругу)
    CHECK(px(200, 15)[0] == 255);
    CHECK(px(200, 15)[2] == 0);
    CHECK(px(256 + 200, 15)[2] == 255);
    CHECK(px(512 + 200, 15)[0] == 255);
    CHECK(px(200, 15)[3] < 255);   // вывеска светится
    CHECK(px(60, 150)[3] < 255);   // стекло витрины светится
    CHECK(px(5, 150)[3] == 255);   // стена — нет
}
