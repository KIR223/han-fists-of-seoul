// Этап 06: профиль жизни города, политика появления и удаления машин, толпа без массовых исчезновений.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "han/ai/archetype.h"
#include "han/ai/city_crowd.h"
#include "han/ai/life_profile.h"
#include "han/ai/traffic.h"
#include "han/world/walk_layout.h"

using namespace han;

namespace {
std::string repoFile(const char* file) { return (std::filesystem::path(HAN_REPO_DIR) / file).string(); }

bool visibleFrom(ai::Vec2 focus, ai::Vec2 view, ai::Vec2 p, float halfAngleDegrees) {
    const float dx = p.x - focus.x, dz = p.z - focus.z, d = std::hypot(dx, dz);
    if (d < 25.f) return true;
    return (dx * view.x + dz * view.z) / d > std::cos(halfAngleDegrees * 0.01745329f);
}
// Видимость для толпы и аудита: до 300 м, в конусе по ходу камеры (дальше — за дымкой и пределом отрисовки).
bool visibleWithin300(ai::Vec2 focus, ai::Vec2 view, ai::Vec2 p, float halfAngleDegrees) {
    return std::hypot(p.x - focus.x, p.z - focus.z) <= 300.f && visibleFrom(focus, view, p, halfAngleDegrees);
}
}  // namespace

TEST_CASE("life_profile: профиль из data/city читается, кривая по часам интерполируется, плохие профили отклоняются без порчи значений") {
    ai::LifeProfile profile;
    std::string error;
    REQUIRE_MESSAGE(ai::loadLifeProfile(repoFile("data/city/life_profile.json"), profile, error), error);
    CHECK(profile.traffic.maxCars > 0);
    CHECK(profile.traffic.spawnRadius <= profile.traffic.despawnRadius);
    CHECK(profile.traffic.spawnMinDistanceInView <= profile.traffic.spawnRadius);
    CHECK(profile.traffic.weightByLength);
    CHECK(profile.crowd.agentCount > 0);
    CHECK(profile.trafficDensityAt(9.f) == doctest::Approx(1.f));
    CHECK(profile.trafficDensityAt(0.f) == doctest::Approx(0.2f));
    CHECK(profile.trafficDensityAt(24.f) == doctest::Approx(0.2f));
    // Между 5 ч (0,25) и 7 ч (0,9): в 6 ч — середина.
    CHECK(profile.trafficDensityAt(6.f) == doctest::Approx(0.575f).epsilon(0.001));
    CHECK(profile.trafficDensityAt(30.f) == profile.trafficDensityAt(6.f));  // сутки замыкаются
    // Ночью меньше, чем в час пик.
    CHECK(profile.trafficDensityAt(3.f) < profile.trafficDensityAt(8.f));

    const auto path = std::filesystem::temp_directory_path() / "han_life_bad.json";
    const auto write = [&](const std::string& text) { std::FILE* f = std::fopen(path.string().c_str(), "wb"); std::fputs(text.c_str(), f); std::fclose(f); };
    const auto make = [](const std::string& traffic) {
        return "{\"version\":1,\"traffic\":{" + traffic + "},\"crowd\":{\"agent_count\":10,\"nav_radius\":300,\"recenter_fraction\":0.5,\"spawn_radius\":100,\"min_recycle_distance\":100,"
               "\"far_visible\":400,\"view_half_angle_degrees\":60,\"treadmill_interval\":1,\"treadmill_batch\":2,\"treadmill_min_distance\":0.5}}";
    };
    const std::string ok = "\"max_cars\":50,\"spawn_radius\":300,\"despawn_radius\":400,\"despawn_in_view_radius\":500,\"spawn_min_distance_in_view\":200,\"spawn_min_distance_behind\":30,"
                           "\"view_half_angle_degrees\":60,\"spawn_gap\":20,\"class_weight\":{\"motorway\":1,\"primary\":1,\"secondary\":1,\"local\":1},\"hour_density\":[[0,0.5],[12,1],[24,0.5]]";
    ai::LifeProfile kept;
    kept.traffic.maxCars = 77;
    write(make(ok));
    CHECK(ai::loadLifeProfile(path.string(), kept, error));
    CHECK(kept.traffic.maxCars == 50);
    ai::LifeProfile untouched;
    untouched.traffic.maxCars = 77;
    write(make("\"max_cars\":50,\"spawn_radius\":500,\"despawn_radius\":400,\"despawn_in_view_radius\":500,\"spawn_min_distance_in_view\":200,\"spawn_min_distance_behind\":30,\"view_half_angle_degrees\":60,"
               "\"spawn_gap\":20,\"class_weight\":{\"motorway\":1,\"primary\":1,\"secondary\":1,\"local\":1},\"hour_density\":[[0,0.5],[12,1]]"));
    CHECK_FALSE(ai::loadLifeProfile(path.string(), untouched, error));  // удаление ближе появления
    CHECK(untouched.traffic.maxCars == 77);
    std::string bad = ok;
    bad.replace(bad.find("[[0,0.5],[12,1],[24,0.5]]"), 25, "[[12,0.5],[6,1]]");
    write(make(bad));
    CHECK_FALSE(ai::loadLifeProfile(path.string(), untouched, error));  // часы не по возрастанию
    std::filesystem::remove(path);
}

TEST_CASE("life: политика появления — в конусе зрения машины не появляются ближе порога, не вплотную к камере; видимые не исчезают рядом") {
    ai::RoadGraph::Line line;
    for (float x = -2000.f; x <= 2000.f; x += 100.f) line.pts.push_back({x, 0.f});
    line.lanesPerDirection = 2;
    const auto graph = ai::RoadGraph::fromLines({line});
    ai::TrafficSettings settings;
    settings.maxCars = 80;
    settings.spawnRadius = 600.f;
    settings.despawnRadius = 800.f;
    settings.despawnInViewRadius = 1000.f;
    settings.spawnMinDistanceInView = 400.f;
    settings.spawnMinDistanceBehind = 40.f;
    settings.viewHalfAngleDegrees = 62.f;
    settings.weightByLength = true;
    ai::TrafficSim sim(graph, settings);
    const ai::Vec2 focus{0.f, 0.f}, view{1.f, 0.f};
    sim.setFocus(focus);
    sim.setView(view);
    std::unordered_set<std::uint32_t> known;
    long spawns = 0, violations = 0, nearViolations = 0, visibleRemovedNear = 0;
    std::unordered_map<std::uint32_t, ai::Vec2> previous;
    for (int step = 0; step < 6000; ++step) {
        sim.update(0.1f);
        std::unordered_map<std::uint32_t, ai::Vec2> now;
        for (const auto& car : sim.cars()) {
            now[car.id] = car.pos;
            if (known.insert(car.id).second) {
                ++spawns;
                const float d = std::hypot(car.pos.x - focus.x, car.pos.z - focus.z);
                // Машина видна впервые после шага симуляции, за который успела проехать до ~1 м: запас 3 м.
                if (d < settings.spawnMinDistanceBehind - 3.f) ++nearViolations;
                if (visibleFrom(focus, view, car.pos, settings.viewHalfAngleDegrees) && d < settings.spawnMinDistanceInView - 3.f) ++violations;
            }
        }
        for (const auto& [id, pos] : previous)
            if (now.count(id) == 0 && visibleFrom(focus, view, pos, settings.viewHalfAngleDegrees) && std::hypot(pos.x - focus.x, pos.z - focus.z) < settings.despawnRadius) ++visibleRemovedNear;
        previous.swap(now);
    }
    CHECK(spawns > 100);
    CHECK(violations == 0);
    CHECK(nearViolations == 0);
    CHECK(visibleRemovedNear == 0);  // видимые машины ближе радиуса удаления не исчезают (дорога без тупиков в радиусе)
}

TEST_CASE("life: кривая плотности по часу — лишние машины уходят вне взгляда, число подстраивается под долю maxCars") {
    ai::RoadGraph::Line line;
    for (float x = -1500.f; x <= 1500.f; x += 100.f) line.pts.push_back({x, 0.f});
    line.lanesPerDirection = 2;
    const auto graph = ai::RoadGraph::fromLines({line});
    ai::TrafficSettings settings;
    settings.maxCars = 100;
    settings.spawnRadius = 500.f;
    settings.despawnRadius = 700.f;
    settings.viewHalfAngleDegrees = 62.f;
    ai::TrafficSim sim(graph, settings);
    sim.setFocus({0.f, 0.f});
    sim.setView({1.f, 0.f});
    for (int i = 0; i < 1800; ++i) sim.update(0.1f);
    const int full = static_cast<int>(sim.cars().size());
    CHECK(full >= 80);
    sim.setDensityScale(0.4f);
    for (int i = 0; i < 6000; ++i) sim.update(0.1f);
    const int reduced = static_cast<int>(sim.cars().size());
    // Видимые машины не убираются принудительно: число приближается к доле и не растёт.
    CHECK(reduced < full);
    CHECK(reduced <= 60);
    sim.setDensityScale(1.f);
    for (int i = 0; i < 3000; ++i) sim.update(0.1f);
    CHECK(static_cast<int>(sim.cars().size()) >= 80);
}

TEST_CASE("life: толпа следует за камерой без массовых исчезновений — прохожие и их число сохраняются, видимых телепортов нет; прежний режим даёт") {
    ai::ArchetypeLibrary library;
    std::string error;
    REQUIRE_MESSAGE(library.loadDirectory(repoFile("data/people/archetypes"), error), error);
    REQUIRE(library.size() > 0);
    // Улица зданий вдоль x: ряды домов по обе стороны, проход посередине.
    std::vector<std::vector<world::Vec2>> buildings;
    for (float x = -300.f; x <= 5500.f; x += 14.f)
        for (const float z : {-30.f, 30.f}) buildings.push_back({{x, z - 5.f}, {x + 10.f, z - 5.f}, {x + 10.f, z + 5.f}, {x, z + 5.f}});
    const world::WalkLayout layout;  // без проезжей части и воды
    struct Result { long teleports = 0, visibleTeleports = 0; std::size_t agents = 0; int rebuilds = 0; };
    const auto run = [&](bool keepAgents) {
        ai::CityCrowdSettings s;
        s.navRadius = 520.f;  // кольцо дальше farVisible (450 м) впереди по ходу — место, куда переселяются прохожие вне взгляда
        s.spawnRadius = 150.f;
        s.agentCount = 100;
        s.keepAgents = keepAgents;
        s.minRecycleDistance = 120.f;
        s.farVisible = 450.f;
        s.treadmillMinDistance = 0.5f;
        ai::CityCrowd city(buildings, layout, library.pointers(), s);
        ai::Vec2 focus{0.f, 0.f};
        const ai::Vec2 view{1.f, 0.f};
        city.update(0.f, focus, {0.f, 0.f}, 12.f);
        REQUIRE(city.crowd());
        Result r;
        r.agents = city.crowd()->agents().size();
        std::vector<ai::Vec2> last;
        for (const auto& a : city.crowd()->agents()) last.push_back(a.pos);
        for (int step = 0; step < 1500; ++step) {  // 150 с полёта на 30 м/с = 4,5 км
            focus.x += 3.f;
            city.update(0.1f, focus, view, 12.f);
            const auto& agents = city.crowd()->agents();
            REQUIRE(agents.size() == r.agents);  // число прохожих не меняется
            for (std::size_t i = 0; i < agents.size(); ++i) {
                const float jump = std::hypot(agents[i].pos.x - last[i].x, agents[i].pos.z - last[i].z);
                if (jump > 25.f) {
                    ++r.teleports;
                    if (visibleWithin300(focus, view, agents[i].pos, 62.f) || visibleWithin300(focus, view, last[i], 62.f)) ++r.visibleTeleports;
                }
                last[i] = agents[i].pos;
            }
        }
        r.rebuilds = city.stats().rebuilds;
        return r;
    };
    const Result kept = run(true), legacy = run(false);
    CHECK(kept.rebuilds >= 10);
    CHECK(legacy.rebuilds == kept.rebuilds);
    CHECK(legacy.visibleTeleports > 0);                              // прежний режим пересоздаёт толпу у камеры — на глазах
    CHECK(kept.visibleTeleports * 10 <= legacy.visibleTeleports);    // новый — на порядок меньше видимых телепортов
    CHECK(kept.agents == legacy.agents);
}
