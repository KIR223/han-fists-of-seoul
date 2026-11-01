// Тесты трафика (G7): дорожный граф, правостороннее движение, следование за лидером, перекрёстки, 30 минут без зависаний.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <set>

#include "han/ai/traffic.h"
#include "han/world/osm.h"

using namespace han::ai;

namespace {
// Сетка n x n улиц с шагом step м: отрезок на каждый квартал, двусторонние дороги.
RoadGraph gridGraph(int n, float step, bool oneway = false) {
    std::vector<RoadGraph::Line> lines;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j + 1 < n; ++j) {
            RoadGraph::Line h, v;
            h.pts = {{static_cast<float>(j) * step, static_cast<float>(i) * step}, {static_cast<float>(j + 1) * step, static_cast<float>(i) * step}};
            v.pts = {{static_cast<float>(i) * step, static_cast<float>(j) * step}, {static_cast<float>(i) * step, static_cast<float>(j + 1) * step}};
            h.cls = v.cls = RoadClass::Secondary;
            h.oneway = v.oneway = oneway;
            lines.push_back(h);
            lines.push_back(v);
        }
    return RoadGraph::fromLines(lines);
}
}  // namespace

TEST_CASE("traffic: крест из четырёх кварталов — центральный узел перекрёсток, концы нет") {
    const RoadGraph g = RoadGraph::fromLines({{{{-50, 0}, {0, 0}}, RoadClass::Secondary, false, 1}, {{{0, 0}, {50, 0}}, RoadClass::Secondary, false, 1},
                                              {{{0, -50}, {0, 0}}, RoadClass::Local, false, 1}, {{{0, 0}, {0, 50}}, RoadClass::Local, false, 1}});
    CHECK(g.nodes().size() == 5);
    CHECK(g.edges().size() == 8);
    int junctions = 0;
    for (const RoadNode& n : g.nodes()) junctions += n.junction ? 1 : 0;
    CHECK(junctions == 1);
}

TEST_CASE("traffic: сетка — каждая дорога имеет встречную пару, длины сходятся, перекрёстки размечены") {
    const RoadGraph g = gridGraph(4, 80.f);
    CHECK(g.nodes().size() == 16);
    CHECK(g.edges().size() == 48);  // 24 квартала в обе стороны
    for (const RoadEdge& e : g.edges()) {
        CHECK(e.length == doctest::Approx(80.f));
        REQUIRE(e.reverse >= 0);
        CHECK(g.edges()[static_cast<std::size_t>(e.reverse)].from == e.to);
    }
    int junctions = 0;
    for (const RoadNode& n : g.nodes()) junctions += n.junction ? 1 : 0;
    CHECK(junctions == 12);  // четыре угла сетки — не перекрёстки (два соседа)
    CHECK(g.totalLength() == doctest::Approx(48 * 80.f));
}

TEST_CASE("traffic: точка на дороге со смещением вправо — правостороннее движение") {
    const RoadGraph g = RoadGraph::fromLines({{{{0, 0}, {100, 0}}, RoadClass::Local, true, 1}});  // на восток (+X)
    REQUIRE(g.edges().size() == 1);
    const RoadEdge& e = g.edges()[0];
    const Vec2 p = e.pointAt(50.f, 1.6f);
    CHECK(p.x == doctest::Approx(50.f));
    CHECK(p.z == doctest::Approx(1.6f));  // справа от едущего на восток — на юг (+Z)
    const Vec2 d = e.directionAt(10.f);
    CHECK(d.x == doctest::Approx(1.f));
}

TEST_CASE("traffic: одиночная машина разгоняется до ограничения скорости и не превышает его заметно") {
    const RoadGraph g = RoadGraph::fromLines({{{{0, 0}, {2000, 0}}, RoadClass::Secondary, true, 1}});
    TrafficSettings s;
    s.maxCars = 1;
    TrafficSim sim(g, s);
    for (int i = 0; i < 400; ++i) sim.update(0.1f);
    REQUIRE(sim.cars().size() == 1);
    const Car& c = sim.cars()[0];
    CHECK(c.v > 0.7f * c.v0);
    CHECK(c.v <= c.v0 * 1.05f);
    CHECK(c.v0 <= 50.f / 3.6f * 1.2f);  // городское ограничение 50 км/ч
}

TEST_CASE("traffic: быстрая машина догоняет медленную и держит дистанцию, столкновений нет") {
    const RoadGraph g = RoadGraph::fromLines({{{{0, 0}, {3000, 0}}, RoadClass::Secondary, true, 1}});
    TrafficSettings s;
    s.maxCars = 12;
    s.seed = 5;
    TrafficSim sim(g, s);
    int worstOverlap = 0;
    for (int i = 0; i < 3000; ++i) {
        sim.update(0.1f);
        worstOverlap = std::max(worstOverlap, sim.overlaps());
    }
    CHECK(worstOverlap == 0);
    // Дистанция между соседями в одной полосе не меньше минимального зазора (с запасом на торможение)
    std::vector<const Car*> cars;
    for (const Car& c : sim.cars()) cars.push_back(&c);
    std::sort(cars.begin(), cars.end(), [](const Car* a, const Car* b) { return a->s > b->s; });
    for (std::size_t i = 0; i + 1 < cars.size(); ++i) CHECK(cars[i]->s - cars[i]->length - cars[i + 1]->s > 0.5f);
}

TEST_CASE("traffic: сетка улиц — 30 минут без столкновений и массовых зависаний, машины реально едут") {
    const RoadGraph g = gridGraph(5, 90.f);
    TrafficSettings s;
    s.maxCars = 60;
    s.seed = 11;
    TrafficSim sim(g, s);
    int worstOverlap = 0;
    double speedSum = 0;
    int samples = 0;
    for (int step = 0; step < 30 * 60 * 10; ++step) {  // 30 минут по 0,1 с
        sim.update(0.1f);
        if (step % 50 == 0) {
            worstOverlap = std::max(worstOverlap, sim.overlaps());
            speedSum += sim.stats().meanSpeed;
            ++samples;
        }
    }
    CHECK(worstOverlap == 0);
    CHECK(sim.cars().size() >= 50);                         // трафик держится на заданной плотности
    CHECK(speedSum / samples > 2.0);                        // средняя скорость осмысленная, не пробка
    CHECK(sim.stats().stuckResets <= 1);                    // «застреваний» нет
    CHECK(sim.stats().spawned >= static_cast<std::uint64_t>(sim.cars().size()));
}

TEST_CASE("traffic: перекрёсток пропускает по одной машине — на перекрёстке не бывает двух машин из разных направлений одновременно") {
    const RoadGraph g = gridGraph(3, 100.f);
    TrafficSettings s;
    s.maxCars = 40;
    s.seed = 3;
    TrafficSim sim(g, s);
    const Vec2 center{100.f, 100.f};  // центральный узел сетки 3x3
    int simultaneous = 0;
    for (int step = 0; step < 6000; ++step) {
        sim.update(0.1f);
        int inside = 0;
        std::set<int> axes;
        for (const Car& c : sim.cars()) {
            const float dx = c.pos.x - center.x, dz = c.pos.z - center.z;
            if (dx * dx + dz * dz < 5.f * 5.f) {
                ++inside;
                axes.insert(std::fabs(std::sin(c.heading)) > 0.5f ? 0 : 1);
            }
        }
        if (inside >= 2 && axes.size() == 2) ++simultaneous;
    }
    CHECK(simultaneous == 0);
}

TEST_CASE("traffic: фокус игрока — машины появляются рядом и убираются вдали") {
    const RoadGraph g = gridGraph(6, 100.f);
    TrafficSettings s;
    s.maxCars = 30;
    s.spawnRadius = 160.f;
    s.despawnRadius = 260.f;
    TrafficSim sim(g, s);
    sim.setFocus({50.f, 50.f});
    for (int i = 0; i < 300; ++i) sim.update(0.1f);
    REQUIRE_FALSE(sim.cars().empty());
    for (const Car& c : sim.cars()) CHECK(std::hypot(c.pos.x - 50.f, c.pos.z - 50.f) < 300.f);
    sim.setFocus({450.f, 450.f});  // игрок уехал
    for (int i = 0; i < 1500; ++i) sim.update(0.1f);
    int near = 0;
    for (const Car& c : sim.cars()) near += std::hypot(c.pos.x - 450.f, c.pos.z - 450.f) < 300.f ? 1 : 0;
    CHECK(near >= static_cast<int>(sim.cars().size()) - 1);
    CHECK(sim.nearestCar({450.f, 450.f}, 1e6f) != nullptr);
    CHECK(sim.nearestCar({-9999.f, -9999.f}, 10.f) == nullptr);
}

TEST_CASE("traffic: реальные дороги Мапо — граф строится, по нему идёт трафик 30 минут") {
    const auto file = std::filesystem::path(HAN_REPO_DIR) / "assets_src/osm/mapo.osm";
    if (!std::filesystem::exists(file)) {
        MESSAGE("нет assets_src/osm/mapo.osm (han_osm_fetch.py) — тест пропущен");
        return;
    }
    han::world::OsmData d;
    std::string error;
    REQUIRE_MESSAGE(han::world::loadOsmFile(file.string(), d, error), error);
    const han::world::GeoProjection proj(37.5572, 126.9230);
    const RoadGraph g = RoadGraph::build(d, proj);
    CHECK(g.edges().size() > 200);
    CHECK(g.totalLength() > 10000.f);
    int twoWay = 0;
    for (const RoadEdge& e : g.edges()) twoWay += e.reverse >= 0 ? 1 : 0;
    CHECK(twoWay > 100);
    TrafficSettings s;
    s.maxCars = 120;
    s.seed = 7;
    TrafficSim sim(g, s);
    int worstOverlap = 0;
    for (int step = 0; step < 30 * 60 * 10; ++step) {
        sim.update(0.1f);
        if (step % 100 == 0) worstOverlap = std::max(worstOverlap, sim.overlaps());
    }
    CHECK(worstOverlap == 0);
    CHECK(sim.cars().size() > 60);
    CHECK(sim.stats().meanSpeed > 1.5f);
    // Трафик живёт полчаса без застреваний: сторожевой таймер почти не срабатывает.
    CHECK(sim.stats().stuckResets <= 2);
    for (const std::string& n : sim.stats().stuckNotes) MESSAGE(n);
    {
        std::map<int, int> byEdge;
        for (int e : sim.stats().stuckEdges) ++byEdge[e];
        for (const auto& [e, n] : byEdge) {
            const RoadEdge& re = g.edges()[static_cast<std::size_t>(e)];
            const RoadNode& to = g.nodes()[static_cast<std::size_t>(re.to)];
            if (n >= 3) MESSAGE("застревают на ребре " << e << " x" << n << ": длина " << re.length << ", в узел " << re.to << " (перекрёсток=" << to.junction << ", выходов " << to.out.size() << ", входов " << to.in.size() << ")");
        }
    }
    MESSAGE("Мапо: рёбер " << g.edges().size() << ", длина " << g.totalLength() << " м, появилось " << sim.stats().spawned << ", сбросов " << sim.stats().stuckResets
                          << ", уехало за край " << sim.stats().reachedEnd << ", средняя скорость " << sim.stats().meanSpeed);
}
