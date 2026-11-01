// Тесты E11: JSON, навигационная сетка, архетипы (данные игры), толпа (расписания, LOD, реакции, реплики).
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "han/ai/archetype.h"
#include "han/ai/crowd.h"
#include "han/ai/navgrid.h"
#include "han/core/json.h"

using namespace han::ai;
using han::core::Json;

namespace {
std::vector<Vec2> rect(float x0, float z0, float x1, float z1) { return {{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}}; }

// Город-сетка 400 x 400 м: кварталы-здания 40 x 40 м, улицы между ними 10 м; границы карты свободны.
std::vector<std::vector<Vec2>> blocks() {
    std::vector<std::vector<Vec2>> out;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) out.push_back(rect(5.f + static_cast<float>(i) * 50.f, 5.f + static_cast<float>(j) * 50.f, 45.f + static_cast<float>(i) * 50.f, 45.f + static_cast<float>(j) * 50.f));
    return out;
}

NavGrid cityGrid() { return NavGrid::build(0, 0, 400, 400, blocks()); }

// Места на перекрёстках улиц (центры улиц: 50, 100, ..., 350): работа, кафе, магазины, еда, парк и т. д.
std::vector<Poi> cityPois() {
    static const char* cats[] = {"work", "cafe", "shop", "eat", "park", "campus", "market", "nightlife", "sights", "delivery", "security"};
    std::vector<Poi> pois;
    for (int n = 0; n < 49; ++n) pois.push_back({{50.f * static_cast<float>(1 + n % 7), 50.f * static_cast<float>(1 + n / 7)}, cats[n % 11]});
    return pois;
}

float pathLength(const std::vector<Vec2>& p) {
    float l = 0;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) l += std::sqrt((p[i + 1].x - p[i].x) * (p[i + 1].x - p[i].x) + (p[i + 1].z - p[i].z) * (p[i + 1].z - p[i].z));
    return l;
}

ArchetypeLibrary loadRealArchetypes() {
    ArchetypeLibrary lib;
    std::string error;
    REQUIRE_MESSAGE(lib.loadDirectory((std::filesystem::path(HAN_REPO_DIR) / "data" / "people" / "archetypes").string(), error), error);
    return lib;
}
}  // namespace

// ---------------------------------------------------------------- JSON

TEST_CASE("json: значения, вложенность, юникод и обратная запись") {
    Json j;
    std::string error;
    REQUIRE_MESSAGE(han::core::parseJson(R"({"a":[1,2.5,-3e2],"b":{"c":true,"d":null},"s":"x\"y\n\u0416\ud83d\ude00"})", j, error), error);
    CHECK(j["a"].size() == 3);
    CHECK(j["a"].at(1).asNumber() == doctest::Approx(2.5));
    CHECK(j["a"].at(2).asNumber() == doctest::Approx(-300.0));
    CHECK(j["b"]["c"].asBool());
    CHECK(j["b"]["d"].isNull());
    CHECK(j["s"].asString() == "x\"y\nЖ😀");
    CHECK(j["нет"].isNull());
    CHECK(j["a"].at(99).isNull());
    CHECK(j["a"]["не объект"].isNull());
    Json again;
    REQUIRE(han::core::parseJson(j.dump(2), again, error));
    CHECK(again.dump() == j.dump());
}

TEST_CASE("json: ошибки с позицией и жёсткие правила") {
    Json j;
    std::string error;
    const auto bad = [&](const char* text, const char* expected) {
        CHECK_FALSE(han::core::parseJson(text, j, error));
        CHECK_MESSAGE(error.find(expected) != std::string::npos, text << " -> " << error);
    };
    bad("{", "ключ");
    bad("{\"a\":1,}", "ключ");
    bad("[1,]", "значение");
    bad("[1 2]", "«,» или «]»");
    bad("{\"a\" 1}", "«:»");
    bad("{\"a\":1,\"a\":2}", "повторяющийся");
    bad("01", "ведущие нули");
    bad("1.", "цифры");
    bad("\"abc", "не закрыта");
    bad("\"\\x\"", "escape");
    bad("\"\\ud800\"", "суррогат");
    bad("\"\\u12\"", "\\u");
    bad("true false", "лишние данные");
    bad("", "ожидалось значение");
    bad("\"a\tb\"", "управляющий");
    bad("1e999", "диапазона");
    CHECK(error.find("строка 1") != std::string::npos);
    std::string deep(300, '[');
    bad(deep.c_str(), "глубокая");
    CHECK(han::core::parseJson("\xEF\xBB\xBF{\"ok\":1}", j, error));  // BOM
    CHECK(j["ok"].asNumber() == 1);
    CHECK_FALSE(han::core::loadJsonFile("нет_такого.json", j, error));
}

// ---------------------------------------------------------------- Навигация

TEST_CASE("navgrid: препятствия закрывают клетки, вложенные контуры объединяются") {
    std::vector<std::vector<Vec2>> obs = {rect(10, 10, 30, 30), rect(15, 15, 25, 25)};  // два вложенных контура: объединяются
    const NavGrid g = NavGrid::build(0, 0, 50, 50, obs);
    REQUIRE(g.valid());
    CHECK(g.width() == 50);
    CHECK_FALSE(g.walkable(12.f, 12.f));
    CHECK_FALSE(g.walkable(20.f, 20.f));  // внутри большого контура, вложенный не открывает дыру
    CHECK(g.walkable(5.f, 5.f));
    CHECK_FALSE(g.walkable(-1.f, 5.f));   // вне карты
    CHECK_FALSE(g.walkable(51.f, 5.f));
    CHECK(g.walkableCount() == 2500 - 400);  // закрыт квадрат 20 x 20
}

TEST_CASE("navgrid: путь по прямой, вокруг стены и вокруг двух стен — точки не лежат внутри зданий") {
    const NavGrid g = NavGrid::build(0, 0, 200, 200, {rect(95, 20, 105, 180)});
    std::vector<Vec2> p;
    REQUIRE(g.findPath({20, 100}, {80, 100}, p));
    CHECK(p.size() == 2);  // прямая видимость
    REQUIRE(g.findPath({20, 100}, {180, 100}, p));
    CHECK(p.size() >= 3);
    for (const Vec2& v : p) CHECK(g.walkable(v.x, v.z));
    for (std::size_t i = 0; i + 1 < p.size(); ++i) CHECK(g.lineOfSight(p[i], p[i + 1]));
    const float straight = 160.f, len = pathLength(p);
    CHECK(len > straight);
    CHECK(len < straight * 1.5f);  // обход короче 1,5 прямой: путь спрямлён
    CHECK(p.front().x == doctest::Approx(20.f));
    CHECK(p.back().x == doctest::Approx(180.f));
}

TEST_CASE("navgrid: цель внутри здания подтягивается к ближайшей свободной точке; замкнутая область недостижима") {
    const NavGrid g = NavGrid::build(0, 0, 100, 100, {rect(40, 40, 60, 60), rect(10, 10, 30, 12), rect(10, 28, 30, 30), rect(10, 10, 12, 30), rect(28, 10, 30, 30)});
    std::vector<Vec2> p;
    REQUIRE(g.findPath({5, 50}, {50, 50}, p));  // цель в центре здания
    CHECK(g.walkable(p.back().x, p.back().z));
    CHECK(std::fabs(p.back().x - 50.f) < 12.f);
    // Комната 10..30 закрыта со всех сторон стенами: внутрь пути нет.
    CHECK_FALSE(g.findPath({5, 5}, {20, 20}, p));
    CHECK(p.empty());
    CHECK_FALSE(NavGrid().findPath({0, 0}, {1, 1}, p));
}

TEST_CASE("navgrid: линия видимости не проходит между зданиями, касающимися углами") {
    const NavGrid g = NavGrid::build(0, 0, 40, 40, {rect(10, 10, 20, 20), rect(20, 20, 30, 30)});
    CHECK_FALSE(g.lineOfSight({5, 25}, {35, 15}));  // сквозь точку касания
    CHECK(g.lineOfSight({5, 5}, {35, 5}));
    CHECK_FALSE(g.lineOfSight({15, 15}, {15, 5}));  // начало внутри здания
}

TEST_CASE("navgrid: города-сетка — путь от угла до угла идёт по улицам, быстро и без срезания углов") {
    const NavGrid g = cityGrid();
    std::vector<Vec2> p;
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(g.findPath({2, 2}, {398, 398}, p));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[navgrid] путь по городу 400x400: %.1f мс, %zu точек, %zu раскрытий\n", ms, p.size(), g.lastExpansions());
    CHECK(ms < 2000.0);
    CHECK(pathLength(p) > 560.f);   // прямая 560 м: короче нельзя
    CHECK(pathLength(p) < 800.f);   // Манхэттен 796 м: не длиннее
    for (const Vec2& v : p) CHECK(g.walkable(v.x, v.z));
    for (std::size_t i = 0; i + 1 < p.size(); ++i) CHECK(g.lineOfSight(p[i], p[i + 1]));
    CHECK(g.lastExpansions() < g.walkableCount());
}

TEST_CASE("navgrid: потолок работы поиска возвращает «пути нет», а не зависает") {
    NavGrid::Settings s;
    s.maxExpansions = 50;
    const NavGrid g = NavGrid::build(0, 0, 400, 400, blocks(), s);
    std::vector<Vec2> p;
    CHECK_FALSE(g.findPath({2, 2}, {398, 398}, p));
}

TEST_CASE("navgrid: огромная или пустая область отклоняется без падения") {
    CHECK_FALSE(NavGrid::build(0, 0, 0, 0, {}).valid());
    CHECK_FALSE(NavGrid::build(0, 0, 1e6f, 1e6f, {}).valid());
    const NavGrid ok = NavGrid::build(0, 0, 10, 10, {{{1, 1}, {2, 2}}});  // контур из двух точек игнорируется
    CHECK(ok.walkableCount() == 100);
}

// ---------------------------------------------------------------- Архетипы

TEST_CASE("archetype: правильный JSON разбирается, расписание через полночь работает") {
    Json j;
    std::string error;
    REQUIRE(han::core::parseJson(R"({"id":"night_owl","numeric_id":42,"name_key":"people.archetype.night_owl","age_range":[20,40],
        "schedule":[{"from":22,"to":6,"activity":"sleep","place_category":"home"},{"from":6,"to":22,"activity":"work"}],
        "reactions":{"fight":"film"},"line_keys":["people.line.night_owl_hi"]})", j, error));
    Archetype a;
    REQUIRE_MESSAGE(parseArchetype(j, a, error), error);
    CHECK(a.numericId == 42);
    CHECK(a.activityAt(23.f) == Activity::Sleep);
    CHECK(a.activityAt(3.f) == Activity::Sleep);
    CHECK(a.activityAt(12.f) == Activity::Work);
    CHECK(a.activityAt(-1.f) == Activity::Sleep);   // отрицательные часы заворачиваются
    CHECK(a.activityAt(30.f) == Activity::Work);    // 30 ч = 6 ч
    CHECK(a.reactionTo(WorldEventType::Fight) == Reaction::Film);
    CHECK(a.walkSpeed == doctest::Approx(1.4f));
}

TEST_CASE("archetype: ошибки данных — возраст младше 18, лишние поля, неверные значения") {
    const auto bad = [](const std::string& json, const char* expected) {
        Json j;
        std::string error;
        REQUIRE(han::core::parseJson(json, j, error));
        Archetype a;
        CHECK_FALSE(parseArchetype(j, a, error));
        CHECK_MESSAGE(error.find(expected) != std::string::npos, json << " -> " << error);
    };
    const std::string head = R"({"id":"x1","numeric_id":1,"name_key":"people.a","age_range":[20,30],"schedule":[{"from":0,"to":24,"activity":"walk"}])";
    bad(R"({"id":"x1","numeric_id":1,"name_key":"people.a","age_range":[17,30],"schedule":[{"from":0,"to":24,"activity":"walk"}]})", "младше 18");
    bad(R"({"id":"X1","numeric_id":1,"name_key":"people.a","age_range":[20,30],"schedule":[{"from":0,"to":24,"activity":"walk"}]})", "id должен");
    bad(R"({"id":"x1","numeric_id":-1,"name_key":"people.a","age_range":[20,30],"schedule":[{"from":0,"to":24,"activity":"walk"}]})", "numeric_id");
    bad(R"({"id":"x1","numeric_id":1,"name_key":"ui.a","age_range":[20,30],"schedule":[{"from":0,"to":24,"activity":"walk"}]})", "people.");
    bad(R"({"id":"x1","numeric_id":1,"name_key":"people.a","age_range":[30,20],"schedule":[{"from":0,"to":24,"activity":"walk"}]})", "максимум");
    bad(R"({"id":"x1","numeric_id":1,"name_key":"people.a","age_range":[20,30],"schedule":[]})", "schedule");
    bad(R"({"id":"x1","numeric_id":1,"name_key":"people.a","age_range":[20,30],"schedule":[{"from":0,"to":25,"activity":"walk"}]})", "от 0 до 24");
    bad(R"({"id":"x1","numeric_id":1,"name_key":"people.a","age_range":[20,30],"schedule":[{"from":0,"to":24,"activity":"fly"}]})", "занятие");
    bad(R"({"id":"x1","numeric_id":1,"name_key":"people.a","age_range":[20,30],"schedule":[{"from":0,"to":24,"activity":"walk","x":1}]})", "лишнее");
    bad((head + R"(,"walk_speed":9})"), "walk_speed");
    bad((head + R"(,"courage":2})"), "courage");
    bad((head + R"(,"reactions":{"meteor":"flee"}})"), "событие");
    bad((head + R"(,"reactions":{"fight":"dance"}})"), "реакция");
    bad((head + R"(,"line_keys":["x.y"]})"), "people.line.");
    bad((head + R"(,"bonus":1})"), "лишнее");
    bad("[]", "объект");
}

TEST_CASE("archetype: реакция по умолчанию зависит от храбрости") {
    Archetype a;
    a.courage = 0.1f;
    CHECK(a.reactionTo(WorldEventType::Gunshot) == Reaction::Flee);
    a.courage = 0.4f;
    CHECK(a.reactionTo(WorldEventType::Fight) == Reaction::Watch);
    a.courage = 0.6f;
    CHECK(a.reactionTo(WorldEventType::Fight) == Reaction::Film);
    a.courage = 0.95f;
    CHECK(a.reactionTo(WorldEventType::Fight) == Reaction::Intervene);
    CHECK(a.reactionTo(WorldEventType::Siren) == Reaction::Film);
}

TEST_CASE("archetype: данные игры — 9 взрослых архетипов, расписание покрывает сутки, ключи локализации заведены") {
    const ArchetypeLibrary lib = loadRealArchetypes();
    CHECK(lib.size() >= 9);
    std::set<std::uint32_t> ids;
    for (const Archetype& a : lib.all()) {
        CHECK(ids.insert(a.numericId).second);
        CHECK(a.ageMin >= 18);
        CHECK(a.walkSpeed > 0.f);
        for (float h = 0.f; h < 24.f; h += 0.25f) CHECK_MESSAGE(a.entryAt(h) != nullptr, a.id << ": нет записи расписания в " << h << " ч");
        CHECK(a.lineKeys.size() >= 1);
    }
    CHECK(lib.byId("security_guard") != nullptr);
    CHECK(lib.byNumericId(3) != nullptr);
    CHECK(lib.byId("нет") == nullptr);
    // Все ключи есть в заявках на локализацию (contracts/loc_requests/A.csv).
    std::ifstream in(std::filesystem::path(HAN_REPO_DIR) / "contracts" / "loc_requests" / "A.csv", std::ios::binary);
    std::set<std::string> keys;
    std::string line;
    while (std::getline(in, line)) keys.insert(line.substr(0, line.find(',')));
    for (const Archetype& a : lib.all()) {
        CHECK_MESSAGE(keys.count(a.nameKey), a.nameKey << " не заведён в A.csv");
        for (const std::string& k : a.lineKeys) CHECK_MESSAGE(keys.count(k), k << " не заведён в A.csv");
    }
    for (const char* r : {"flee", "watch", "film", "call_police", "intervene"}) CHECK(keys.count(std::string("people.react.") + r));
}

TEST_CASE("archetype: библиотека отклоняет повтор id и numeric_id, ошибки в одном файле не мешают остальным") {
    ArchetypeLibrary lib;
    std::string error;
    Archetype a, b, c;
    a.id = "a";
    a.numericId = 1;
    b.id = "a";
    b.numericId = 2;
    c.id = "c";
    c.numericId = 1;
    CHECK(lib.add(a, error));
    CHECK_FALSE(lib.add(b, error));
    CHECK_FALSE(lib.add(c, error));
    CHECK(lib.size() == 1);
    const auto dir = std::filesystem::temp_directory_path() / "han_arch_test";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "good.json") << R"({"id":"ok_one","numeric_id":7,"name_key":"people.x","age_range":[20,30],"schedule":[{"from":0,"to":24,"activity":"walk"}]})";
    std::ofstream(dir / "broken.json") << "{ не json";
    std::ofstream(dir / "readme.txt") << "игнорируется";
    ArchetypeLibrary lib2;
    CHECK_FALSE(lib2.loadDirectory(dir.string(), error));
    CHECK(error.find("broken.json") != std::string::npos);
    CHECK(lib2.size() == 1);
    CHECK_FALSE(lib2.loadDirectory("нет_папки", error));
}

// ---------------------------------------------------------------- Толпа

TEST_CASE("crowd: население ставится на проходимые места, лимит и неудачные места учитываются") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    CrowdSettings s;
    s.maxAgents = 120;
    Crowd c(g, cityPois(), s);
    CHECK(c.populate(lib.pointers(), 500) == 120);  // лимит агентов
    CHECK(c.agents().size() == 120);
    for (const Agent& a : c.agents()) CHECK(g.walkable(a.pos.x, a.pos.z));
    CHECK(c.spawn(*lib.byId("tourist"), {10, 10}) == 0);  // лимит исчерпан
    Crowd c2(g, {}, {});
    CHECK(c2.spawn(*lib.byId("tourist"), {10, 10}) != 0);   // внутри квартала у края: подтянется к улице
    CHECK(g.walkable(c2.agents()[0].pos.x, c2.agents()[0].pos.z));
    CHECK(c2.spawn(*lib.byId("tourist"), {25, 25}) == 0);      // в центре квартала дальше 15 м от улицы
    CHECK(c2.spawn(*lib.byId("tourist"), {-500, -500}) == 0);  // вдали от карты
}

TEST_CASE("crowd: 5 минут утра — агенты не выходят на здания, пути запрашиваются по лимиту, идут по расписанию к работе") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    CrowdSettings s;
    s.maxAgents = 200;
    Crowd c(g, cityPois(), s);
    c.populate({lib.byId("office_worker")}, 120);
    c.setHour(10.f);  // рабочее время
    int maxQueries = 0, failures = 0, offGrid = 0;
    for (int i = 0; i < 3000; ++i) {  // 300 с по 0,1 с
        c.update(0.1f, {200, 200});
        maxQueries = std::max(maxQueries, c.stats().pathQueries);
        failures += c.stats().pathFailures;
        if (i % 50 == 0)
            for (const Agent& a : c.agents()) {
                if (!g.walkable(a.pos.x, a.pos.z) || !std::isfinite(a.pos.x) || !std::isfinite(a.pos.z)) ++offGrid;
            }
    }
    CHECK(offGrid == 0);
    CHECK(maxQueries <= s.maxPathQueriesPerFrame);
    int atWork = 0;
    for (const Agent& a : c.agents()) {
        float best = 1e9f;
        for (const Poi& p : cityPois())
            if (p.category == "work") best = std::min(best, std::sqrt((p.pos.x - a.pos.x) * (p.pos.x - a.pos.x) + (p.pos.z - a.pos.z) * (p.pos.z - a.pos.z)));
        atWork += best < 8.f ? 1 : 0;
        CHECK(a.activity == Activity::Work);
    }
    CHECK(atWork >= 84);  // не меньше 70 % уже на работе
    std::printf("[crowd] на работе %d из 120, неудачных поисков пути %d\n", atWork, failures);
}

TEST_CASE("crowd: смена часа переводит агентов на новое занятие — вечером офисные уходят домой") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    Crowd c(g, cityPois(), {});
    c.populate({lib.byId("office_worker")}, 60);
    c.setHour(10.f);
    for (int i = 0; i < 2000; ++i) c.update(0.1f, {200, 200});
    c.setHour(23.f);
    for (int i = 0; i < 3500; ++i) c.update(0.1f, {200, 200});
    int home = 0;
    for (const Agent& a : c.agents()) {
        const float d = std::sqrt((a.home.x - a.pos.x) * (a.home.x - a.pos.x) + (a.home.z - a.pos.z) * (a.home.z - a.pos.z));
        home += d < 6.f ? 1 : 0;
        CHECK(a.activity == Activity::Home);
    }
    CHECK(home >= 48);  // 80 % дома
}

TEST_CASE("crowd: уровни детализации — счётчики сходятся, дальние агенты обновляются редко") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    CrowdSettings s;
    s.nearRadius = 40.f;
    s.midRadius = 100.f;
    s.farRadius = 200.f;
    Crowd c(g, cityPois(), s);
    c.populate(lib.pointers(), 150);
    c.setHour(12.f);
    c.update(1.f / 60.f, {200, 200});
    const CrowdStats st = c.stats();
    CHECK(st.near + st.mid + st.far + st.dormant == st.agents);
    CHECK(st.near > 0);
    CHECK(st.dormant > 0);
    for (const Agent& a : c.agents()) {
        const float d = std::sqrt((a.pos.x - 200.f) * (a.pos.x - 200.f) + (a.pos.z - 200.f) * (a.pos.z - 200.f));
        if (a.lod == LodTier::Near) CHECK(d <= 40.f + 1e-3f);
        if (a.lod == LodTier::Dormant) CHECK(d > 200.f - 1e-3f);
    }
    // Дальние почти не обновляются: за 59 кадров между тактами позиция одного и того же агента не меняется.
    const AgentId far = [&] {
        for (const Agent& a : c.agents())
            if (a.lod == LodTier::Dormant) return a.id;
        return AgentId{0};
    }();
    REQUIRE(far != 0);
    c.update(1.f / 60.f, {200, 200});
    const Vec2 p0 = c.agent(far)->pos;
    int changed = 0;
    for (int i = 0; i < 60; ++i) {
        c.update(1.f / 60.f, {200, 200});
        const Vec2 p = c.agent(far)->pos;
        changed += (p.x != p0.x || p.z != p0.z) ? 1 : 0;
    }
    CHECK(changed <= 40);  // меняется не на каждом кадре
}

TEST_CASE("crowd: выстрел — пугливые убегают от места, возникают реплики и счётчик бегущих") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    Crowd c(g, cityPois(), {});
    for (int i = 0; i < 30; ++i) c.spawn(*lib.byId("office_worker"), {180.f + static_cast<float>(i % 6) * 8.f, 180.f + static_cast<float>(i / 6) * 8.f});
    c.setHour(12.f);
    for (int i = 0; i < 100; ++i) c.update(0.1f, {200, 200});  // осесть
    const Vec2 boom{200, 200};
    auto meanDistance = [&] {
        double sum = 0;
        for (const Agent& a : c.agents()) sum += std::sqrt((a.pos.x - boom.x) * (a.pos.x - boom.x) + (a.pos.z - boom.z) * (a.pos.z - boom.z));
        return sum / static_cast<double>(c.agents().size());
    };
    const double before = meanDistance();
    c.drainUtterances();
    c.postEvent({WorldEventType::Gunshot, boom, 80.f});
    int fleeing = 0;
    for (const Agent& a : c.agents()) fleeing += a.state == AgentState::Fleeing ? 1 : 0;
    CHECK(fleeing >= 15);  // реакция по вероятности, но большинство рядом заметило
    for (int i = 0; i < 100; ++i) c.update(0.1f, {200, 200});
    CHECK(meanDistance() > before + 20.0);
    const auto lines = c.drainUtterances();
    CHECK_FALSE(lines.empty());
    CHECK(std::any_of(lines.begin(), lines.end(), [](const Utterance& u) { return u.key == "people.react.flee"; }));
    CHECK(c.drainUtterances().empty());
}

TEST_CASE("crowd: драка — охранник идёт к месту, турист снимает, лавочник вызывает полицию, пожилой смотрит") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    Crowd c(g, cityPois(), {});
    const Vec2 fight{200, 200};
    const AgentId guard = c.spawn(*lib.byId("security_guard"), {200, 240});
    const AgentId tourist = c.spawn(*lib.byId("tourist"), {200, 160});
    const AgentId shop = c.spawn(*lib.byId("shopkeeper"), {160, 200});
    const AgentId elder = c.spawn(*lib.byId("elderly_walker"), {240, 200});
    REQUIRE(guard * tourist * shop * elder != 0);
    c.setHour(12.f);
    for (int i = 0; i < 20; ++i) c.update(0.1f, {200, 200});
    const Vec2 guardBefore = c.agent(guard)->pos;
    bool filmed = false, police = false;
    bool seenGuard = false, seenElder = false;
    for (int attempt = 0; attempt < 30 && !(filmed && police && seenGuard && seenElder); ++attempt) {
        c.postEvent({WorldEventType::Fight, fight, 80.f});
        for (int i = 0; i < 90; ++i) c.update(0.1f, {200, 200});
        for (const CrowdEvent& e : c.drainEvents()) {
            filmed = filmed || (e.type == CrowdEvent::Type::Filmed && e.agent == tourist);
            police = police || (e.type == CrowdEvent::Type::PoliceCalled && e.agent == shop);
        }
        seenGuard = seenGuard || c.agent(guard)->state == AgentState::Intervening || c.agent(guard)->state == AgentState::Watching;
        seenElder = seenElder || c.agent(elder)->state == AgentState::Watching;
    }
    CHECK(filmed);
    CHECK(police);
    CHECK(seenGuard);
    CHECK(seenElder);
    const auto d = [&](Vec2 p) { return std::sqrt((p.x - fight.x) * (p.x - fight.x) + (p.z - fight.z) * (p.z - fight.z)); };
    CHECK(d(c.agent(guard)->pos) < d(guardBefore) - 10.f);
}

TEST_CASE("crowd: события вне радиуса и при перезарядке реакции игнорируются") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    Crowd c(g, cityPois(), {});
    const AgentId a = c.spawn(*lib.byId("office_worker"), {300, 300});
    c.update(0.1f, {300, 300});
    c.postEvent({WorldEventType::Gunshot, {20, 20}, 40.f});
    CHECK(c.agent(a)->state != AgentState::Fleeing);
    for (int i = 0; i < 5; ++i) c.postEvent({WorldEventType::Gunshot, {300, 300}, 40.f});  // подряд: реагирует один раз
    const AgentState first = c.agent(a)->state;
    CHECK((first == AgentState::Fleeing || first == AgentState::Idle || first == AgentState::Waiting));
}

TEST_CASE("crowd: один и тот же сид даёт одно и то же поведение, разные сиды — разное") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    const auto run = [&](std::uint32_t seed) {
        CrowdSettings s;
        s.seed = seed;
        Crowd c(g, cityPois(), s);
        c.populate(lib.pointers(), 80);
        c.setHour(9.f);
        for (int i = 0; i < 600; ++i) c.update(0.1f, {100, 100});
        double sum = 0;
        for (const Agent& a : c.agents()) sum += a.pos.x * 1.3 + a.pos.z * 0.7;
        return sum;
    };
    CHECK(run(7) == run(7));
    CHECK(run(7) != run(8));
}

TEST_CASE("crowd: 1000 агентов на карте — стоимость кадра (справочно) и отсутствие NaN") {
    const NavGrid g = cityGrid();
    const ArchetypeLibrary lib = loadRealArchetypes();
    CrowdSettings s;
    s.maxAgents = 1000;
    Crowd c(g, cityPois(), s);
    REQUIRE(c.populate(lib.pointers(), 1000) == 1000);
    c.setHour(9.f);
    for (int i = 0; i < 300; ++i) c.update(1.f / 60.f, {200, 200});  // прогрев: пути разобраны
    const auto t0 = std::chrono::steady_clock::now();
    const int frames = 600;
    for (int i = 0; i < frames; ++i) c.update(1.f / 60.f, {200, 200});
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    std::printf("[crowd] 1000 агентов: %.3f мс за кадр (near %d, mid %d, far %d, dormant %d)\n", ms, c.stats().near, c.stats().mid, c.stats().far, c.stats().dormant);
    for (const Agent& a : c.agents()) CHECK((std::isfinite(a.pos.x) && std::isfinite(a.pos.z) && g.walkable(a.pos.x, a.pos.z)));
    CHECK(ms < 20.0);  // грубый потолок против катастрофы, не бенчмарк
}
