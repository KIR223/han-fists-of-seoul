// Тесты рельефа (E17): сетка высот, билинейная выборка, нормали, сетка земли по ячейкам, город на склоне (крыши плоские, низ стен на земле).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

#include "han/world/city_builder.h"
#include "han/world/osm.h"
#include "han/world/terrain.h"

using namespace han::world;

namespace {
// Склон: высота растёт на восток на 0,1 м на метр, от 0 до 100 м на 1000 м; 41 x 21 узлов, шаг 25 м.
Heightmap slope() {
    std::vector<float> h;
    for (int j = 0; j < 21; ++j)
        for (int i = 0; i < 41; ++i) h.push_back(static_cast<float>(i) * 2.5f);
    return Heightmap::fromGrid(0.f, 0.f, 25.f, 41, 21, h);
}
}  // namespace

TEST_CASE("terrain: билинейная высота, края не обрываются, нормаль наклонена против склона") {
    const Heightmap m = slope();
    REQUIRE(m.valid());
    CHECK(m.heightAt(0.f, 0.f) == doctest::Approx(0.f));
    CHECK(m.heightAt(100.f, 50.f) == doctest::Approx(10.f));
    CHECK(m.heightAt(112.5f, 30.f) == doctest::Approx(11.25f));  // между узлами
    CHECK(m.heightAt(-500.f, 0.f) == doctest::Approx(0.f));      // за краем — значение у края
    CHECK(m.heightAt(5000.f, 0.f) == doctest::Approx(100.f).epsilon(0.01));
    CHECK(m.minHeight() == doctest::Approx(0.f));
    CHECK(m.maxHeight() == doctest::Approx(100.f));
    float nx, ny, nz;
    m.normalAt(500.f, 250.f, nx, ny, nz);
    CHECK(nx < 0.f);                       // склон растёт к востоку, нормаль смотрит на запад
    CHECK(nx == doctest::Approx(-0.0995f).epsilon(0.02));
    CHECK(std::fabs(nz) < 1e-3f);
    CHECK(nx * nx + ny * ny + nz * nz == doctest::Approx(1.f));
    CHECK_FALSE(Heightmap().valid());
    CHECK_FALSE(Heightmap::fromGrid(0, 0, 25, 3, 3, {1, 2}).valid());  // размер не сходится
    Heightmap bad;
    std::string error;
    CHECK_FALSE(bad.load("нет_рельефа.hht", error));
}

TEST_CASE("terrain: сетка земли ячейки — число вершин и треугольников, нормали вверх, обход против часовой сверху, ниже поверхности на sink") {
    const Heightmap m = slope();
    TerrainSettings s;
    const GeoMesh mesh = buildTerrainCell(m, {1, 1}, s);
    const int n = static_cast<int>(std::round(s.cellSize / s.spacing));
    REQUIRE(!mesh.empty());
    CHECK(mesh.vertices.size() == static_cast<std::size_t>((n + 1) * (n + 1)));
    CHECK(mesh.indices.size() == static_cast<std::size_t>(n * n * 6));
    REQUIRE(mesh.ranges.size() == 1);
    CHECK(mesh.ranges[0].material == GeoMaterial::Terrain);
    for (const GeoVertex& v : mesh.vertices) {
        CHECK(v.normal[1] > 0.9f);
        CHECK(v.position[1] == doctest::Approx(m.heightAt(v.position[0], v.position[2]) - s.sink));
    }
    int down = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const GeoVertex &a = mesh.vertices[mesh.indices[i]], &b = mesh.vertices[mesh.indices[i + 1]], &c = mesh.vertices[mesh.indices[i + 2]];
        const float ux = b.position[0] - a.position[0], uy = b.position[1] - a.position[1], uz = b.position[2] - a.position[2];
        const float vx = c.position[0] - a.position[0], vy = c.position[1] - a.position[1], vz = c.position[2] - a.position[2];
        const float gy = uz * vx - ux * vz;
        if (gy <= 0.f) ++down;
    }
    CHECK(down == 0);  // все грани смотрят вверх
    CHECK(buildTerrainCell(m, {50, 50}).empty());  // вне рельефа
}

TEST_CASE("terrain: дом на склоне — крыша плоская на отметке самой высокой точки земли, низ стен на земле и под ней, дороги идут по склону") {
    const Heightmap m = slope();
    OsmData d;
    std::string error;
    // Дом 40 x 20 м, вытянут вдоль склона (запад-восток), высотой 10 м; рядом дорога вдоль склона.
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5000" lon="127.0000"/><node id="2" lat="37.5000" lon="127.0005"/>
        <node id="3" lat="37.5002" lon="127.0005"/><node id="4" lat="37.5002" lon="127.0000"/>
        <node id="10" lat="37.4990" lon="127.0000"/><node id="11" lat="37.4990" lon="127.0010"/>
        <way id="7"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="4"/><nd ref="1"/><tag k="building" v="yes"/><tag k="height" v="10"/></way>
        <way id="8"><nd ref="10"/><nd ref="11"/><tag k="highway" v="residential"/></way></osm>)", d, error));
    // Проекция смещена так, чтобы дом оказался на склоне карты (x ≥ 0, z ≥ 0)
    const GeoProjection proj(37.4995, 126.9990);
    CityBuildSettings settings;
    settings.terrain = &m;
    settings.buildingDetail = false;
    settings.streetProps = false;
    settings.terrainMesh = false;
    const auto chunks = buildCity(d, proj, settings);
    REQUIRE_FALSE(chunks.empty());
    float roofY = -1e9f, wallMin = 1e9f;
    int roofVerts = 0;
    float roadMin = 1e9f, roadMax = -1e9f;
    for (const auto& [cell, c] : chunks) {
        for (const GeoVertex& v : c.buildings.vertices) {
            if (v.normal[1] > 0.9f) {
                roofY = std::max(roofY, v.position[1]);
                ++roofVerts;
            } else {
                wallMin = std::min(wallMin, v.position[1]);
            }
        }
        for (const GeoVertex& v : c.ground.vertices) {
            roadMin = std::min(roadMin, v.position[1]);
            roadMax = std::max(roadMax, v.position[1]);
        }
    }
    REQUIRE(roofVerts >= 4);
    float lowestGround = 1e9f, highestGround = -1e9f;
    for (const auto& [cell, c] : chunks)
        for (const GeoVertex& v : c.buildings.vertices)
            if (v.normal[1] > 0.9f) {
                CHECK(v.position[1] == doctest::Approx(roofY));  // крыша плоская
                lowestGround = std::min(lowestGround, m.heightAt(v.position[0], v.position[2]));
                highestGround = std::max(highestGround, m.heightAt(v.position[0], v.position[2]));
            }
    CHECK(highestGround - lowestGround > 1.f);                       // склон под домом заметный
    CHECK(roofY == doctest::Approx(highestGround + 10.f).epsilon(0.001));
    CHECK(wallMin < lowestGround);                                   // низ стены ниже самой низкой точки земли (с заглублением)
    CHECK(roadMax - roadMin > 2.f);                                  // дорога идёт по склону, а не по плоскости
}

TEST_CASE("terrain: без рельефа город строится плоским, как раньше; с рельефом добавляется сетка земли в ячейки города и вокруг") {
    const Heightmap m = slope();
    OsmData d;
    std::string error;
    REQUIRE(parseOsmXml(R"(<osm><node id="1" lat="37.5000" lon="127.0000"/><node id="2" lat="37.5000" lon="127.0005"/>
        <node id="3" lat="37.5002" lon="127.0005"/><node id="4" lat="37.5002" lon="127.0000"/>
        <way id="7"><nd ref="1"/><nd ref="2"/><nd ref="3"/><nd ref="4"/><nd ref="1"/><tag k="building" v="yes"/></way></osm>)", d, error));
    const GeoProjection proj(37.4995, 126.9990);
    CityBuildSettings flat;
    flat.buildingDetail = false;
    flat.streetProps = false;
    const auto flatChunks = buildCity(d, proj, flat);
    for (const auto& [cell, c] : flatChunks)
        for (const GeoVertex& v : c.buildings.vertices) CHECK(v.position[1] >= 0.f);
    CityBuildSettings hills = flat;
    hills.terrain = &m;
    const auto hillChunks = buildCity(d, proj, hills);
    CHECK(hillChunks.size() > flatChunks.size());  // добавились ячейки земли вокруг
    bool terrainFound = false;
    for (const auto& [cell, c] : hillChunks)
        for (const GeoRange& r : c.ground.ranges) terrainFound |= r.material == GeoMaterial::Terrain;
    CHECK(terrainFound);
}

TEST_CASE("terrain: реальный рельеф Сеула (если скачан) — читается, Мапо в разумных высотах, Намсан выше; файл не HHT1 отклоняется") {
    const auto file = std::filesystem::path(HAN_REPO_DIR) / "assets_bin/terrain/seoul.hht";
    if (!std::filesystem::exists(file)) {
        MESSAGE("нет assets_bin/terrain/seoul.hht (han_terrain_fetch.py) — проверка пропущена");
        return;
    }
    Heightmap m;
    std::string error;
    REQUIRE_MESSAGE(m.load(file.string(), error), error);
    CHECK(m.width() > 500);
    const float mapo = m.heightAt(0.f, 0.f);
    CHECK(mapo > 5.f);
    CHECK(mapo < 80.f);
    CHECK(m.maxHeight() > 200.f);  // в зоне есть горы (Намсан, Бугаксан)
    const auto bad = std::filesystem::temp_directory_path() / "han_bad.hht";
    std::ofstream(bad, std::ios::binary) << "NOPE";
    CHECK_FALSE(Heightmap().load(bad.string(), error));
}

#include "han/physics/physics_world.h"
#include "han/world/terrain_physics.h"

TEST_CASE("terrain physics: луч сверху попадает в склон на высоте рельефа, швы между плитками не заметны, дальние плитки убираются") {
    auto world = han::physics::PhysicsWorld::create();
    if (!world) {
        MESSAGE("физика собрана без Jolt — пропуск");
        return;
    }
    // Большой склон: 120 x 60 узлов с шагом 25 м (3 км x 1,5 км), высота растёт к востоку и немного к югу
    std::vector<float> h;
    for (int j = 0; j < 60; ++j)
        for (int i = 0; i < 120; ++i) h.push_back(static_cast<float>(i) * 1.5f + static_cast<float>(j) * 0.5f);
    const Heightmap map = Heightmap::fromGrid(0.f, 0.f, 25.f, 120, 60, h);
    TerrainPhysics terrain(*world, map);
    CHECK(terrain.tileCount() == 0);
    CHECK(terrain.update(400.f, 300.f) > 0);
    const std::size_t afterFirst = terrain.tileCount();
    CHECK(afterFirst >= 2);
    CHECK(terrain.update(400.f, 300.f) == 0);  // повторно ничего нового
    // Лучи через весь диапазон x, включая стык плиток (787,5 м): высота совпадает с рельефом до сантиметров
    for (float x = 20.f; x < 1500.f; x += 37.5f) {
        const float z = 300.f;
        const auto hit = world->raycast({x, 500.f, z}, {x, -100.f, z});
        REQUIRE_MESSAGE(hit.hit, "нет попадания на x=" << x);
        CHECK_MESSAGE(hit.point.y == doctest::Approx(map.heightAt(x, z)).epsilon(0.002), "x=" << x);
        CHECK(hit.normal.y > 0.9f);
    }
    // Игрок уехал на 2,5 км на восток: старые плитки убраны, новые созданы
    CHECK(terrain.update(2900.f, 300.f) > 0);
    CHECK_FALSE(world->raycast({100.f, 500.f, 300.f}, {100.f, -100.f, 300.f}).hit);
    CHECK(world->raycast({2900.f, 500.f, 300.f}, {2900.f, -100.f, 300.f}).hit);
    terrain.clear();
    CHECK(terrain.tileCount() == 0);
    CHECK_FALSE(world->raycast({2900.f, 500.f, 300.f}, {2900.f, -100.f, 300.f}).hit);
    // Неверные параметры сетки отклоняются
    CHECK(world->addStaticHeightfield(0.f, 0.f, 10.f, 5, std::vector<float>(25, 0.f)) == han::contracts::kNoEntity);   // нечётное число отсчётов
    CHECK(world->addStaticHeightfield(0.f, 0.f, 10.f, 8, std::vector<float>(10, 0.f)) == han::contracts::kNoEntity);  // размер не сходится
}

TEST_CASE("terrain: дальний план — одна грубая сетка на всю карту, ниже ближней земли на sink, края совпадают с рельефом, грани вверх") {
    const Heightmap m = slope();  // 1000 x 500 м
    const GeoMesh far = buildFarTerrain(m, 100.f, 6.f);
    REQUIRE_FALSE(far.empty());
    CHECK(far.vertices.size() == static_cast<std::size_t>((10 + 1) * (5 + 1)));  // 1000/100 x 500/100
    CHECK(far.indices.size() == 10u * 5u * 6u);
    float minX = 1e9f, maxX = -1e9f;
    for (const GeoVertex& v : far.vertices) {
        minX = std::min(minX, v.position[0]);
        maxX = std::max(maxX, v.position[0]);
        CHECK(v.position[1] == doctest::Approx(m.heightAt(v.position[0], v.position[2]) - 6.f));
        CHECK(v.normal[1] > 0.9f);
    }
    CHECK(minX == doctest::Approx(m.minX()));
    CHECK(maxX == doctest::Approx(m.maxX()));
    int down = 0;
    for (std::size_t i = 0; i < far.indices.size(); i += 3) {
        const GeoVertex &a = far.vertices[far.indices[i]], &b = far.vertices[far.indices[i + 1]], &c = far.vertices[far.indices[i + 2]];
        const float gy = (b.position[2] - a.position[2]) * (c.position[0] - a.position[0]) - (b.position[0] - a.position[0]) * (c.position[2] - a.position[2]);
        down += gy <= 0.f ? 1 : 0;
    }
    CHECK(down == 0);
    CHECK(buildFarTerrain(Heightmap(), 100.f).empty());
    CHECK(far.ranges.size() == 1);
}
