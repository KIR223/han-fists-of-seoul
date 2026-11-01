// Тесты локализации (E8): CSV с кавычками, язык и запасные варианты, подстановки, проверка полноты, файлы языков.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "han/ui/localization.h"

using han::ui::Localization;
using han::ui::parseCsvLine;

namespace {
std::filesystem::path tempDir() {
    const auto dir = std::filesystem::temp_directory_path() / "han_loc_test";
    std::filesystem::create_directories(dir);
    return dir;
}
std::string write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
    return file.string();
}
}  // namespace

TEST_CASE("loc: разбор строки CSV — кавычки, запятые в тексте, пустые поля, комментарии, BOM") {
    CHECK(parseCsvLine("a,b,c") == std::vector<std::string>{"a", "b", "c"});
    CHECK(parseCsvLine("k,\"Привет, мир\",x") == std::vector<std::string>{"k", "Привет, мир", "x"});
    CHECK(parseCsvLine("k,\"он сказал \"\"да\"\"\",x") == std::vector<std::string>{"k", "он сказал \"да\"", "x"});
    CHECK(parseCsvLine("k,,x") == std::vector<std::string>{"k", "", "x"});
    CHECK(parseCsvLine("# комментарий").empty());
    CHECK(parseCsvLine("").empty());
    CHECK(parseCsvLine("\xEF\xBB\xBF" "key,ru") == std::vector<std::string>{"key", "ru"});
    CHECK(parseCsvLine("a,b\r\n") == std::vector<std::string>{"a", "b"});
}

TEST_CASE("loc: таблица заявок — язык переключается, нет перевода — английский, потом русский, потом ключ") {
    const auto dir = tempDir();
    const std::string file = write(dir / "t.csv", "key,ru,ko,en,context\nmenu.start,Начать,시작,Start,кнопка\nmenu.only_ru,Только русский,,,нет перевода\nmenu.no_ko,Без корейского,,No Korean,x\n");
    Localization loc;
    std::string error;
    REQUIRE(loc.loadTable(file, error));
    CHECK(error.empty());
    CHECK(loc.size() == 3);
    CHECK(loc.get("menu.start") == "Начать");
    loc.setLanguage("ko");
    CHECK(loc.get("menu.start") == "시작");
    CHECK(loc.get("menu.no_ko") == "No Korean");           // нет ko → en
    CHECK(loc.get("menu.only_ru") == "Только русский");     // нет ko и en → ru
    CHECK(loc.get("menu.missing_key") == "menu.missing_key");  // нет ключа — сам ключ
    loc.setLanguage("en");
    CHECK(loc.get("menu.start") == "Start");
    const auto bad = loc.incomplete();
    CHECK(bad.size() == 3);  // only_ru: ko и en; no_ko: ko
}

TEST_CASE("loc: подстановки {0} {1}, повторные и лишние; ошибки файла") {
    const auto dir = tempDir();
    Localization loc;
    std::string error;
    REQUIRE(loc.loadTable(write(dir / "f.csv", "key,ru,ko,en\nhud.round,Раунд {0} из {1} ({0}),{0}/{1} 라운드,Round {0} of {1}\n"), error));
    CHECK(loc.format("hud.round", {"2", "5"}) == "Раунд 2 из 5 (2)");
    loc.setLanguage("ko");
    CHECK(loc.format("hud.round", {"2", "5"}) == "2/5 라운드");
    CHECK(loc.format("hud.round", {"2"}) == "2/{1} 라운드");  // не хватает аргумента — токен остаётся видимым
    CHECK_FALSE(loc.loadTable((dir / "нет.csv").string(), error));
    CHECK_FALSE(loc.loadTable(write(dir / "g.csv", "ключ,текст\nа,б\n"), error));
    CHECK(error.find("первая строка") != std::string::npos);
    std::string e2;
    CHECK(loc.loadTable(write(dir / "h.csv", "key,ru,ko,en\nПлохой Ключ,а,б,в\nok.key,а,б,в\n"), e2));
    CHECK(e2.find("неверная строка или ключ") != std::string::npos);
    CHECK(loc.has("ok.key", "ru"));
    CHECK_FALSE(loc.has("Плохой Ключ", "ru"));
}

TEST_CASE("loc: файлы языков data/loc/<язык>/*.csv — по зонам, ключи объединяются") {
    const auto dir = tempDir() / "lang";
    std::filesystem::create_directories(dir);
    write(dir / "combat.csv", "key,text\ncombat.hit,Попадание\n");
    write(dir / "ui.csv", "key,text\nui.ok,\"Ок, понятно\"\n");
    Localization loc;
    std::string error;
    REQUIRE(loc.loadLanguageDir("ru", dir.string(), error));
    CHECK(loc.get("combat.hit") == "Попадание");
    CHECK(loc.get("ui.ok") == "Ок, понятно");
    CHECK_FALSE(loc.loadLanguageFile("de", (dir / "ui.csv").string(), error));
    CHECK_FALSE(loc.loadLanguageDir("ru", (dir / "нет").string(), error));
}

TEST_CASE("loc: реальные заявки contracts/loc_requests (A.csv и B.csv) читаются без ошибок и без дублей языков") {
    Localization loc;
    for (const char* name : {"A.csv", "B.csv"}) {
        std::string error;
        REQUIRE_MESSAGE(loc.loadTable((std::filesystem::path(HAN_REPO_DIR) / "contracts/loc_requests" / name).string(), error), error);
        CHECK_MESSAGE(error.empty(), error);
    }
    CHECK(loc.size() > 30);
    CHECK_MESSAGE(loc.incomplete().empty(), "ключи без перевода: " << loc.incomplete().size());
}
