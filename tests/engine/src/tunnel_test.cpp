// Этап 08b: тоннели — полотно уходит под рельеф между порталами, на земле только порталы.
#include <doctest/doctest.h>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "han/world/osm.h"
#include "han/world/road_profile.h"
#include "han/world/terrain.h"

using namespace han::world;

namespace {
const GeoProjection kProj(37.557, 126.914);

OsmData tunnelMap(const std::string& tunnelTags) {
    std::ostringstream out;
    out.precision(10);
    out << "<?xml version=\"1.0\"?>\n<osm version=\"0.6\">\n";
    long id = 1;
    const auto node = [&](float x) {
        double lat, lon;
        kProj.toGeo(x, 0.f, lat, lon);
        out << " <node id=\"" << id++ << "\" lat=\"" << lat << "\" lon=\"" << lon << "\"/>\n";
    };
    for (float x = -1000.f; x <= 1000.f; x += 50.f) node(x);  // узлы 1..41, x = -1000 + 50*(id-1)
    const auto way = [&](long wid, int from, int to, const std::string& tags) {
        out << " <way id=\"" << wid << "\">";
        for (int n = from; n <= to; ++n) out << "<nd ref=\"" << n << "\"/>";
        out << tags << "</way>\n";
    };
    const std::string road = "<tag k=\"highway\" v=\"primary\"/>";
    way(100, 1, 9, road);                       // земля до x = -600
    way(101, 9, 33, road + tunnelTags);         // тоннель -600..600
    way(102, 33, 41, road);                     // земля от x = 600
    out << "</osm>\n";
    OsmData data;
    std::string error;
    REQUIRE_MESSAGE(parseOsmXml(out.str(), data, error), error);
    return data;
}

// Холм 30 м шириной ±500 м над дорогой вдоль x.
Heightmap hill() {
    std::vector<float> h(81 * 81);
    for (int z = 0; z < 81; ++z)
        for (int x = 0; x < 81; ++x) h[static_cast<std::size_t>(z * 81 + x)] = 30.f * std::max(0.f, 1.f - std::fabs(-2000.f + 50.f * static_cast<float>(x)) / 500.f);
    return Heightmap::fromGrid(-2000.f, -2000.f, 50.f, 81, 81, h);
}
}  // namespace

TEST_CASE("tunnel: теги — тоннелем считается только проезжий тоннель") {
    const auto data = tunnelMap("<tag k=\"tunnel\" v=\"yes\"/>");
    CHECK(isTunnelWay(data.ways[1]));
    CHECK_FALSE(isTunnelWay(data.ways[0]));
    CHECK_FALSE(isTunnelWay(tunnelMap("<tag k=\"tunnel\" v=\"building_passage\"/>").ways[1]));
    CHECK_FALSE(isTunnelWay(tunnelMap("<tag k=\"tunnel\" v=\"culvert\"/>").ways[1]));
    CHECK_FALSE(isTunnelWay(tunnelMap("<tag k=\"tunnel\" v=\"no\"/>").ways[1]));
}

TEST_CASE("tunnel: под холмом полотно уходит под землю, порталы на земле, уклон в пределах, выше земли узлов нет") {
    const auto data = tunnelMap("<tag k=\"tunnel\" v=\"yes\"/>");
    const Heightmap terrain = hill();
    const RoadProfileSettings settings;
    const auto profile = computeRoadProfile(data, kProj, &terrain, settings);
    REQUIRE_FALSE(profile.empty());
    const auto& st = profile.stats();
    CHECK(st.tunnelNodes == 23);              // узлы 10..32 внутри тоннеля
    CHECK(st.tunnelAboveGround == 0);
    CHECK(st.tunnelMaxGrade <= settings.maxGrade + 0.001f);
    CHECK(st.maxTunnelDepth > 25.f);          // под вершиной холма ~30 м
    // Порталы стоят на земле и не относятся к узлам профиля (они — якоря обычной дороги).
    CHECK(profile.hasNode(9));
    CHECK(profile.height(9) == doctest::Approx(terrain.heightAt(-600.f, 0.f)).epsilon(0.01));
    CHECK_FALSE(profile.isProfileNode(9));
    CHECK(profile.isProfileNode(21));         // середина (x = 0)
    CHECK(profile.height(21) < terrain.heightAt(0.f, 0.f) - 20.f);
    CHECK_FALSE(profile.hasNode(5));          // обычная дорога вне тоннеля профилем не затронута
}

TEST_CASE("tunnel: на ровной земле полотно держится на слое грунта, нарастающем от портала не круче допустимого уклона") {
    const auto data = tunnelMap("<tag k=\"tunnel\" v=\"yes\"/>");
    const Heightmap flat = Heightmap::fromGrid(-2000.f, -2000.f, 50.f, 81, 81, std::vector<float>(81 * 81, 10.f));
    const RoadProfileSettings settings;
    const auto profile = computeRoadProfile(data, kProj, &flat, settings);
    CHECK(profile.stats().tunnelAboveGround == 0);
    // Узел 10 в 50 м от портала: слой грунта min(2 м, 7% * 50 м = 3,5 м) = 2 м.
    CHECK(profile.height(10) == doctest::Approx(10.f - settings.tunnelCover).epsilon(0.01));
    // Узел 9 — портал на земле; узел следующий за ним не глубже, чем позволяет уклон от портала.
    CHECK(10.f - profile.height(10) <= settings.maxGrade * 50.f + 0.01f);
}
