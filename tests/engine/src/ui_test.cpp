// Тесты интерфейса на RmlUi (E8): документ, шрифт с кириллицей и хангылем, клики, повторная инициализация. Без окна и ImGui (отрисовка — в демо).
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#ifdef HAN_WITH_RMLUI
#include "han/ui/localization.h"
#include "han/ui/rml_layer.h"

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }
}  // namespace

TEST_CASE("ui: демо-документ загружается, элементы считаются по селектору, текст заменяется") {
    han::ui::RmlLayer rml;
    REQUIRE(rml.init(1280, 720));
    const std::string font = repo("assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf");
    if (std::filesystem::exists(font)) CHECK(rml.loadFont(font));  // без скачанного шрифта проверяем только разметку
    REQUIRE(rml.loadDocument(repo("han/ui/samples/ui_demo.rml")));
    rml.beginFrame(1280, 720);
    CHECK(rml.count(".button") == 6);  // три кнопки меню и три кнопки языка
    CHECK(rml.count(".panel") == 2);
    CHECK(rml.count("#menu") == 1);
    CHECK(rml.setText("health-value", "77"));
    CHECK_FALSE(rml.setText("нет-такого", "x"));
    CHECK_FALSE(rml.loadDocument(repo("han/ui/samples/нет_файла.rml")));
}

TEST_CASE("ui: клик по кнопке даёт её id, клик мимо — ничего; раскладка в пределах окна") {
    han::ui::RmlLayer rml;
    REQUIRE(rml.init(1280, 720));
    const std::string font = repo("assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf");
    if (std::filesystem::exists(font)) rml.loadFont(font);
    REQUIRE(rml.loadDocument(repo("han/ui/samples/ui_demo.rml")));
    REQUIRE(rml.setVisible("menu", true));  // меню в документе скрыто, пока игрок не открыл паузу
    rml.injectMouse(0, 0, false);
    for (const char* id : {"btn-resume", "btn-settings", "btn-quit"}) {
        int x = 0, y = 0;
        REQUIRE(rml.elementCenter(id, x, y));
        CHECK(x > 0);
        CHECK(x < 1280);
        CHECK(y > 0);
        CHECK(y < 720);
        rml.injectMouse(x, y, false);
        CHECK(rml.wantsMouse());
        rml.injectMouse(x, y, true);
        rml.injectMouse(x, y, false);
        CHECK(rml.takeClicked() == id);
        CHECK(rml.takeClicked().empty());  // забирается один раз
    }
    rml.injectMouse(6, 700, false);
    rml.injectMouse(6, 700, true);
    rml.injectMouse(6, 700, false);
    CHECK(rml.takeClicked().empty());  // клик по пустому месту
}

TEST_CASE("ui: слой можно создать заново после уничтожения (повторная инициализация RmlUi)") {
    for (int round = 0; round < 3; ++round) {
        han::ui::RmlLayer rml;
        REQUIRE(rml.init(800, 600));
        const std::string font = repo("assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf");
        if (std::filesystem::exists(font)) rml.loadFont(font);
        CHECK(rml.loadDocument(repo("han/ui/samples/ui_demo.rml")));
        rml.beginFrame(800, 600);
        CHECK(rml.count(".button") == 6);  // три кнопки меню и три кнопки языка
    }
}
#else
TEST_CASE("ui: RmlUi не собран — тесты интерфейса пропущены") { CHECK(true); }
#endif

#ifdef HAN_WITH_RMLUI
TEST_CASE("ui: тексты по data-loc подставляются на трёх языках, меню управляется клавишами, Enter выбирает кнопку") {
    han::ui::RmlLayer rml;
    REQUIRE(rml.init(1280, 720));
    const std::string font = repo("assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf");
    if (std::filesystem::exists(font)) rml.loadFont(font);
    REQUIRE(rml.loadDocument(repo("han/ui/samples/ui_demo.rml")));
    han::ui::Localization loc;
    std::string error;
    REQUIRE_MESSAGE(loc.loadTable(repo("han/ui/samples/ui_demo_loc.csv"), error), error);
    CHECK(loc.incomplete().empty());
    for (const char* lang : {"ru", "ko", "en"}) {
        loc.setLanguage(lang);
        CHECK(rml.applyLocalization(loc) >= 9);
    }
    // Меню скрыто — кнопок для управления нет; открыли — стрелки двигают фокус по кругу.
    rml.injectMouse(0, 0, false);
    CHECK_FALSE(rml.navigate(han::ui::RmlLayer::NavKey::Down));
    CHECK(rml.setVisible("menu", true));
    rml.injectMouse(0, 0, false);
    CHECK(rml.navigate(han::ui::RmlLayer::NavKey::Down));
    CHECK(rml.focusedId() == "btn-resume");
    CHECK(rml.navigate(han::ui::RmlLayer::NavKey::Down));
    CHECK(rml.focusedId() == "btn-settings");
    CHECK(rml.navigate(han::ui::RmlLayer::NavKey::Up));
    CHECK(rml.navigate(han::ui::RmlLayer::NavKey::Up));
    CHECK(rml.focusedId() == "btn-lang-en");  // с первой кнопки вверх — на последнюю
    CHECK(rml.navigate(han::ui::RmlLayer::NavKey::Accept));
    CHECK(rml.takeClicked() == "btn-lang-en");
    CHECK(rml.navigate(han::ui::RmlLayer::NavKey::Back));
    CHECK(rml.takeClicked() == "ui:back");
    CHECK(rml.setVisible("menu", false));
    CHECK(rml.focusedId().empty());  // скрытое меню сбрасывает фокус
}
#endif
