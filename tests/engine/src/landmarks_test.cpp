// Этап 08e: высотные ориентиры, добавляемые в данные OSM.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>

#include "han/world/city_builder.h"
#include "han/world/landmarks.h"

using namespace han::world;

TEST_CASE("landmarks: файл читается, ориентир добавляется как здание нужной высоты, повторно рядом не добавляется") {
    std::vector<Landmark> list;
    std::string error;
    REQUIRE_MESSAGE(loadLandmarks((std::filesystem::path(HAN_REPO_DIR) / "data/city/landmarks.json").string(), list, error), error);
    REQUIRE(list.size() >= 2);
    for (const auto& l : list) CHECK_FALSE(l.source.empty());

    OsmData data;
    const auto stats = applyLandmarks(data, list);
    CHECK(stats.added == static_cast<int>(list.size()));
    REQUIRE(data.ways.size() == list.size());
    const OsmWay& way = data.ways[0];
    CHECK(way.closed());
    CHECK(way.tag("building") == "yes");
    CHECK(way.tag("han:landmark") == list[0].id);
    CityBuildSettings settings;
    CHECK(buildingHeight(way, settings) == doctest::Approx(std::min(list[0].heightMeters, settings.maxBuildingHeight)));
    // Все узлы призмы лежат на заданном радиусе от центра.
    const double mx = 111320.0 * std::cos(list[0].lat * 3.14159265358979 / 180.0), my = 110540.0;
    for (const auto id : way.nodes) {
        const auto& node = data.nodes.at(id);
        CHECK(std::hypot((node.lon - list[0].lon) * mx, (node.lat - list[0].lat) * my) == doctest::Approx(list[0].radiusMeters).epsilon(0.01));
    }
    // Второй вызов видит добавленные здания: не дублирует.
    const auto again = applyLandmarks(data, list);
    CHECK(again.added == 0);
    CHECK(again.skippedPresent == static_cast<int>(list.size()));
}

TEST_CASE("landmarks: плохие данные отклоняются") {
    const auto path = std::filesystem::temp_directory_path() / "han_landmarks_bad.json";
    const auto write = [&](const char* text) { std::FILE* f = std::fopen(path.string().c_str(), "wb"); std::fputs(text, f); std::fclose(f); };
    std::vector<Landmark> out;
    std::string error;
    write("{\"version\":1,\"landmarks\":[{\"id\":\"a\",\"lat\":37.5,\"lon\":127.0,\"radius_m\":20,\"sides\":8,\"height_m\":5,\"source\":\"x\"}]}");
    CHECK_FALSE(loadLandmarks(path.string(), out, error));  // высота меньше 20 м
    write("{\"version\":1,\"landmarks\":[{\"id\":\"a\",\"lat\":37.5,\"lon\":127.0,\"radius_m\":20,\"sides\":8,\"height_m\":100}]}");
    CHECK_FALSE(loadLandmarks(path.string(), out, error));  // нет источника
    write("{\"version\":1,\"landmarks\":[{\"id\":\"a\",\"lat\":137.5,\"lon\":127.0,\"radius_m\":20,\"sides\":8,\"height_m\":100,\"source\":\"x\"}]}");
    CHECK_FALSE(loadLandmarks(path.string(), out, error));  // широта вне диапазона
    CHECK(out.empty());
    std::filesystem::remove(path);
}
