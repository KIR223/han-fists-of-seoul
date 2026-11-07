// Этап 08d: дальний силуэт — коробки высоких зданий по плиткам.
#include <doctest/doctest.h>

#include <cmath>
#include <sstream>
#include <string>

#include "han/world/far_skyline.h"

using namespace han::world;

namespace {
const GeoProjection kProj(37.557, 126.914);

// Квадратные здания со стороной side: id, центр (x, z) и теги.
std::string mapText(const std::vector<std::tuple<int, float, float, float, std::string>>& buildings) {
    std::ostringstream out;
    out.precision(10);
    out << "<?xml version=\"1.0\"?>\n<osm version=\"0.6\">\n";
    int node = 1;
    std::ostringstream ways;
    for (const auto& [id, cx, cz, side, tags] : buildings) {
        const float h = side * 0.5f;
        const float corners[4][2] = {{cx - h, cz - h}, {cx + h, cz - h}, {cx + h, cz + h}, {cx - h, cz + h}};
        const int first = node;
        for (const auto& c : corners) {
            double lat, lon;
            kProj.toGeo(c[0], c[1], lat, lon);
            out << " <node id=\"" << node++ << "\" lat=\"" << lat << "\" lon=\"" << lon << "\"/>\n";
        }
        ways << " <way id=\"" << id << "\">";
        for (int k = 0; k < 4; ++k) ways << "<nd ref=\"" << first + k << "\"/>";
        ways << "<nd ref=\"" << first << "\"/>" << tags << "</way>\n";
    }
    out << ways.str() << "</osm>\n";
    return out.str();
}
}  // namespace

TEST_CASE("far_skyline: высокие здания — коробка из стен и крыши, низкие и мелкие пропускаются, нормали стен наружу") {
    OsmData data;
    std::string error;
    const std::string tall = "<tag k=\"building\" v=\"yes\"/><tag k=\"height\" v=\"60\"/>";
    const std::string low = "<tag k=\"building\" v=\"yes\"/><tag k=\"height\" v=\"12\"/>";
    REQUIRE_MESSAGE(parseOsmXml(mapText({{1, 100.f, 100.f, 30.f, tall}, {2, 300.f, 100.f, 30.f, low}, {3, 600.f, 100.f, 5.f, tall}, {4, 1400.f, -300.f, 40.f, tall}}), data, error), error);
    CityBuildSettings settings;
    FarSkylineStats stats;
    const auto tiles = buildFarSkyline(data, kProj, settings, nullptr, {}, &stats);
    CHECK(stats.buildings == 2);
    CHECK(stats.skippedLow == 1);
    CHECK(stats.skippedSmall == 1);
    CHECK(stats.tiles == 2);
    CHECK(stats.triangles == 20);  // по 4 стены × 2 + крыша 2 на здание
    REQUIRE(tiles.count({0, 0}) == 1);
    const GeoMesh& mesh = tiles.at({0, 0});
    float maxY = -1e9f;
    for (const auto& v : mesh.vertices) maxY = std::max(maxY, v.position[1]);
    CHECK(maxY == doctest::Approx(60.f));  // плоская земля: крыша на высоте здания
    // Каждый треугольник смотрит по своей нормали (порядок вершин согласован), стены — от центра здания.
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const float e1[3] = {b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]};
        const float e2[3] = {c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]};
        const float cross[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        CHECK(cross[0] * a.normal[0] + cross[1] * a.normal[1] + cross[2] * a.normal[2] > 0.f);
        if (std::fabs(a.normal[1]) < 0.5f) {  // стена: нормаль направлена от центра здания (100, 100)
            const float out = (a.position[0] - 100.f) * a.normal[0] + (a.position[2] - 100.f) * a.normal[2];
            CHECK(out > 0.f);
        }
    }
    // Две части: стена и крыша.
    int walls = 0, roofs = 0;
    for (const auto& r : mesh.ranges) {
        walls += r.material == GeoMaterial::WallConcrete;
        roofs += r.material == GeoMaterial::Roof;
    }
    CHECK(walls == 1);
    CHECK(roofs == 1);
}
