#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "han/core/json.h"
#include "han/world/district_scene.h"

TEST_CASE("district_scene: район находится в общей системе координат, фонари и размеры проверяются") {
    namespace fs = std::filesystem;
    std::vector<han::world::DistrictScene> scenes;
    std::string error;
    const auto file = fs::path(HAN_REPO_DIR) / "data/city/district_scenes.json";
    REQUIRE_MESSAGE(han::world::loadDistrictScenes(file.string(), scenes, error), error);
    REQUIRE(scenes.size() == 1);
    CHECK(scenes[0].id == "kandam");
    CHECK(scenes[0].shadow == "kandam/kandam_shadow.hmesh");
    CHECK(scenes[0].origin.x == 10020);
    CHECK(scenes[0].origin.z == 6560);
    CHECK(scenes[0].lamps.size() == 368);
    CHECK(scenes[0].interiorLamps.size() == 7);
    CHECK(scenes[0].footprints.size() == 151);
    CHECK(han::world::districtDistance(scenes[0], 10020, 6560) == 0);
    CHECK(han::world::districtDistance(scenes[0], 10020 + scenes[0].halfWidth + 20, 6560) == doctest::Approx(20));
    const auto saved = scenes;
    han::core::Json data;
    REQUIRE(han::core::loadJsonFile(file.string(), data, error));
    const auto temporary = fs::temp_directory_path() / "han_bad_district_scene.json";
    const auto invalid = [&](const char* key, han::core::Json value) {
        auto item = data["scenes"].at(0);
        item.set(key, std::move(value));
        auto list = han::core::Json::array(); list.push(item);
        auto copy = data; copy.set("scenes", list);
        std::ofstream(temporary) << copy.dump();
        CHECK_FALSE(han::world::loadDistrictScenes(temporary.string(), scenes, error));
        CHECK(scenes.size() == saved.size()); CHECK(scenes[0].mesh == saved[0].mesh);
    };
    invalid("mesh", han::core::Json::string("../outside.hmesh"));
    invalid("shadow", han::core::Json::string("../outside.hmesh"));
    invalid("interiors", han::core::Json::string("C:/outside.hmesh"));
    invalid("load_distance", han::core::Json::number(2000));
    invalid("lights", han::core::Json::string("data/city/../../secret.json"));
    invalid("half_size", han::core::Json::array());
    fs::remove(temporary);
}
