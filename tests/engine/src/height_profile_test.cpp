// Этап 08: профиль высот зданий без тегов.
#include <doctest/doctest.h>

#include <filesystem>

#include "han/world/city_builder.h"
#include "han/world/height_profile.h"

using namespace han::world;

TEST_CASE("height_profile: данные читаются, высота по типу и площади, теги главнее профиля") {
    HeightProfile profile;
    std::string error;
    REQUIRE_MESSAGE(loadHeightProfile((std::filesystem::path(HAN_REPO_DIR) / "data/city/height_profile.json").string(), profile, error), error);
    CHECK(profile.heightFor("apartments", 500.f) == doctest::Approx(48.f));
    CHECK(profile.heightFor("house", 80.f) < profile.heightFor("apartments", 80.f));
    CHECK(profile.heightFor("yes", 100.f) < profile.heightFor("yes", 300.f));
    CHECK(profile.heightFor("yes", 300.f) < profile.heightFor("yes", 900.f));
    CHECK(profile.heightFor("неизвестный_тип", 300.f) == 0.f);
    HeightProfile off;
    CHECK(off.heightFor("apartments", 500.f) == 0.f);  // выключенный профиль ничего не знает

    CityBuildSettings settings;
    settings.heights = profile;
    OsmWay untagged;
    untagged.tags = {{"building", "apartments"}};
    CHECK(buildingHeight(untagged, settings, 400.f) == doctest::Approx(48.f));
    OsmWay levels;
    levels.tags = {{"building", "apartments"}, {"building:levels", "5"}};
    CHECK(buildingHeight(levels, settings, 400.f) == doctest::Approx(5.f * settings.levelHeight + 1.f));
    OsmWay tall;
    tall.tags = {{"building", "apartments"}, {"height", "20"}};
    CHECK(buildingHeight(tall, settings, 400.f) == doctest::Approx(20.f));
    CityBuildSettings plain;  // профиль выключен: прежние 9 м
    CHECK(buildingHeight(untagged, plain, 400.f) == doctest::Approx(plain.defaultBuildingHeight));
}

TEST_CASE("height_profile: плохой файл отклоняется и не меняет профиль") {
    const auto path = std::filesystem::temp_directory_path() / "han_height_bad.json";
    const auto write = [&](const char* text) { std::FILE* f = std::fopen(path.string().c_str(), "wb"); std::fputs(text, f); std::fclose(f); };
    HeightProfile p;
    p.yesSmall = 7.f;
    std::string error;
    write("{\"version\":1,\"by_type\":{\"house\":-3},\"yes_by_area\":{\"small_max_m2\":100,\"mid_max_m2\":500,\"small\":8,\"mid\":20,\"large\":40}}");
    CHECK_FALSE(loadHeightProfile(path.string(), p, error));
    write("{\"version\":1,\"by_type\":{\"house\":8},\"yes_by_area\":{\"small_max_m2\":500,\"mid_max_m2\":100,\"small\":8,\"mid\":20,\"large\":40}}");
    CHECK_FALSE(loadHeightProfile(path.string(), p, error));  // границы не по возрастанию
    CHECK(p.yesSmall == 7.f);
    CHECK_FALSE(p.enabled);
    std::filesystem::remove(path);
}
