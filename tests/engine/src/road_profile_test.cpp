// Мосты и эстакады (этап 03): профиль высоты полотна, геометрия, граф трафика и физический проход.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "han/ai/traffic.h"
#include "han/physics/physics_world.h"
#include "han/world/city_builder.h"
#include "han/world/osm.h"
#include "han/world/road_profile.h"
#include "han/world/terrain.h"

using namespace han::world;

namespace {
const GeoProjection kProjection(37.557, 126.914);

struct PointDef { long id; float x, z; };
struct WayDef { long id; std::vector<long> nodes; std::string tags; };

// Синтетическая карта в метрах: узлы переводятся в широту/долготу той же проекцией, которой читает движок.
std::string osmText(const std::vector<PointDef>& points, const std::vector<WayDef>& ways) {
    std::ostringstream out;
    out.precision(10);
    out << "<?xml version=\"1.0\"?>\n<osm version=\"0.6\">\n";
    for (const auto& p : points) {
        double lat, lon;
        kProjection.toGeo(p.x, p.z, lat, lon);
        out << " <node id=\"" << p.id << "\" lat=\"" << lat << "\" lon=\"" << lon << "\"/>\n";
    }
    for (const auto& w : ways) {
        out << " <way id=\"" << w.id << "\">";
        for (const auto n : w.nodes) out << "<nd ref=\"" << n << "\"/>";
        out << w.tags << "</way>\n";
    }
    out << "</osm>\n";
    return out.str();
}

OsmData parse(const std::string& text) {
    OsmData data;
    std::string error;
    REQUIRE_MESSAGE(parseOsmXml(text, data, error), error);
    return data;
}

Heightmap flatTerrain(float height = 0.f) {
    return Heightmap::fromGrid(-2000.f, -2000.f, 50.f, 81, 81, std::vector<float>(81 * 81, height));
}

const char* kRoad = "<tag k=\"highway\" v=\"primary\"/>";

// Прямая дорога вдоль x: земля — мост из четырёх участков — земля.
OsmData straightBridge(const char* bridgeTags = "<tag k=\"highway\" v=\"primary\"/><tag k=\"bridge\" v=\"yes\"/><tag k=\"layer\" v=\"1\"/>") {
    return parse(osmText({{1, -300, 0}, {2, -200, 0}, {3, -100, 0}, {4, 0, 0}, {5, 100, 0}, {6, 200, 0}, {7, 300, 0}},
                         {{100, {1, 2}, kRoad}, {101, {2, 3, 4, 5, 6}, bridgeTags}, {102, {6, 7}, kRoad}}));
}

RoadProfileSettings repoSettings() {
    RoadProfileSettings s;
    std::string error;
    REQUIRE_MESSAGE(loadRoadProfileSettings((std::filesystem::path(HAN_REPO_DIR) / "data/city/road_profile.json").string(), s, error), error);
    return s;
}

CityStyle repoStyle() {
    CityStyle style;
    std::string error;
    REQUIRE_MESSAGE(loadCityStyle((std::filesystem::path(HAN_REPO_DIR) / "data/city/streetscape.json").string(), style, error), error);
    return style;
}
}  // namespace

TEST_CASE("road_profile: настройки из data/city читаются, ошибки отклоняются без порчи значений") {
    const auto s = repoSettings();
    CHECK(s.maxGrade > 0.f);
    CHECK(s.maxGrade <= 0.1f);
    CHECK(s.clearanceLayer2 >= s.clearanceLayer1);
    const auto path = std::filesystem::temp_directory_path() / "han_road_profile_bad.json";
    const auto write = [&](const char* text) { std::FILE* f = std::fopen(path.string().c_str(), "wb"); std::fputs(text, f); std::fclose(f); };
    RoadProfileSettings kept;
    kept.maxGrade = 0.123f;
    std::string error;
    write("{\"version\":2}");
    CHECK_FALSE(loadRoadProfileSettings(path.string(), kept, error));
    write("{\"version\":1,\"max_grade\":5,\"max_height_above_ground\":30,\"clearance_layer1\":6,\"clearance_layer2\":10,\"clearance_layer3\":14,\"deck_thickness\":1,\"pier_spacing\":40,\"pier_size\":1.6,\"min_pier_clearance\":2.5,\"collider_step\":2.5,\"collider_thickness\":0.6}");
    CHECK_FALSE(loadRoadProfileSettings(path.string(), kept, error));
    CHECK(kept.maxGrade == doctest::Approx(0.123f));  // прежнее значение не тронуто
    write("{\"version\":1,\"max_grade\":0.07,\"max_height_above_ground\":30,\"clearance_layer1\":10,\"clearance_layer2\":6,\"clearance_layer3\":14,\"deck_thickness\":1,\"pier_spacing\":40,\"pier_size\":1.6,\"min_pier_clearance\":2.5,\"collider_step\":2.5,\"collider_thickness\":0.6}");
    CHECK_FALSE(loadRoadProfileSettings(path.string(), kept, error));  // габариты слоёв не должны убывать
    std::filesystem::remove(path);
}

TEST_CASE("road_profile: мост поднимается на габарит, концы стоят на земле, уклон не больше допустимого") {
    const auto data = straightBridge();
    const auto terrain = flatTerrain(0.f);
    const auto settings = repoSettings();
    const auto profile = computeRoadProfile(data, kProjection, &terrain, settings);
    REQUIRE_FALSE(profile.empty());
    CHECK(profile.stats().bridgeWays == 1);
    CHECK(profile.height(2) == doctest::Approx(0.f).epsilon(0.01).scale(1.0));
    CHECK(profile.height(6) == doctest::Approx(0.f).epsilon(0.01).scale(1.0));
    CHECK(profile.height(4) == doctest::Approx(settings.clearanceLayer1).epsilon(0.01));  // середина достигает габарита
    CHECK(std::isnan(profile.height(1)));  // обычная дорога вне профиля
    CHECK(std::isnan(profile.height(7)));
    const long order[] = {2, 3, 4, 5, 6};
    for (int i = 0; i + 1 < 5; ++i) {
        const float length = 100.f;
        CHECK(std::fabs(profile.height(order[i + 1]) - profile.height(order[i])) <= settings.maxGrade * length + 1e-3f);
    }
    CHECK(profile.stats().maxGradeUsed <= settings.maxGrade + 1e-4f);
}

TEST_CASE("road_profile: слой 2 поднимает выше слоя 1; короткая эстакада не превышает допустимый уклон от концов") {
    const auto terrain = flatTerrain(0.f);
    const auto settings = repoSettings();
    const auto layer2 = computeRoadProfile(straightBridge("<tag k=\"highway\" v=\"primary\"/><tag k=\"bridge\" v=\"yes\"/><tag k=\"layer\" v=\"2\"/>"), kProjection, &terrain, settings);
    CHECK(layer2.height(4) == doctest::Approx(settings.clearanceLayer2).epsilon(0.01));
    CHECK(layer2.height(4) > settings.clearanceLayer1);
    // Пролёт 40 м: от концов можно подняться только на maxGrade * 20 м, габарит недостижим — полотно не «висит» ступенью.
    const auto data = parse(osmText({{1, 0, -60}, {2, 0, -20}, {3, 0, 0}, {4, 0, 20}, {5, 0, 60}},
                                    {{100, {1, 2}, kRoad}, {101, {2, 3, 4}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"bridge\" v=\"yes\"/>"}, {102, {4, 5}, kRoad}}));
    const auto flat = computeRoadProfile(data, kProjection, &terrain, settings);
    CHECK(flat.height(3) <= settings.maxGrade * 20.f + 1e-3f);
    CHECK(flat.height(3) > 0.5f);
    CHECK(flat.height(2) == doctest::Approx(0.f).epsilon(0.01).scale(1.0));
}

TEST_CASE("road_profile: цепочка мостов делит узел — одна отметка, без съезда посреди эстакады; полотно не повторяет рельеф под рекой") {
    // Два пути моста стыкуются в узле 3, который не лежит на обычной дороге: съезд к земле там не нужен.
    const auto data = parse(osmText({{1, -400, 0}, {2, -300, 0}, {3, 0, 0}, {4, 300, 0}, {5, 400, 0}},
                                    {{100, {1, 2}, kRoad}, {101, {2, 3}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"bridge\" v=\"yes\"/>"},
                                     {102, {3, 4}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"bridge\" v=\"yes\"/>"}, {103, {4, 5}, kRoad}}));
    // Рельеф с «рекой» в центре: DEM там ниже берегов на 8 м. Габарит считается от DEM в узле (ограничение модели: уровень берега и подпорных стенок
    // на пролёте не учитывается — для настоящей реки нужна отметка воды/набережной, этап 03 не закрывает её полностью).
    std::vector<float> heights(81 * 81, 0.f);
    for (int iz = 0; iz < 81; ++iz)
        for (int ix = 0; ix < 81; ++ix) {
            const float x = -2000.f + static_cast<float>(ix) * 50.f;
            if (std::fabs(x) < 150.f) heights[static_cast<std::size_t>(iz * 81 + ix)] = -8.f;
        }
    const auto terrain = Heightmap::fromGrid(-2000.f, -2000.f, 50.f, 81, 81, heights);
    const auto settings = repoSettings();
    const auto profile = computeRoadProfile(data, kProjection, &terrain, settings);
    CHECK(profile.stats().bridgeWays == 2);
    CHECK(profile.height(3) == doctest::Approx(-8.f + settings.clearanceLayer1).epsilon(0.02).scale(1.0));  // габарит над DEM в узле
    CHECK(profile.height(3) >= terrain.heightAt(0.f, 0.f) + settings.clearanceLayer1 - 0.01f);
    CHECK(profile.height(2) == doctest::Approx(terrain.heightAt(-300.f, 0.f)).epsilon(0.01).scale(1.0));
}

TEST_CASE("road_profile: без рельефа и без мостов профиль пуст и ничего не ломает") {
    const auto none = parse(osmText({{1, 0, 0}, {2, 50, 0}}, {{100, {1, 2}, kRoad}}));
    CHECK(computeRoadProfile(none, kProjection, nullptr, repoSettings()).empty());
    const auto flat = computeRoadProfile(straightBridge(), kProjection, nullptr, repoSettings());
    CHECK(flat.height(4) == doctest::Approx(repoSettings().clearanceLayer1).epsilon(0.01));  // плоская земля y = 0
}

TEST_CASE("road_profile: туннели считаются, но не поднимаются и не опускаются (пока не обработаны)") {
    const auto data = parse(osmText({{1, 0, 0}, {2, 100, 0}}, {{100, {1, 2}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"tunnel\" v=\"yes\"/>"}}));
    const auto terrain = flatTerrain(0.f);
    const auto profile = computeRoadProfile(data, kProjection, &terrain, repoSettings());
    CHECK(profile.empty());
    CHECK(profile.stats().tunnelWays == 1);
}

TEST_CASE("road_profile: геометрия города — полотно на габарите, опоры и коллайдеры; без моста всё по рельефу") {
    const auto data = straightBridge();
    const auto terrain = flatTerrain(0.f);
    CityBuildSettings settings;
    settings.style = repoStyle();
    settings.terrain = &terrain;
    settings.roadProfile = repoSettings();
    CityStats stats;
    const auto chunks = buildCity(data, kProjection, settings, &stats);
    CHECK(stats.bridgeWays == 1);
    CHECK(stats.bridgeSegments == 4);
    CHECK(stats.piers > 0);
    CHECK(stats.deckColliders >= 4 * 40);  // 400 м полотна ступеньками по 2,5 м
    float maxY = -1e9f, maxTop = -1e9f;
    for (const auto& [cell, chunk] : chunks) {
        for (const auto& v : chunk.ground.vertices) maxY = std::max(maxY, v.position[1]);
        for (const auto& w : chunk.walls) maxTop = std::max(maxTop, w.center.y + w.half.y);
    }
    CHECK(maxY >= settings.roadProfile.clearanceLayer1);
    CHECK(maxY <= settings.roadProfile.clearanceLayer1 + 0.5f);
    CHECK(maxTop >= settings.roadProfile.clearanceLayer1);

    CityBuildSettings off = settings;
    off.bridges = false;
    CityStats offStats;
    const auto flatChunks = buildCity(data, kProjection, off, &offStats);
    CHECK(offStats.bridgeSegments == 0);
    float flatMax = -1e9f;
    for (const auto& [cell, chunk] : flatChunks)
        for (const auto& v : chunk.ground.vertices) flatMax = std::max(flatMax, v.position[1]);
    CHECK(flatMax < 1.f);
}

TEST_CASE("road_profile: граф трафика — рёбра моста получают высоту полотна, земля — NaN; разные уровни без общего узла не соединяются") {
    const auto data = straightBridge();
    const auto terrain = flatTerrain(0.f);
    const auto settings = repoSettings();
    const auto profile = computeRoadProfile(data, kProjection, &terrain, settings);
    han::ai::RoadGraphSettings graphSettings;
    graphSettings.profile = &profile;
    const auto graph = han::ai::RoadGraph::build(data, kProjection, graphSettings);
    int onBridge = 0, onGround = 0;
    for (const auto& edge : graph.edges()) {
        const float mid = edge.surfaceAt(edge.length * 0.5f);
        if (std::isnan(mid)) ++onGround;
        else {
            ++onBridge;
            CHECK(mid >= 0.f);
            CHECK(mid <= settings.clearanceLayer1 + 1e-3f);
        }
    }
    CHECK(onBridge > 0);
    CHECK(onGround > 0);
    // Эстакада над перекрёстком: пути пересекаются в плане, но узла общего нет — перекрёстка в графе быть не должно.
    const auto cross = parse(osmText({{1, -100, 0}, {2, 100, 0}, {3, 0, -100}, {4, 0, 100}},
                                     {{100, {1, 2}, kRoad}, {101, {3, 4}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"bridge\" v=\"yes\"/><tag k=\"layer\" v=\"1\"/>"}}));
    const auto crossGraph = han::ai::RoadGraph::build(cross, kProjection);
    CHECK(crossGraph.nodes().size() == 4);
    for (const auto& node : crossGraph.nodes()) CHECK_FALSE(node.junction);
}

TEST_CASE("road_profile: машины едут по полотну моста (surfaceY), на земле surfaceY = NaN; высота непрерывна вдоль маршрута") {
    const auto data = straightBridge();
    const auto terrain = flatTerrain(0.f);
    const auto settings = repoSettings();
    const auto profile = computeRoadProfile(data, kProjection, &terrain, settings);
    han::ai::RoadGraphSettings graphSettings;
    graphSettings.profile = &profile;
    const auto graph = han::ai::RoadGraph::build(data, kProjection, graphSettings);
    han::ai::TrafficSettings traffic;
    traffic.maxCars = 12;
    han::ai::TrafficSim sim(graph, traffic);
    int onBridge = 0, onGround = 0;
    float maxStep = 0.f;
    std::vector<std::pair<std::uint32_t, float>> last;
    for (int step = 0; step < 1500; ++step) {
        sim.update(0.1f);
        for (const auto& car : sim.cars()) {
            if (car.edge < 0) continue;
            const bool bridgeEdge = !graph.edges()[static_cast<std::size_t>(car.edge)].ys.empty();
            if (bridgeEdge) {
                ++onBridge;
                REQUIRE(std::isfinite(car.surfaceY));
                CHECK(car.surfaceY >= -1e-3f);
                CHECK(car.surfaceY <= settings.clearanceLayer1 + 1e-3f);
                const auto it = std::find_if(last.begin(), last.end(), [&](const auto& p) { return p.first == car.id; });
                if (it != last.end()) maxStep = std::max(maxStep, std::fabs(car.surfaceY - it->second));
                if (it != last.end()) it->second = car.surfaceY; else last.push_back({car.id, car.surfaceY});
            } else {
                ++onGround;
                CHECK(std::isnan(car.surfaceY));
            }
        }
    }
    CHECK(onBridge > 0);
    CHECK(onGround > 0);
    CHECK(maxStep < 0.5f);  // за шаг 0,1 с машина (до ~22 м/с) меняет высоту полотна плавно, без скачков
}

#ifdef HAN_HAS_JOLT
namespace {
// Идёт персонажем вдоль оси x от x0 до x1; возвращает наименьшую высоту ног в пути и высоту в середине.
struct WalkResult { float minY = 1e9f, midY = 0.f, endX = 0.f, endY = 0.f; bool grounded = false; };
WalkResult walk(const std::vector<WallCollider>& walls, float x0, float x1) {
    auto world = han::physics::PhysicsWorld::create();
    REQUIRE(world);
    world->addStaticBox({0.f, -0.5f, 0.f}, {2000.f, 0.5f, 2000.f});  // земля y = 0
    for (const auto& w : walls) {
        const float h = w.yaw * 0.5f;
        world->addStaticBox({w.center.x, w.center.y, w.center.z}, {w.half.x, w.half.y, w.half.z}, han::contracts::SurfaceMaterial::Concrete, {0.f, std::sin(h), 0.f, std::cos(h)});
    }
    const auto id = world->create({}, {x0, 0.05f, 0.f});
    REQUIRE(id != han::contracts::kNoEntity);
    for (int i = 0; i < 30; ++i) world->step(1.f / 60.f);
    const float direction = x1 > x0 ? 1.f : -1.f;
    WalkResult r;
    for (int i = 0; i < 9000; ++i) {  // 500 м при 4 м/с — около 7500 шагов
        const auto state = world->state(id);
        if ((x1 - state.position.x) * direction <= 0.f) break;
        world->setDesiredVelocity(id, {4.f * direction, 0.f, 0.f});
        world->step(1.f / 60.f);
        const auto s = world->state(id);
        r.minY = std::min(r.minY, s.position.y);
        if (std::fabs(s.position.x) < 4.f) r.midY = s.position.y;
    }
    const auto end = world->state(id);
    r.endX = end.position.x;
    r.endY = end.position.y;
    r.grounded = end.grounded;
    return r;
}
}  // namespace

TEST_CASE("road_profile: физика — пешком по мосту в обе стороны: подъём, середина на габарите, спуск, без падения под полотно") {
    const auto data = straightBridge();
    const auto terrain = flatTerrain(0.f);
    CityBuildSettings settings;
    settings.style = repoStyle();
    settings.terrain = &terrain;
    settings.roadProfile = repoSettings();
    settings.terrainMesh = false;
    std::vector<WallCollider> walls;
    for (const auto& [cell, chunk] : buildCity(data, kProjection, settings)) walls.insert(walls.end(), chunk.walls.begin(), chunk.walls.end());
    REQUIRE_FALSE(walls.empty());
    const float clearance = settings.roadProfile.clearanceLayer1;
    for (const bool forward : {true, false}) {
        const auto r = walk(walls, forward ? -250.f : 250.f, forward ? 250.f : -250.f);
        CAPTURE(forward);
        CHECK(r.midY == doctest::Approx(clearance).epsilon(0.1));   // над серединой — на полотне, а не на земле под ним
        CHECK(r.minY > -0.3f);                                      // не провалился сквозь землю
        CHECK(std::fabs(r.endX) >= 245.f);                          // дошёл до другой стороны
        CHECK(r.endY < 0.6f);                                       // сошёл обратно на землю
        CHECK(r.grounded);
    }
}
#endif

TEST_CASE("road_profile: у машин на мосту есть высоты полотна под осями, перепад между осями не больше уклона на базе; на земле NaN") {
    const auto data = straightBridge();
    const auto terrain = flatTerrain(0.f);
    const auto settings = repoSettings();
    const auto profile = computeRoadProfile(data, kProjection, &terrain, settings);
    han::ai::RoadGraphSettings graphSettings;
    graphSettings.profile = &profile;
    const auto graph = han::ai::RoadGraph::build(data, kProjection, graphSettings);
    han::ai::TrafficSettings traffic;
    traffic.maxCars = 12;
    han::ai::TrafficSim sim(graph, traffic);
    int onBridge = 0;
    float maxDrop = 0.f;
    for (int step = 0; step < 1500; ++step) {
        sim.update(0.1f);
        for (const auto& car : sim.cars()) {
            if (car.edge < 0) continue;
            const bool bridgeEdge = !graph.edges()[static_cast<std::size_t>(car.edge)].ys.empty();
            if (bridgeEdge) {
                ++onBridge;
                REQUIRE(std::isfinite(car.surfaceYFront));
                REQUIRE(std::isfinite(car.surfaceYRear));
                maxDrop = std::max(maxDrop, std::fabs(car.surfaceYFront - car.surfaceYRear));
            } else {
                CHECK(std::isnan(car.surfaceYFront));
                CHECK(std::isnan(car.surfaceYRear));
            }
        }
    }
    CHECK(onBridge > 0);
    CHECK(maxDrop <= settings.maxGrade * traffic.wheelbase + 0.02f);  // не круче допустимого уклона на колёсной базе
}
