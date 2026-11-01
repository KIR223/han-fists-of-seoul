// Тесты полиции и розыска (G8): засчитывание преступлений, звёзды, камеры и линия взгляда, погоня, зона поиска, побег, задержание.
#include <doctest/doctest.h>

#include <cmath>

#include "han/ai/navgrid.h"
#include "han/ai/traffic.h"
#include "han/police/police.h"

using namespace han::police;
using han::ai::NavGrid;
using han::ai::RoadGraph;

namespace {
// Город 600 x 600 м: открытая площадь, стена-здание посередине (линия взгляда закрыта).
NavGrid openCity() { return NavGrid::build(-300, -300, 300, 300, {}); }
NavGrid cityWithWall() {
    std::vector<std::vector<han::ai::Vec2>> walls = {{{-10, -80}, {10, -80}, {10, 80}, {-10, 80}}};
    return NavGrid::build(-300, -300, 300, 300, walls);
}
RoadGraph crossRoads() {
    std::vector<RoadGraph::Line> lines;
    for (int i = -3; i < 3; ++i) {
        lines.push_back({{{static_cast<float>(i) * 100.f, -300.f}, {static_cast<float>(i + 1) * 100.f, -300.f}}, han::ai::RoadClass::Secondary, false, 1});
        lines.push_back({{{-300.f, static_cast<float>(i) * 100.f}, {-300.f, static_cast<float>(i + 1) * 100.f}}, han::ai::RoadClass::Secondary, false, 1});
        lines.push_back({{{300.f, static_cast<float>(i) * 100.f}, {300.f, static_cast<float>(i + 1) * 100.f}}, han::ai::RoadClass::Secondary, false, 1});
        lines.push_back({{{static_cast<float>(i) * 100.f, 300.f}, {static_cast<float>(i + 1) * 100.f, 300.f}}, han::ai::RoadClass::Secondary, false, 1});
    }
    return RoadGraph::fromLines(lines);
}
bool has(const std::vector<Event>& events, EventType t) {
    for (const Event& e : events)
        if (e.type == t) return true;
    return false;
}
}  // namespace

TEST_CASE("police: драка без свидетелей не засчитывается, со свидетелем — да; нападение на полицейского засчитывается без свидетеля") {
    NavGrid nav = openCity();
    PoliceSystem p(&nav, nullptr);
    CHECK_FALSE(p.addCrime(Crime::Assault, {0, 0}, false));
    CHECK(p.level() == 0);
    CHECK(p.addCrime(Crime::Assault, {0, 0}, true));
    CHECK(p.level() == 1);  // 12 жара — порог первой звезды 10
    CHECK(p.heat() == doctest::Approx(12.f));
    CHECK(p.addCrime(Crime::AssaultOfficer, {5, 5}, false));
    CHECK(p.level() == 2);  // 52
    auto ev = p.takeEvents();
    CHECK(has(ev, EventType::CrimeCounted));
    CHECK(has(ev, EventType::LevelChanged));
    CHECK(p.takeEvents().empty());  // события забираются один раз
}

TEST_CASE("police: звёзды по порогам жара") {
    NavGrid nav = openCity();
    PoliceSystem p(&nav, nullptr);
    struct Row {
        Crime c;
        int times, level;
    } rows[] = {{Crime::Vandalism, 1, 0}, {Crime::Vandalism, 1, 1}, {Crime::Murder, 1, 3}, {Crime::Murder, 1, 5}};  // жар 6, 12, 92, 172
    for (const Row& r : rows) {
        for (int i = 0; i < r.times; ++i) p.addCrime(r.c, {0, 0}, true);
        CHECK(p.level() == r.level);
    }
}

TEST_CASE("police: камера видит только в своём конусе, в пределах дальности и без стены; камера засчитывает преступление без свидетелей") {
    NavGrid nav = cityWithWall();
    PoliceSystem p(&nav, nullptr);
    Camera cam;
    cam.pos = {-60, 0};
    cam.yaw = 1.5707963f;  // смотрит на восток (+X)
    cam.fovDeg = 60.f;
    cam.range = 80.f;
    p.addCamera(cam);
    CHECK(p.addCrime(Crime::Theft, {-20, 0}, false));  // в конусе, между ним и камерой стены нет — видит
    // за стеной (x > 10) камера не видит
    CHECK_FALSE(p.addCrime(Crime::Theft, {30, 0}, false));
    // позади камеры не видит
    CHECK_FALSE(p.addCrime(Crime::Theft, {-100, 0}, false));
}

TEST_CASE("police: камера на открытом месте видит в конусе и засчитывает кражу") {
    NavGrid nav = openCity();
    PoliceSystem p(&nav, nullptr);
    Camera cam;
    cam.pos = {-60, 0};
    cam.yaw = 1.5707963f;
    cam.fovDeg = 60.f;
    cam.range = 80.f;
    p.addCamera(cam);
    CHECK(p.addCrime(Crime::Theft, {-20, 0}, false));   // в конусе, 40 м
    CHECK_FALSE(p.addCrime(Crime::Theft, {-20, 60}, false));  // вне конуса (по диагонали ~56°)
    CHECK_FALSE(p.addCrime(Crime::Theft, {100, 0}, false));   // дальше 80 м
    CHECK_FALSE(p.addCrime(Crime::Theft, {-100, 0}, false));  // позади
}

TEST_CASE("police: герой на месте — патрули прибывают, задерживают за разумное время; события: Spotted, Busted") {
    NavGrid nav = openCity();
    PoliceSystem p(&nav, nullptr);
    p.addStation({130, 130});
    p.addStation({-130, 130});
    p.addCrime(Crime::Assault, {0, 0}, true);
    bool spotted = false;
    float t = 0.f;
    for (; t < 120.f && !p.busted(); t += 0.1f) {
        p.update(0.1f, {0, 0});
        spotted = spotted || has(p.takeEvents(), EventType::Spotted);
    }
    CHECK(p.busted());
    CHECK(t < 90.f);
    CHECK(p.units().size() >= 1);
    CHECK(spotted);
}

TEST_CASE("police: бегущий прочь герой на открытой площади уходит от пеших офицеров, но жар не остывает, пока его видят") {
    NavGrid nav = openCity();
    PoliceSystem p(&nav, nullptr);
    p.addStation({0, 250});
    p.addCrime(Crime::Assault, {0, -250}, true);
    han::ai::Vec2 hero{0, -250};
    float heatAtSeen = 0.f;
    for (int i = 0; i < 600; ++i) {  // 60 с: герой бежит на запад со скоростью 6,5 м/с (быстрее офицеров 5,2)
        hero.x = std::max(-290.f, hero.x - 6.5f * 0.1f);
        p.update(0.1f, hero);
        if (p.playerSeen()) heatAtSeen = p.heat();
    }
    CHECK_FALSE(p.busted());
    if (heatAtSeen > 0.f) CHECK(p.heat() >= heatAtSeen - 0.01f);  // пока видели — жар не падал
}

TEST_CASE("police: оторвался — вне зоны поиска и вне поля зрения жар остывает до нуля, розыск снят, патрули уходят и исчезают") {
    NavGrid nav = cityWithWall();
    PoliceSystem p(&nav, nullptr);
    p.addStation({250, 250});
    p.addCrime(Crime::Assault, {-200, -200}, true);
    // Герой ушёл далеко от места преступления (вне зоны поиска) и стоит спрятанным за стеной.
    const han::ai::Vec2 hero{-280, 200};
    bool escaped = false;
    int lastLevel = p.level();
    float t = 0.f;
    for (; t < 400.f && !escaped; t += 0.1f) {
        p.update(0.1f, hero);
        for (const Event& e : p.takeEvents()) {
            if (e.type == EventType::Escaped) escaped = true;
            if (e.type == EventType::LevelChanged) CHECK(e.level <= lastLevel + 0);  // уровень только падает
            if (e.type == EventType::LevelChanged) lastLevel = e.level;
        }
        if (p.busted()) break;
    }
    CHECK(escaped);
    CHECK(p.level() == 0);
    CHECK(p.heat() == doctest::Approx(0.f));
    // Патрули уходят и исчезают в пределах leaveSeconds.
    for (int i = 0; i < 400; ++i) p.update(0.1f, hero);
    CHECK(p.units().empty());
}

TEST_CASE("police: зона поиска сужается, пока героя не видно, и не меньше минимума; новое преступление возвращает её") {
    NavGrid nav = openCity();
    PoliceSystem p(&nav, nullptr);
    p.addCrime(Crime::Assault, {0, 0}, true);
    p.update(0.1f, {250, 250});
    const float start = p.searchRadius();
    CHECK(start > 60.f);
    for (int i = 0; i < 300; ++i) p.update(0.1f, {250, 250});
    CHECK(p.searchRadius() < start);
    CHECK(p.searchRadius() >= 15.f);
    p.addCrime(Crime::Assault, {250, 250}, true);
    CHECK(p.searchRadius() >= start - 0.1f);
}

TEST_CASE("police: на 3 звёздах приезжают машины по дорогам, высаживают офицеров; отряд растёт с уровнем") {
    NavGrid nav = openCity();
    RoadGraph roads = crossRoads();
    PoliceSystem p(&nav, &roads);
    p.addStation({-280, 280});
    p.addStation({280, 280});
    p.addCrime(Crime::Murder, {0, 0}, true);
    p.addCrime(Crime::Murder, {0, 0}, true);  // 160 → 5 звёзд? 80 → 2, 160 → 5
    CHECK(p.level() == 5);
    int cars = 0, foot = 0;
    for (int i = 0; i < 1200 && !p.busted(); ++i) {
        p.update(0.1f, {0, 0});
        cars = foot = 0;
        for (const Unit& u : p.units()) (u.kind == UnitKind::Car ? cars : foot)++;
    }
    CHECK(p.busted());
    CHECK(foot >= 2);
}

TEST_CASE("police: после «задержан» система стоит; clear сбрасывает всё; одинаковый сид — одинаковый ход") {
    NavGrid nav = openCity();
    const auto run = [&](std::uint32_t seed) {
        PoliceSystem p(&nav, nullptr, {}, seed);
        p.addStation({150, 150});
        p.addStation({-150, 150});
        p.addCrime(Crime::Assault, {0, 0}, true);
        float t = 0.f;
        while (!p.busted() && t < 120.f) {
            p.update(0.1f, {0, 0});
            t += 0.1f;
        }
        return t;
    };
    CHECK(run(5) == doctest::Approx(run(5)));
    PoliceSystem p(&nav, nullptr);
    p.addStation({120, 120});
    p.addCrime(Crime::Assault, {0, 0}, true);
    for (int i = 0; i < 1200 && !p.busted(); ++i) p.update(0.1f, {0, 0});
    REQUIRE(p.busted());
    CHECK_FALSE(p.addCrime(Crime::Assault, {0, 0}, true));  // пока задержан — новые не копятся
    p.clear();
    CHECK(p.level() == 0);
    CHECK_FALSE(p.busted());
    CHECK(p.units().empty());
}

TEST_CASE("police: кратчайший путь по дорожному графу — цепочка дорог от узла к узлу; нет пути — пусто") {
    RoadGraph g = crossRoads();
    const int a = g.nearestNode({-300, -300}), b = g.nearestNode({300, -300});
    const auto path = g.shortestPath(a, b);
    REQUIRE_FALSE(path.empty());
    CHECK(g.edges()[static_cast<std::size_t>(path.front())].from == a);
    CHECK(g.edges()[static_cast<std::size_t>(path.back())].to == b);
    float len = 0.f;
    for (int e : path) len += g.edges()[static_cast<std::size_t>(e)].length;
    CHECK(len == doctest::Approx(600.f));
    CHECK(g.shortestPath(a, a).empty());
    CHECK(g.shortestPath(a, -1).empty());
}


#include "han/ai/world_services.h"

TEST_CASE("services: контракт IWorldServices — время и сезоны, район, святые места, спавн, путь, розыск") {
    using namespace han;
    NavGrid nav = cityWithWall();
    PoliceSystem police(&nav, nullptr);
    PoliceBackend backend(police);
    ai::WorldServices services;
    services.setNavGrid(&nav);
    services.setWantedBackend(&backend);
    contracts::IWorldServices& w = services;  // как видит игровой слой

    // Время: 12:00 первого дня; ход времени и смена суток, сезоны по календарю
    services.setTime(23.5f, 100);
    CHECK(w.time().season == contracts::Season::Spring);
    services.advance(60.f, 60.f);  // минута реального времени = час игры
    CHECK(w.time().hour == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(w.time().dayOfYear == 101);
    services.setTime(12.f, 200);
    CHECK(w.time().season == contracts::Season::Summer);
    services.setTime(12.f, 350);
    CHECK(w.time().season == contracts::Season::Winter);
    services.setWeather(contracts::Weather::Rain);
    CHECK(w.weather() == contracts::Weather::Rain);

    // Районы и святыни
    services.addDistrict({7, {{-100, -100}, {0, -100}, {0, 100}, {-100, 100}}});
    CHECK(w.districtAt({-50, 0, 0}) == 7);
    CHECK(w.districtAt({50, 0, 0}) == 0);
    services.addSacred({{200, 200}, 30.f});
    CHECK(w.isSacred({210, 0, 210}));
    CHECK_FALSE(w.isSacred({260, 0, 260}));

    // Спавн: сущности получают номера, заявки забирает игровой слой
    const auto npc = w.spawnNpc(3, {1, 0, 2});
    const auto car = w.spawnVehicle(5, {});
    CHECK(npc != contracts::kNoEntity);
    CHECK(car != npc);
    const auto requests = services.takeSpawnRequests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].kind == ai::SpawnRequest::Kind::Npc);
    CHECK(requests[0].model == 3);
    CHECK(requests[1].kind == ai::SpawnRequest::Kind::Vehicle);
    CHECK(services.takeSpawnRequests().empty());

    // Путь вокруг стены; урезается по maxPoints, последняя точка — цель
    contracts::Vec3 pts[64];
    const auto n = w.findPath({-100, 0, 0}, {100, 0, 0}, pts, 64);
    CHECK(n >= 2);
    CHECK(pts[n - 1].x == doctest::Approx(100.f).epsilon(0.05));
    CHECK(w.findPath({-100, 0, 0}, {100, 0, 0}, pts, 2) == 2);
    CHECK(w.findPath({-100, 0, 0}, {100, 0, 0}, nullptr, 2) == 0);

    // Розыск: преступление без свидетелей не засчитывается, со свидетелем — да; в святом месте — нарушение святыни
    CHECK(w.wantedLevel() == 0);
    w.addCrime(1, {0, 0, 0}, 1);
    CHECK(w.wantedLevel() == 0);
    backend.setWitnessed(true);
    w.addCrime(1, {0, 0, 0}, 1);
    CHECK(w.wantedLevel() == 1);
    CHECK(services.crimesReported() == 1);
    w.addCrime(1, {200, 0, 200}, 1);  // в святыне: Trespass (15 жара) вместо драки (12)
    CHECK(police.heat() == doctest::Approx(12.f + 15.f));
}
