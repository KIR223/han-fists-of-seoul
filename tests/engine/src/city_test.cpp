// Тесты E10: чтение OSM, проекция, триангуляция, построение города (дороги, здания, парки, вода) по ячейкам.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include "han/world/city_builder.h"
#include "han/world/geometry.h"
#include "han/world/osm.h"

using namespace han::world;

namespace {
// Небольшой район около точки (37.5000, 127.0000): дорога-«Г», два дома, Г-образный дом, парк, пруд, мелкий сарай и мусор.
// Узлы: широта шагом 0.0002° ≈ 22 м, долгота шагом 0.0002° ≈ 17.7 м.
const char* kFixture = R"(<?xml version='1.0' encoding='UTF-8'?>
<osm version="0.6" generator="test">
 <!-- комментарий внутри файла -->
 <node id="1" lat="37.5000" lon="127.0000"/>
 <node id="2" lat="37.5000" lon="127.0010"/>
 <node id="3" lat="37.5010" lon="127.0010"/>
 <node id="10" lat="37.5003" lon="127.0002"/>
 <node id="11" lat="37.5003" lon="127.0004"/>
 <node id="12" lat="37.5005" lon="127.0004"/>
 <node id="13" lat="37.5005" lon="127.0002"/>
 <node id="20" lat="37.5003" lon="127.0006"/>
 <node id="21" lat="37.5003" lon="127.0008"/>
 <node id="22" lat="37.5005" lon="127.0008"/>
 <node id="23" lat="37.5005" lon="127.0006"/>
 <node id="30" lat="37.5007" lon="127.0002"/>
 <node id="31" lat="37.5007" lon="127.0006"/>
 <node id="32" lat="37.5008" lon="127.0006"/>
 <node id="33" lat="37.5008" lon="127.0004"/>
 <node id="34" lat="37.5010" lon="127.0004"/>
 <node id="35" lat="37.5010" lon="127.0002"/>
 <node id="40" lat="37.5001" lon="127.0005"/>
 <node id="41" lat="37.5001" lon="127.0007"/>
 <node id="42" lat="37.5002" lon="127.0007"/>
 <node id="43" lat="37.5002" lon="127.0005"/>
 <node id="50" lat="37.50031" lon="127.00091"/>
 <node id="51" lat="37.50032" lon="127.00091"/>
 <node id="52" lat="37.50032" lon="127.00092"/>
 <node id="53" lat="37.50031" lon="127.00092"/>
 <way id="100">
  <nd ref="1"/><nd ref="2"/><nd ref="3"/>
  <tag k="highway" v="residential"/>
  <tag k="name" v="Tom &amp; Jerry &quot;Street&quot;"/>
 </way>
 <way id="101">
  <nd ref="10"/><nd ref="11"/><nd ref="12"/><nd ref="13"/><nd ref="10"/>
  <tag k="building" v="yes"/>
  <tag k="building:levels" v="4"/>
 </way>
 <way id="102">
  <nd ref="20"/><nd ref="21"/><nd ref="22"/><nd ref="23"/><nd ref="20"/>
  <tag k="building" v="apartments"/>
  <tag k="height" v="25 m"/>
 </way>
 <way id="103">
  <nd ref="30"/><nd ref="31"/><nd ref="32"/><nd ref="33"/><nd ref="34"/><nd ref="35"/><nd ref="30"/>
  <tag k="building" v="yes"/>
 </way>
 <way id="104">
  <nd ref="40"/><nd ref="41"/><nd ref="42"/><nd ref="43"/><nd ref="40"/>
  <tag k="leisure" v="park"/>
 </way>
 <way id="105">
  <nd ref="50"/><nd ref="51"/><nd ref="52"/><nd ref="53"/><nd ref="50"/>
  <tag k="building" v="shed"/>
 </way>
 <way id="106">
  <nd ref="10"/><nd ref="11"/><nd ref="12"/>
  <tag k="building" v="yes"/>
 </way>
 <way id="107">
  <nd ref="1"/><nd ref="999"/><nd ref="2"/>
  <tag k="highway" v="footway"/>
 </way>
 <relation id="500"><member type="way" ref="101" role="outer"/><tag k="type" v="multipolygon"/></relation>
</osm>
)";

OsmData parseFixture() {
    OsmData d;
    std::string error;
    REQUIRE_MESSAGE(parseOsmXml(kFixture, d, error), error);
    return d;
}

const OsmWay* wayById(const OsmData& d, std::int64_t id) {
    for (const auto& w : d.ways)
        if (w.id == id) return &w;
    return nullptr;
}

// Площадь горизонтальных граней на высоте y (проекция на XZ) — для проверки крыш и плоских покрытий.
float areaAtHeight(const GeoMesh& m, float y, float tol = 1e-3f) {
    double area = 0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
        if (std::fabs(a.position[1] - y) > tol || std::fabs(b.position[1] - y) > tol || std::fabs(c.position[1] - y) > tol) continue;
        area += std::fabs(0.5 * ((b.position[0] - a.position[0]) * (c.position[2] - a.position[2]) - (c.position[0] - a.position[0]) * (b.position[2] - a.position[2])));
    }
    return static_cast<float>(area);
}

// Все треугольники смотрят наружу: геометрическая нормаль (против часовой) совпадает с нормалью вершины.
int wrongFacing(const GeoMesh& m) {
    int bad = 0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
        const float ux = b.position[0] - a.position[0], uy = b.position[1] - a.position[1], uz = b.position[2] - a.position[2];
        const float vx = c.position[0] - a.position[0], vy = c.position[1] - a.position[1], vz = c.position[2] - a.position[2];
        const float gx = uy * vz - uz * vy, gy = uz * vx - ux * vz, gz = ux * vy - uy * vx;
        const float dot = gx * a.normal[0] + gy * a.normal[1] + gz * a.normal[2];
        if (dot <= 0.f) ++bad;
    }
    return bad;
}

bool allValid(const GeoMesh& m) {
    for (std::uint32_t i : m.indices)
        if (i >= m.vertices.size()) return false;
    for (const auto& v : m.vertices) {
        for (float f : v.position) if (!std::isfinite(f)) return false;
        const float n = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
        if (std::fabs(n - 1.f) > 1e-3f) return false;
    }
    return m.indices.size() % 3 == 0;
}
}  // namespace

TEST_CASE("osm: файл разбирается — узлы, линии, теги с экранированием, пропущенные отношения") {
    const OsmData d = parseFixture();
    CHECK(d.nodes.size() == 25);
    CHECK(d.ways.size() == 8);
    CHECK(d.relationsSkipped == 1);
    const OsmWay* road = wayById(d, 100);
    REQUIRE(road);
    CHECK(road->nodes.size() == 3);
    CHECK(road->tag("highway") == "residential");
    CHECK(road->tag("name") == "Tom & Jerry \"Street\"");
    CHECK(road->tag("нет_такого").empty());
    CHECK(road->closed() == false);
    CHECK(wayById(d, 101)->closed());
    CHECK(d.nodes.at(1).lat == doctest::Approx(37.5));
    CHECK(d.nodes.at(3).lon == doctest::Approx(127.001));
    CHECK(d.waysWithMissingNodes == 1);                   // линия 107 ссылалась на узел 999
    CHECK(wayById(d, 107)->nodes.size() == 2);            // отсутствующий узел выброшен
}

TEST_CASE("osm: повреждённые файлы дают понятную ошибку, а не падение") {
    OsmData d;
    std::string error;
    CHECK_FALSE(parseOsmXml("это не xml", d, error));
    CHECK(error.find("не файл OSM") != std::string::npos);
    CHECK_FALSE(parseOsmXml("<osm><node id=\"1\" lat=\"1\" lon=\"2\"", d, error));
    CHECK(error.find("не закрыт тег") != std::string::npos);
    CHECK_FALSE(parseOsmXml("<osm><node id=1 lat=\"1\"/></osm>", d, error));
    CHECK(error.find("атрибуты") != std::string::npos);
    CHECK_FALSE(parseOsmXml("<osm><way></way></osm>", d, error));
    CHECK(error.find("нет id") != std::string::npos);
    CHECK_FALSE(parseOsmXml("<osm><!-- без конца", d, error));
    CHECK_FALSE(loadOsmFile("нет_такого_файла.osm", d, error));
    CHECK(parseOsmXml("<osm/>", d, error));  // пустой, но корректный
    CHECK(d.nodes.empty());
}

TEST_CASE("osm: числовые символьные ссылки (&#x...;) превращаются в UTF-8") {
    OsmData d;
    std::string error;
    REQUIRE(parseOsmXml("<osm><way id=\"1\"><tag k=\"a\" v=\"&#xD55C;&#44544; &#65;\"/></way></osm>", d, error));
    CHECK(d.ways[0].tag("a") == "한글 A");
}

TEST_CASE("geo: проекция — начало в нуле, север по -Z, восток по +X, длины градусов, обратное преобразование") {
    GeoProjection p(37.5, 127.0);
    float x = 1, z = 1;
    p.toLocal(37.5, 127.0, x, z);
    CHECK(x == doctest::Approx(0.f).epsilon(1e-6));
    CHECK(z == doctest::Approx(0.f).epsilon(1e-6));
    p.toLocal(37.501, 127.0, x, z);
    CHECK(z < 0.f);  // севернее — к -Z
    CHECK(-z == doctest::Approx(111.0f).epsilon(0.01));
    p.toLocal(37.5, 127.001, x, z);
    CHECK(x == doctest::Approx(88.3f).epsilon(0.01));  // cos(37.5°) · 111.3 км/°
    double lat = 0, lon = 0;
    p.toLocal(37.5123, 127.0456, x, z);
    p.toGeo(x, z, lat, lon);
    CHECK(lat == doctest::Approx(37.5123).epsilon(1e-7));
    CHECK(lon == doctest::Approx(127.0456).epsilon(1e-7));
}

TEST_CASE("geometry: триангуляция — квадрат, Г-образный, выпуклый, обратный обход, замыкающая вершина") {
    const std::vector<Vec2> square = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    auto t = triangulate(square);
    CHECK(t.size() == 6);
    const std::vector<Vec2> lshape = {{0, 0}, {20, 0}, {20, 10}, {10, 10}, {10, 20}, {0, 20}};
    t = triangulate(lshape);
    REQUIRE(t.size() == 12);
    double sum = 0;  // сумма площадей треугольников равна площади фигуры (300) и все они положительны
    for (std::size_t i = 0; i < t.size(); i += 3) {
        const Vec2 a = lshape[t[i]], b = lshape[t[i + 1]], c = lshape[t[i + 2]];
        const double tri = 0.5 * ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
        CHECK(tri > 0);
        sum += tri;
    }
    CHECK(sum == doctest::Approx(300.0));
    std::vector<Vec2> reversed(lshape.rbegin(), lshape.rend());  // по часовой
    CHECK(triangulate(reversed).size() == 12);
    std::vector<Vec2> closed = square;
    closed.push_back(square.front());
    CHECK(triangulate(closed).size() == 6);
    CHECK(signedArea(square) == doctest::Approx(100.f));
    CHECK(signedArea(std::vector<Vec2>(square.rbegin(), square.rend())) == doctest::Approx(-100.f));
}

TEST_CASE("geometry: вырожденные многоугольники и «бабочка» дают пустой результат") {
    CHECK(triangulate({}).empty());
    CHECK(triangulate({{0, 0}, {1, 1}}).empty());
    CHECK(triangulate({{0, 0}, {1, 1}, {2, 2}}).empty());                   // на одной прямой
    CHECK(triangulate({{0, 0}, {10, 10}, {10, 0}, {0, 10}}).empty());       // восьмёрка: нулевая площадь
}

TEST_CASE("geometry: вогнутый многоугольник со многими вершинами (звезда) — площадь сходится") {
    std::vector<Vec2> star;
    for (int i = 0; i < 20; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 20.f, r = i % 2 ? 4.f : 10.f;
        star.push_back({std::cos(a) * r, std::sin(a) * r});
    }
    const auto t = triangulate(star);
    REQUIRE(t.size() == 3 * 18);
    double sum = 0;
    for (std::size_t i = 0; i < t.size(); i += 3) {
        const Vec2 a = star[t[i]], b = star[t[i + 1]], c = star[t[i + 2]];
        sum += 0.5 * ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
    }
    CHECK(sum == doctest::Approx(signedArea(star)).epsilon(1e-4));
}

TEST_CASE("city: высота зданий и ширина дорог берутся из тегов") {
    const OsmData d = parseFixture();
    CityBuildSettings s;
    CHECK(buildingHeight(*wayById(d, 101), s) == doctest::Approx(4 * 3.2f + 1.f));  // building:levels = 4
    CHECK(buildingHeight(*wayById(d, 102), s) == doctest::Approx(25.f));            // height="25 m"
    CHECK(buildingHeight(*wayById(d, 103), s) == doctest::Approx(s.defaultBuildingHeight));
    CHECK(roadWidth("motorway") > roadWidth("residential"));
    CHECK(roadWidth("residential") > roadWidth("footway"));
    CHECK(roadWidth("proposed") == 0.f);
}

TEST_CASE("city: из фрагмента строятся дорога, дома, парк; мелкое и незамкнутое отбрасывается") {
    const OsmData d = parseFixture();
    CityStats st;
    const auto chunks = buildCity(d, GeoProjection(37.5, 127.0), {}, &st);
    CHECK(st.buildings == 3);  // 101, 102, 103
    CHECK(st.roads == 2);      // 100 и пешеходная 107
    CHECK(st.parks == 1);
    CHECK(st.skipped == 2);    // сарай (мал) и незамкнутый «дом» 106
    REQUIRE_FALSE(chunks.empty());
    GeoMesh buildings, ground;
    for (const auto& [cell, c] : chunks) {
        buildings.append(c.buildings);
        ground.append(c.ground);
    }
    CHECK(allValid(buildings));
    CHECK(allValid(ground));
    CHECK(wrongFacing(buildings) == 0);
    CHECK(wrongFacing(ground) == 0);
    float maxY = 0;
    for (const auto& v : buildings.vertices) maxY = std::max(maxY, v.position[1]);
    CHECK(maxY == doctest::Approx(25.f));  // самое высокое — дом 102
    CHECK(st.minX < st.maxX);
    CHECK(st.minZ < st.maxZ);
}

TEST_CASE("city: крыша дома равна площади контура, стены — периметру на высоту") {
    OsmData d;
    std::string error;
    // Прямоугольник 17.7 x 22.2 м (0.0002° по долготе и широте на 37.5°), высота 10 м.
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5000" lon="127.0000"/><node id="2" lat="37.5000" lon="127.0002"/>
        <node id="3" lat="37.5002" lon="127.0002"/><node id="4" lat="37.5002" lon="127.0000"/>
        <way id="7"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="4"/><nd ref="1"/><tag k="building" v="yes"/><tag k="height" v="10"/></way></osm>)", d, error));
    const GeoProjection proj(37.5, 127.0);
    const auto chunks = buildCity(d, proj, {});
    REQUIRE(chunks.size() == 1);
    const GeoMesh& m = chunks.begin()->second.buildings;
    float x0, z0, x1, z1;
    proj.toLocal(37.5, 127.0, x0, z0);
    proj.toLocal(37.5002, 127.0002, x1, z1);
    const float w = std::fabs(x1 - x0), h = std::fabs(z1 - z0);
    CHECK(areaAtHeight(m, 10.f) == doctest::Approx(w * h).epsilon(0.001));
    double walls = 0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
        if (a.normal[1] != 0.f) continue;  // только вертикальные стены
        const float ux = b.position[0] - a.position[0], uy = b.position[1] - a.position[1], uz = b.position[2] - a.position[2];
        const float vx = c.position[0] - a.position[0], vy = c.position[1] - a.position[1], vz = c.position[2] - a.position[2];
        const float gx = uy * vz - uz * vy, gy = uz * vx - ux * vz, gz = ux * vy - uy * vx;
        walls += 0.5 * std::sqrt(gx * gx + gy * gy + gz * gz);
    }
    CHECK(walls == doctest::Approx(2.0 * (w + h) * 10.0).epsilon(0.001));
}

TEST_CASE("city: направление обхода контура не влияет — оба варианта смотрят наружу") {
    const std::string cw = R"(<osm><node id="1" lat="37.5000" lon="127.0000"/><node id="2" lat="37.5000" lon="127.0002"/>
        <node id="3" lat="37.5002" lon="127.0002"/><node id="4" lat="37.5002" lon="127.0000"/>
        <way id="7">%s<tag k="building" v="yes"/></way></osm>)";
    for (const char* order : {"<nd ref=\"1\"/><nd ref=\"2\"/><nd ref=\"3\"/><nd ref=\"4\"/><nd ref=\"1\"/>", "<nd ref=\"1\"/><nd ref=\"4\"/><nd ref=\"3\"/><nd ref=\"2\"/><nd ref=\"1\"/>"}) {
        std::string text = cw;
        text.replace(text.find("%s"), 2, order);
        OsmData d;
        std::string error;
        REQUIRE(parseOsmXml(text, d, error));
        const auto chunks = buildCity(d, GeoProjection(37.5, 127.0), {});
        REQUIRE(chunks.size() == 1);
        CHECK(wrongFacing(chunks.begin()->second.buildings) == 0);
    }
}

TEST_CASE("city: объекты раскладываются по ячейкам, длинная дорога делится между ячейками") {
    OsmData d;
    std::string error;
    // Дорога вдоль востока на ~1.7 км (ячейка 256 м) и два дома, один у начала, другой у конца.
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5" lon="127.0000"/><node id="2" lat="37.5" lon="127.0100"/><node id="3" lat="37.5" lon="127.0200"/>
        <node id="10" lat="37.5001" lon="127.0001"/><node id="11" lat="37.5001" lon="127.0003"/><node id="12" lat="37.5003" lon="127.0003"/><node id="13" lat="37.5003" lon="127.0001"/>
        <node id="20" lat="37.5001" lon="127.0191"/><node id="21" lat="37.5001" lon="127.0193"/><node id="22" lat="37.5003" lon="127.0193"/><node id="23" lat="37.5003" lon="127.0191"/>
        <way id="1"><nd ref="1"/><nd ref="2"/><nd ref="3"/><tag k="highway" v="primary"/></way>
        <way id="2"><nd ref="10"/><nd ref="11"/><nd ref="12"/><nd ref="13"/><nd ref="10"/><tag k="building" v="yes"/></way>
        <way id="3"><nd ref="20"/><nd ref="21"/><nd ref="22"/><nd ref="23"/><nd ref="20"/><tag k="building" v="yes"/></way></osm>)", d, error));
    const auto chunks = buildCity(d, GeoProjection(37.5, 127.0), {});
    int withBuildings = 0, withRoad = 0;
    for (const auto& [cell, c] : chunks) {
        withBuildings += c.buildingCount > 0 ? 1 : 0;
        withRoad += c.roadSegments > 0 ? 1 : 0;
    }
    CHECK(withBuildings == 2);
    CHECK(withRoad == 2);  // два сегмента по ~880 м лежат в серединах разных ячеек
}

TEST_CASE("city + streaming: ячейки города подгружаются по мере движения вдоль улицы") {
    OsmData d;
    std::string error;
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5" lon="127.0000"/><node id="2" lat="37.5" lon="127.0100"/>
        <way id="1"><nd ref="1"/><nd ref="2"/><tag k="highway" v="residential"/></way></osm>)", d, error));
    const auto chunks = buildCity(d, GeoProjection(37.5, 127.0), {});
    struct Loader final : ICellLoader {
        StreamingManager* m = nullptr;
        const std::map<CellCoord, CityChunk>* chunks = nullptr;
        std::vector<CellCoord> loaded;
        void beginLoad(CellCoord c) override { loaded.push_back(c); m->onLoaded(c, chunks->count(c) ? chunks->at(c).ground.vertices.size() * sizeof(GeoVertex) : 0); }
        void unload(CellCoord) override {}
    } loader;
    StreamingSettings s;
    s.loadRadius = 400.f;
    s.unloadRadius = 600.f;
    StreamingManager m(s, loader);
    loader.m = &m;
    loader.chunks = &chunks;
    for (int f = 0; f < 200; ++f) m.update(static_cast<float>(f) * 4.f, 0.f, 1.f, 0.f);
    CHECK(loader.loaded.size() > 6);
    CHECK(m.stateOf(m.grid().cellOf(800.f, 0.f)) == CellState::Loaded);
}

TEST_CASE("city: реальный район Мапо (если файл скачан) строится без ошибок геометрии") {
    const auto file = std::filesystem::path(HAN_REPO_DIR) / "assets_src" / "osm" / "mapo.osm";
    if (!std::filesystem::exists(file)) return;  // файл не в Git (лицензия ODbL): появляется после han_osm_fetch.py
    OsmData d;
    std::string error;
    REQUIRE_MESSAGE(loadOsmFile(file.string(), d, error), error);
    CityStats st;
    const auto chunks = buildCity(d, GeoProjection(37.5572, 126.9230), {}, &st);
    CHECK(st.buildings > 500);
    CHECK(st.roads > 200);
    int wrong = 0;
    std::size_t triangles = 0;
    for (const auto& [cell, c] : chunks) {
        CHECK(allValid(c.buildings));
        CHECK(allValid(c.ground));
        wrong += wrongFacing(c.buildings) + wrongFacing(c.ground);
        triangles += (c.buildings.indices.size() + c.ground.indices.size()) / 3;
    }
    CHECK(wrong == 0);
    CHECK(triangles > 10000);
    CHECK(st.maxX - st.minX < 3000.f);
}

TEST_CASE("city: части сетки по материалам — дороги, парк, стены и крыши; индексы покрыты без пропусков; UV непрерывны вдоль стены") {
    const OsmData d = parseFixture();
    const auto chunks = buildCity(d, GeoProjection(37.5, 127.0), {});
    bool sawRoad = false, sawPark = false, sawWall = false, sawRoof = false;
    for (const auto& [cell, chunk] : chunks) {
        for (const GeoMesh* mesh : {&chunk.ground, &chunk.buildings}) {
            if (mesh->empty()) continue;
            REQUIRE_FALSE(mesh->ranges.empty());
            std::uint32_t expected = 0;
            std::vector<int> seen(8, 0);
            for (const GeoRange& r : mesh->ranges) {
                CHECK(r.firstIndex == expected);  // подряд, без дыр
                CHECK(r.indexCount % 3 == 0);
                expected += r.indexCount;
                ++seen[static_cast<std::size_t>(r.material)];
                sawRoad |= r.material == GeoMaterial::Road;
                sawPark |= r.material == GeoMaterial::Park;
                sawWall |= r.material == GeoMaterial::WallBrick || r.material == GeoMaterial::WallPlaster || r.material == GeoMaterial::WallConcrete;
                sawRoof |= r.material == GeoMaterial::Roof;
            }
            CHECK(expected == mesh->indices.size());
            for (int count : seen) CHECK(count <= 1);  // после группировки — одна часть на материал
        }
    }
    CHECK(sawRoad);
    CHECK(sawPark);
    CHECK(sawWall);
    CHECK(sawRoof);
}

TEST_CASE("city: стены размечены UV в плитках (метры / размер плитки), у крыши и дороги UV планарные") {
    const OsmData d = parseFixture();
    const auto chunks = buildCity(d, GeoProjection(37.5, 127.0), {});
    for (const auto& [cell, chunk] : chunks) {
        for (const GeoVertex& v : chunk.ground.vertices) {
            // Плоские материалы: UV пропорциональны координатам XZ (плитка 6 м у дорог, 8 м у парков).
            const bool road = std::fabs(v.uv[0] * 6.f - v.position[0]) < 1e-2f && std::fabs(v.uv[1] * 6.f - v.position[2]) < 1e-2f;
            const bool park = std::fabs(v.uv[0] * 8.f - v.position[0]) < 1e-2f && std::fabs(v.uv[1] * 8.f - v.position[2]) < 1e-2f;
            CHECK((road || park));
        }
        for (const GeoVertex& v : chunk.buildings.vertices) {
            if (std::fabs(v.normal[1]) < 0.5f) {  // стена: v по высоте в плитках по 5 м, 0 вверху
                CHECK(v.uv[1] >= -1e-4f);
                CHECK(v.uv[1] <= 300.f / 5.f + 1e-3f);
            }
        }
    }
}

TEST_CASE("geometry: tag сливает соседние части, append сдвигает части, groupByMaterial собирает материал в одну часть") {
    GeoMesh a;
    a.vertices.resize(3);
    a.indices = {0, 1, 2};
    a.tag(GeoMaterial::Road, 0);
    a.indices.insert(a.indices.end(), {0, 1, 2});
    a.tag(GeoMaterial::Road, 3);  // тот же материал подряд — сливается
    REQUIRE(a.ranges.size() == 1);
    CHECK(a.ranges[0].indexCount == 6);

    GeoMesh b;
    b.vertices.resize(3);
    b.indices = {0, 1, 2};
    b.tag(GeoMaterial::Roof, 0);
    a.append(b);
    CHECK(a.indices.size() == 9);
    REQUIRE(a.ranges.size() == 2);
    CHECK(a.ranges[1].firstIndex == 6);
    CHECK(a.indices[6] == 3);  // индексы второго меша сдвинуты на число вершин первого

    GeoMesh c;
    c.vertices.resize(9);
    c.indices = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    c.tag(GeoMaterial::Roof, 0);
    c.ranges = {{0, 3, GeoMaterial::Roof}, {3, 3, GeoMaterial::WallBrick}, {6, 3, GeoMaterial::Roof}};
    c.groupByMaterial();
    REQUIRE(c.ranges.size() == 2);
    CHECK(c.ranges[0].material == GeoMaterial::WallBrick);
    CHECK(c.ranges[0].indexCount == 3);
    CHECK(c.ranges[1].material == GeoMaterial::Roof);
    CHECK(c.ranges[1].indexCount == 6);
    CHECK(c.indices.size() == 9);
    CHECK(c.indices[0] == 3);  // кирпичные треугольники теперь первые
}
