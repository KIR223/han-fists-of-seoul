// Этап 04: полосы и повороты машин, проходимые поверхности пешеходов. Детерминированные тесты на синтетической карте.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "han/ai/archetype.h"
#include "han/ai/crowd.h"
#include "han/ai/navgrid.h"
#include "han/ai/traffic.h"
#include "han/world/city_style.h"
#include "han/world/osm.h"
#include "han/world/walk_layout.h"

using namespace han;

namespace {
const world::GeoProjection kProjection(37.557, 126.914);

struct P { long id; float x, z; };
struct W { long id; std::vector<long> nodes; std::string tags; };

world::OsmData parseMap(const std::vector<P>& points, const std::vector<W>& ways) {
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
    world::OsmData data;
    std::string error;
    REQUIRE_MESSAGE(world::parseOsmXml(out.str(), data, error), error);
    return data;
}

world::CityStyle repoStyle() {
    world::CityStyle style;
    std::string error;
    REQUIRE_MESSAGE(world::loadCityStyle((std::filesystem::path(HAN_REPO_DIR) / "data/city/streetscape.json").string(), style, error), error);
    return style;
}

bool inside(const std::vector<world::Vec2>& poly, float x, float z) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > z) != (poly[j].y > z) && x < (poly[j].x - poly[i].x) * (z - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    return in;
}
bool insideAny(const std::vector<std::vector<world::Vec2>>& polygons, float x, float z) {
    return std::any_of(polygons.begin(), polygons.end(), [&](const auto& p) { return inside(p, x, z); });
}

struct Run {
    long steps = 0, jumps = 0, sharp = 0, wrongSide = 0;
    float maxJump = 0.f;
};

// Прогоняет трафик и считает рывки позиции больше возможного и повороты круче 25° за шаг. Развороты в тупике (больше 150° за шаг) — прежний
// переезд на встречную полосу — считаются отдельно и в рывки не входят.
Run simulate(const ai::RoadGraph& graph, bool smooth, int seconds, std::uint32_t seed = 1) {
    ai::TrafficSettings settings;
    settings.maxCars = 10;
    settings.smoothTurns = smooth;
    settings.seed = seed;
    ai::TrafficSim sim(graph, settings);
    Run run;
    std::unordered_map<std::uint32_t, ai::Car> last;
    for (int i = 0; i < seconds * 10; ++i) {
        sim.update(0.1f);
        for (const auto& car : sim.cars()) {
            if (car.edge < 0) continue;
            ++run.steps;
            const auto it = last.find(car.id);
            if (it != last.end()) {
                const float jump = std::hypot(car.pos.x - it->second.pos.x, car.pos.z - it->second.pos.z);
                float dh = std::fabs(car.heading - it->second.heading);
                if (dh > 3.14159265f) dh = 6.2831853f - dh;
                const float degrees = dh * 57.29578f;
                if (degrees <= 150.f) {
                    run.maxJump = std::max(run.maxJump, jump);
                    if (jump > std::max(car.v, it->second.v) * 0.1f * 1.5f + 0.3f) ++run.jumps;
                    if (degrees > 25.f && jump > 0.05f) ++run.sharp;
                }
            }
            last[car.id] = car;  // обновляется на каждом шаге, в том числе при развороте
        }
    }
    return run;
}
}  // namespace

TEST_CASE("lanes: правостороннее движение — на двусторонней дороге едущие на восток держатся южнее оси, на запад — севернее") {
    ai::RoadGraph::Line line;
    line.pts = {{-300.f, 0.f}, {300.f, 0.f}};
    line.lanesPerDirection = 2;
    const auto graph = ai::RoadGraph::fromLines({line});
    ai::TrafficSettings settings;
    settings.maxCars = 10;
    ai::TrafficSim sim(graph, settings);
    int east = 0, west = 0;
    for (int i = 0; i < 600; ++i) {
        sim.update(0.1f);
        for (const auto& car : sim.cars()) {
            const float heading = car.heading;
            if (std::sin(heading) > 0.9f) {  // на восток (+x): справа — юг (+z)
                ++east;
                CHECK(car.pos.z > 0.f);
            } else if (std::sin(heading) < -0.9f) {
                ++west;
                CHECK(car.pos.z < 0.f);
            }
            CHECK(std::fabs(car.pos.z) < 2.f * 3.2f + 0.01f);  // в пределах двух полос своего направления
        }
    }
    CHECK(east > 0);
    CHECK(west > 0);
}

TEST_CASE("lanes: односторонняя дорога — все машины едут только в разрешённую сторону") {
    ai::RoadGraph::Line line;
    line.pts = {{0.f, 0.f}, {500.f, 0.f}};
    line.oneway = true;
    const auto graph = ai::RoadGraph::fromLines({line});
    ai::TrafficSettings settings;
    settings.maxCars = 6;
    ai::TrafficSim sim(graph, settings);
    long seen = 0;
    for (int i = 0; i < 600; ++i) {
        sim.update(0.1f);
        for (const auto& car : sim.cars()) {
            ++seen;
            CHECK(std::sin(car.heading) > 0.9f);  // на восток
        }
    }
    CHECK(seen > 0);
}

TEST_CASE("lanes: поворот на перекрёстке без рывков позиции; прежний режим рывки даёт") {
    // Г-образный перекрёсток: восток → север, две полосы в каждую сторону (смещение полосы заметно).
    ai::RoadGraph::Line a, b;
    a.pts = {{-300.f, 0.f}, {0.f, 0.f}};
    b.pts = {{0.f, 0.f}, {0.f, -300.f}};
    a.lanesPerDirection = b.lanesPerDirection = 2;
    const auto graph = ai::RoadGraph::fromLines({a, b});
    const Run smooth = simulate(graph, true, 600);
    const Run legacy = simulate(graph, false, 600);
    REQUIRE(smooth.steps > 100);
    CHECK(smooth.jumps == 0);
    CHECK(smooth.maxJump < 2.5f);  // не больше пути за шаг при скорости до ~22 м/с
    CHECK(legacy.jumps > 0);      // прежний режим перескакивал на смещение полосы
    CHECK(legacy.maxJump > smooth.maxJump);
}

TEST_CASE("lanes: разворот в тупике остаётся прежним переездом и не рисует дугу через разделитель") {
    ai::RoadGraph::Line line;
    line.pts = {{-100.f, 0.f}, {100.f, 0.f}};
    line.lanesPerDirection = 3;
    const auto graph = ai::RoadGraph::fromLines({line});
    ai::TrafficSettings settings;
    settings.maxCars = 6;
    ai::TrafficSim sim(graph, settings);
    for (int i = 0; i < 1200; ++i) {
        sim.update(0.1f);
        for (const auto& car : sim.cars()) CHECK(std::fabs(car.pos.z) >= 3.2f * 0.5f - 0.01f);  // никогда на самой оси
    }
}

TEST_CASE("walk_layout: проезжая часть широкой улицы закрыта, переход открыт, здания и вода — стены, мост не закрывает землю") {
    // Улица шириной 20 м вдоль x с T-образным примыканием в x = 0; рядом здание и пруд.
    const auto data = parseMap({{1, -200, 0}, {2, 0, 0}, {3, 200, 0}, {4, 0, -150}, {5, -120, 30}, {6, -100, 30}, {7, -100, 50}, {8, -120, 50},
                                {20, -60, 60}, {21, -40, 60}, {22, -40, 80}, {23, -60, 80}, {30, 100, -40}, {31, 100, 40}},
                               {{100, {1, 2}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"width\" v=\"20\"/>"},
                                {101, {2, 3}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"width\" v=\"20\"/>"},
                                {102, {4, 2}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"width\" v=\"20\"/>"},
                                {103, {5, 6, 7, 8, 5}, "<tag k=\"building\" v=\"yes\"/>"},
                                {104, {20, 21, 22, 23, 20}, "<tag k=\"natural\" v=\"water\"/>"},
                                {105, {30, 31}, "<tag k=\"highway\" v=\"primary\"/><tag k=\"bridge\" v=\"yes\"/><tag k=\"width\" v=\"12\"/>"}});
    const auto style = repoStyle();
    const auto layout = world::buildWalkLayout(data, kProjection, style);
    CHECK(layout.buildings.size() == 1);
    CHECK(layout.water.size() == 1);
    CHECK(layout.carriageways.size() > 3);
    CHECK(layout.crossings.size() >= 2);
    CHECK(insideAny(layout.carriageways, -100.f, 0.f));        // середина проезжей части закрыта
    CHECK_FALSE(insideAny(layout.carriageways, 100.f, 30.f));  // под мостом проезжей части нет

    // Сетка пешеходов: переходить улицу можно только по переходам.
    std::vector<std::vector<ai::Vec2>> hard, roads, openings;
    const auto convert = [](const std::vector<std::vector<world::Vec2>>& in, std::vector<std::vector<ai::Vec2>>& out) {
        for (const auto& poly : in) {
            std::vector<ai::Vec2> c;
            for (const auto& p : poly) c.push_back({p.x, p.y});
            out.push_back(std::move(c));
        }
    };
    convert(layout.buildings, hard);
    convert(layout.water, hard);
    convert(layout.carriageways, roads);
    convert(layout.crossings, openings);
    const auto grid = ai::NavGrid::build(-250.f, -200.f, 250.f, 150.f, hard, roads, openings, {});
    REQUIRE(grid.valid());
    CHECK_FALSE(grid.walkable(-100.f, 0.f));   // проезжая часть
    CHECK_FALSE(grid.walkable(-110.f, 40.f));  // здание
    CHECK_FALSE(grid.walkable(-50.f, 70.f));   // вода
    CHECK(grid.walkable(-100.f, 16.f));        // тротуар у проезжей части (10 м + бордюр ... уже за кромкой)
    std::vector<ai::Vec2> path;
    const ai::Vec2 north{-100.f, -16.f}, south{-100.f, 16.f};
    REQUIRE(grid.findPath(north, south, path));
    float length = 0.f;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) length += std::hypot(path[i + 1].x - path[i].x, path[i + 1].z - path[i].z);
    CHECK(length > 40.f);  // прямая — 32 м; путь обязан обойти проезжую часть и идти к переходу
    for (const auto& p : path)
        CHECK((!insideAny(layout.carriageways, p.x, p.z) || insideAny(layout.crossings, p.x, p.z)));  // на проезжей части — только на переходах
    CHECK_FALSE(grid.lineOfSight(north, south));
}

TEST_CASE("walk_layout: проёмы не пробивают здания, переулки уже 8 м остаются проходимыми") {
    const auto data = parseMap({{1, -100, 0}, {2, 100, 0}, {3, 0, -60}, {4, 0, 60}},
                               {{100, {1, 2}, "<tag k=\"highway\" v=\"residential\"/>"}, {101, {3, 4}, "<tag k=\"highway\" v=\"service\"/>"}});
    const auto layout = world::buildWalkLayout(data, kProjection, repoStyle());
    CHECK(layout.carriageways.empty());  // жилая улица 6 м и сервисный проезд — по мостовой ходят
    // NavGrid: проём поверх здания не открывает здание.
    const std::vector<std::vector<ai::Vec2>> building = {{{10.f, 10.f}, {30.f, 10.f}, {30.f, 30.f}, {10.f, 30.f}}};
    const std::vector<std::vector<ai::Vec2>> road = {{{-50.f, -10.f}, {50.f, -10.f}, {50.f, 20.f}, {-50.f, 20.f}}};
    const std::vector<std::vector<ai::Vec2>> opening = {{{0.f, -15.f}, {40.f, -15.f}, {40.f, 25.f}, {0.f, 25.f}}};
    const auto grid = ai::NavGrid::build(-60.f, -40.f, 60.f, 60.f, building, road, opening, {});
    CHECK(grid.walkable(5.f, 5.f));          // проём открыл проезжую часть
    CHECK_FALSE(grid.walkable(-20.f, 5.f));  // вне проёма проезжая часть закрыта
    CHECK_FALSE(grid.walkable(20.f, 20.f));  // здание осталось стеной, хотя проём его перекрывает
}

TEST_CASE("crowd: скученные у одной двери прохожие не застревают в равновесии расталкивания") {
    ai::ArchetypeLibrary library;
    std::string error;
    REQUIRE_MESSAGE(library.loadDirectory((std::filesystem::path(HAN_REPO_DIR) / "data/people/archetypes").string(), error), error);
    REQUIRE(library.size() > 0);
    const auto grid = ai::NavGrid::build(0.f, 0.f, 120.f, 120.f, {}, ai::NavGrid::Settings{});
    // Одна дверь на все занятия: все цели расписания совпадают, у двери собирается толпа.
    std::vector<ai::Poi> pois;
    for (const char* category : {"work", "cafe", "shop", "eat", "park", "campus", "market", "nightlife", "sights", "delivery", "security"})
        pois.push_back({{60.f, 60.f}, category});
    ai::CrowdSettings settings;
    settings.maxAgents = 120;
    ai::Crowd crowd(grid, pois, settings);
    const auto archetypes = library.pointers();
    for (int i = 0; i < 120; ++i)
        crowd.spawn(*archetypes[static_cast<std::size_t>(i) % archetypes.size()], {20.f + static_cast<float>(i % 12) * 1.3f, 20.f + static_cast<float>(i / 12) * 1.3f});
    crowd.setHour(12.f);
    std::vector<ai::Vec2> last(crowd.agents().size());
    std::vector<float> still(crowd.agents().size(), 0.f);
    for (std::size_t i = 0; i < last.size(); ++i) last[i] = crowd.agents()[i].pos;
    float worst = 0.f;
    for (int step = 0; step < 6000; ++step) {  // 10 минут при шаге 0,1 с
        crowd.update(0.1f, {60.f, 60.f});
        for (std::size_t i = 0; i < crowd.agents().size(); ++i) {
            const auto& a = crowd.agents()[i];
            const bool walking = a.state == ai::AgentState::Walking;
            const float moved = std::hypot(a.pos.x - last[i].x, a.pos.z - last[i].z);
            still[i] = (walking && moved < 0.005f) ? still[i] + 0.1f : 0.f;
            worst = std::max(worst, still[i]);
            last[i] = a.pos;
        }
    }
    CHECK(worst < 60.f);  // идущий не стоит на месте дольше минуты
}
