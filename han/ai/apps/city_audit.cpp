// han_city_audit — аудит нарушений трафика и толпы на реальной карте (этап 04 плана города), без окна и видеокарты.
//   han_city_audit --osm assets_src/osm/seoul_connected.osm --focus 700 280 [--minutes 30] [--cars 120] [--people 280]
//                  [--water assets_bin/city/seoul_water_areas.json] [--legacy-nav] [--no-smooth] [--seed N] [--out отчёт.json]
// Машины: сколько шагов центр машины внутри здания, в воде, вне своей проезжей части, рывок позиции больше возможного, резкий поворот, перекрытия.
// Люди: внутри здания, в воде, на проезжей части вне переходов, рывок, застрявшие. --legacy-nav строит сетку пешеходов как раньше (только здания);
// --no-smooth отключает дугу через перекрёсток. Это измерение, а не оценка правил движения: светофоров и приоритетов в модели нет.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
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
// Быстрый ответ «точка внутри одного из многоугольников»: равномерная сетка по габаритам.
class PolygonIndex {
public:
    explicit PolygonIndex(float cell = 32.f) : cell_(cell) {}
    void add(const std::vector<world::Vec2>& poly) {
        if (poly.size() < 3) return;
        Box b{1e30f, 1e30f, -1e30f, -1e30f};
        for (const auto& p : poly) { b.minX = std::min(b.minX, p.x); b.maxX = std::max(b.maxX, p.x); b.minZ = std::min(b.minZ, p.y); b.maxZ = std::max(b.maxZ, p.y); }
        const int id = static_cast<int>(polys_.size());
        polys_.push_back(poly);
        boxes_.push_back(b);
        for (int cz = cellOf(b.minZ); cz <= cellOf(b.maxZ); ++cz)
            for (int cx = cellOf(b.minX); cx <= cellOf(b.maxX); ++cx) grid_[key(cx, cz)].push_back(id);
    }
    bool contains(float x, float z) const {
        const auto it = grid_.find(key(cellOf(x), cellOf(z)));
        if (it == grid_.end()) return false;
        for (const int id : it->second) {
            const Box& b = boxes_[static_cast<std::size_t>(id)];
            if (x < b.minX || x > b.maxX || z < b.minZ || z > b.maxZ) continue;
            const auto& poly = polys_[static_cast<std::size_t>(id)];
            bool in = false;
            for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
                if ((poly[i].y > z) != (poly[j].y > z) && x < (poly[j].x - poly[i].x) * (z - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
            if (in) return true;
        }
        return false;
    }
    // Насколько глубоко точка внутри содержащего её многоугольника: расстояние до его границы, м; 0 — снаружи. Клетка сетки 1 м даёт погрешность
    // у стены до 0,7 м, поэтому нарушением считается проникновение глубже порога.
    float depth(float x, float z) const {
        const auto it = grid_.find(key(cellOf(x), cellOf(z)));
        if (it == grid_.end()) return 0.f;
        float best = 0.f;
        for (const int id : it->second) {
            const Box& b = boxes_[static_cast<std::size_t>(id)];
            if (x < b.minX || x > b.maxX || z < b.minZ || z > b.maxZ) continue;
            const auto& poly = polys_[static_cast<std::size_t>(id)];
            bool in = false;
            float edge = 1e30f;
            for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
                if ((poly[i].y > z) != (poly[j].y > z) && x < (poly[j].x - poly[i].x) * (z - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
                const float dx = poly[j].x - poly[i].x, dz = poly[j].y - poly[i].y, len2 = dx * dx + dz * dz;
                const float s = len2 > 1e-6f ? std::clamp(((x - poly[i].x) * dx + (z - poly[i].y) * dz) / len2, 0.f, 1.f) : 0.f;
                edge = std::min(edge, std::hypot(x - (poly[i].x + dx * s), z - (poly[i].y + dz * s)));
            }
            if (in) best = std::max(best, edge);
        }
        return best;
    }
    std::size_t size() const { return polys_.size(); }

private:
    struct Box { float minX, minZ, maxX, maxZ; };
    int cellOf(float v) const { return static_cast<int>(std::floor(v / cell_)); }
    static std::int64_t key(int cx, int cz) { return (static_cast<std::int64_t>(cx) << 32) ^ static_cast<std::uint32_t>(cz); }
    float cell_;
    std::vector<std::vector<world::Vec2>> polys_;
    std::vector<Box> boxes_;
    std::unordered_map<std::int64_t, std::vector<int>> grid_;
};

float distanceToEdge(const ai::RoadEdge& e, ai::Vec2 p) {
    float best = 1e30f;
    for (std::size_t i = 0; i + 1 < e.pts.size(); ++i) {
        const ai::Vec2 a = e.pts[i], b = e.pts[i + 1];
        const float dx = b.x - a.x, dz = b.z - a.z, len2 = dx * dx + dz * dz;
        const float t = len2 > 1e-6f ? std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / len2, 0.f, 1.f) : 0.f;
        best = std::min(best, std::hypot(p.x - (a.x + dx * t), p.z - (a.z + dz * t)));
    }
    return best;
}

constexpr float kDepth = 0.75f;  // порог проникновения в здание/воду/проезжую часть, м

struct Args {
    std::string osm = "assets_src/osm/seoul_connected.osm", water, out;
    float focusX = 700.f, focusZ = 280.f, minutes = 30.f;
    int cars = 120, people = 280;
    bool legacyNav = false, smooth = true, turnLanes = false;
    float connectorRadius = 0.f;  // --connector-radius R: половина длины дуги, м (0 — по умолчанию движка)
    unsigned seed = 1;
};

struct CarMetrics {
    long steps = 0, inBuilding = 0, inWater = 0, outsideOwnRoad = 0, jumps = 0, sharpTurns = 0, overlaps = 0, uTurns = 0;
    float maxJump = 0.f, maxOutside = 0.f, maxTurnDegrees = 0.f;
    std::uint64_t spawned = 0, despawned = 0, stuckResets = 0;
    float meanSpeed = 0.f;
};

struct PeopleMetrics {
    long steps = 0, inBuilding = 0, inWater = 0, onRoadwayOutsideCrossing = 0, jumps = 0, nearJumps = 0, outsideNav = 0;
    float maxJump = 0.f;
    std::size_t agents = 0, spawnedBlocked = 0, stuck = 0, maxStuckSeconds = 0;
    int pathFailures = 0, pathQueries = 0;
    long crossingSteps = 0;
};
}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const auto has = [&](const char* name, int n) { return !std::strcmp(argv[i], name) && i + n < argc; };
        if (has("--osm", 1)) a.osm = argv[++i];
        else if (has("--water", 1)) a.water = argv[++i];
        else if (has("--out", 1)) a.out = argv[++i];
        else if (has("--focus", 2)) { a.focusX = static_cast<float>(std::atof(argv[i + 1])); a.focusZ = static_cast<float>(std::atof(argv[i + 2])); i += 2; }
        else if (has("--minutes", 1)) a.minutes = static_cast<float>(std::atof(argv[++i]));
        else if (has("--cars", 1)) a.cars = std::atoi(argv[++i]);
        else if (has("--people", 1)) a.people = std::atoi(argv[++i]);
        else if (has("--seed", 1)) a.seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (has("--connector-radius", 1)) a.connectorRadius = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--legacy-nav")) a.legacyNav = true;
        else if (!std::strcmp(argv[i], "--no-smooth")) a.smooth = false;
        else if (!std::strcmp(argv[i], "--turn-lanes")) a.turnLanes = true;
    }
    std::string error;
    world::OsmData osm;
    if (!world::loadOsmFile(a.osm, osm, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    world::CityStyle style;
    if (!world::loadCityStyle("data/city/streetscape.json", style, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    const world::GeoProjection projection(style.originLat, style.originLon);
    world::WalkLayout layout = world::buildWalkLayout(osm, projection, style);
    if (!a.water.empty() && !layout.addWaterAreas(a.water, projection, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    PolygonIndex buildings, water, roadway, crossings;
    for (const auto& p : layout.buildings) buildings.add(p);
    for (const auto& p : layout.water) water.add(p);
    for (const auto& p : layout.carriageways) roadway.add(p);
    for (const auto& p : layout.crossings) crossings.add(p);
    std::printf("карта: зданий %zu, воды %zu, проезжей части %zu, переходов %zu; режим: %s, дуги %s\n", buildings.size(), water.size(), roadway.size(), crossings.size(),
                a.legacyNav ? "прежняя сетка (только здания)" : "новая сетка", a.smooth ? "вкл" : "выкл");

    const float dt = 0.1f;
    const long steps = static_cast<long>(a.minutes * 60.f / dt);

    // ---------------- Машины ----------------
    CarMetrics cm;
    {
        ai::RoadGraphSettings gs;
        const auto graph = ai::RoadGraph::build(osm, projection, gs);
        ai::TrafficSettings ts;
        ts.maxCars = a.cars;
        ts.spawnRadius = 350.f;
        ts.despawnRadius = 550.f;
        ts.seed = a.seed;
        ts.smoothTurns = a.smooth;
        ts.turnLanes = a.turnLanes;
        ts.connectorRadius = a.connectorRadius;
        ai::TrafficSim sim(graph, ts);
        sim.setFocus({a.focusX, a.focusZ});
        std::unordered_map<std::uint32_t, ai::Car> last;
        double speedSum = 0;
        long speedCount = 0;
        for (long step = 0; step < steps; ++step) {
            sim.update(dt);
            for (const ai::Car& car : sim.cars()) {
                if (car.edge < 0) continue;
                ++cm.steps;
                speedSum += car.v;
                ++speedCount;
                if (buildings.depth(car.pos.x, car.pos.z) > kDepth) ++cm.inBuilding;
                if (water.depth(car.pos.x, car.pos.z) > kDepth) ++cm.inWater;
                const ai::RoadEdge& e = graph.edges()[static_cast<std::size_t>(car.edge)];
                const float own = distanceToEdge(e, car.pos);
                float allowed = e.centerOffset + static_cast<float>(e.lanes) * e.laneWidth + 1.0f;
                if (car.prev >= 0) {  // у перекрёстка допустим выход за свою проезжую часть на ширину соседней
                    const ai::RoadEdge& pe = graph.edges()[static_cast<std::size_t>(car.prev)];
                    allowed = std::max(allowed, pe.centerOffset + static_cast<float>(pe.lanes) * pe.laneWidth + 1.0f);
                }
                if (car.next >= 0) {
                    const ai::RoadEdge& ne = graph.edges()[static_cast<std::size_t>(car.next)];
                    allowed = std::max(allowed, ne.centerOffset + static_cast<float>(ne.lanes) * ne.laneWidth + 1.0f);
                }
                if (own > allowed) { ++cm.outsideOwnRoad; cm.maxOutside = std::max(cm.maxOutside, own - allowed); }
                const auto it = last.find(car.id);
                if (it != last.end()) {
                    const float jump = std::hypot(car.pos.x - it->second.pos.x, car.pos.z - it->second.pos.z);
                    const float possible = std::max(car.v, it->second.v) * dt * 1.5f + 0.3f;
                    float dh = std::fabs(car.heading - it->second.heading);
                    if (dh > 3.14159265f) dh = 6.2831853f - dh;
                    const float degrees = dh * 57.29578f;
                    if (degrees > 150.f) {  // разворот в тупике: прежний переезд на встречную полосу, считается отдельно
                        ++cm.uTurns;
                    } else {
                        cm.maxJump = std::max(cm.maxJump, jump);
                        cm.maxTurnDegrees = std::max(cm.maxTurnDegrees, degrees);
                        if (jump > possible) ++cm.jumps;
                        if (degrees > 25.f && jump > 0.05f) ++cm.sharpTurns;
                    }
                }
                last[car.id] = car;
            }
            cm.overlaps += sim.overlaps();
        }
        cm.spawned = sim.stats().spawned;
        cm.despawned = sim.stats().despawned;
        cm.stuckResets = sim.stats().stuckResets;
        cm.meanSpeed = speedCount ? static_cast<float>(speedSum / static_cast<double>(speedCount)) : 0.f;
    }

    // ---------------- Люди ----------------
    PeopleMetrics pm;
    {
        std::vector<std::vector<ai::Vec2>> hard, roads, openings;
        const float radius = style.crowdNavRadius;
        const float minX = a.focusX - radius, minZ = a.focusZ - radius, maxX = a.focusX + radius, maxZ = a.focusZ + radius;
        const auto convert = [&](const std::vector<std::vector<world::Vec2>>& polygons, std::vector<std::vector<ai::Vec2>>& out) {
            for (const auto& poly : polygons) {
                float lox = 1e30f, hix = -1e30f, loz = 1e30f, hiz = -1e30f;
                for (const auto& p : poly) { lox = std::min(lox, p.x); hix = std::max(hix, p.x); loz = std::min(loz, p.y); hiz = std::max(hiz, p.y); }
                if (hix < minX || lox > maxX || hiz < minZ || loz > maxZ) continue;
                std::vector<ai::Vec2> c;
                for (const auto& p : poly) c.push_back({p.x, p.y});
                out.push_back(std::move(c));
            }
        };
        convert(layout.buildings, hard);
        if (!a.legacyNav) {
            convert(layout.water, hard);
            convert(layout.carriageways, roads);
            convert(layout.crossings, openings);
        }
        ai::NavGrid::Settings ns;
        ns.cellSize = style.crowdNavCell;
        ns.maxExpansions = static_cast<std::size_t>(style.crowdPathExpansions);
        const ai::NavGrid grid = ai::NavGrid::build(minX, minZ, maxX, maxZ, hard, roads, openings, ns);
        ai::ArchetypeLibrary library;
        if (!library.loadDirectory("data/people/archetypes", error) || library.size() == 0) { std::fprintf(stderr, "архетипы: %s\n", error.c_str()); return 1; }
        static const char* categories[] = {"work", "cafe", "shop", "eat", "park", "campus", "market", "nightlife", "sights", "delivery", "security"};
        std::vector<ai::Poi> pois, nearby;
        for (std::size_t i = 0; i < layout.buildings.size(); ++i) {
            const auto& fp = layout.buildings[i];
            if (fp.size() < 2) continue;
            const ai::Vec2 edge{(fp[0].x + fp[1].x) * 0.5f, (fp[0].y + fp[1].y) * 0.5f};
            if (std::fabs(edge.x - a.focusX) > radius || std::fabs(edge.z - a.focusZ) > radius) continue;
            ai::Vec2 door;
            if (!grid.nearestWalkable(edge, 12.f, door)) continue;
            pois.push_back({door, categories[i % std::size(categories)]});
            const float dx = door.x - a.focusX, dz = door.z - a.focusZ;
            if (dx * dx + dz * dz < style.crowdRadius * style.crowdRadius) nearby.push_back(pois.back());
        }
        if (nearby.empty()) { std::fprintf(stderr, "рядом с точкой %.0f %.0f нет зданий для спавна толпы\n", a.focusX, a.focusZ); return 1; }
        ai::CrowdSettings cs;
        cs.maxAgents = static_cast<std::size_t>(a.people);
        cs.maxPathQueriesPerFrame = style.crowdPathQueries > 0 ? static_cast<int>(style.crowdPathQueries) : 4;
        cs.seed = 12345u + a.seed;
        ai::Crowd crowd(grid, std::move(pois), cs);
        const auto archetypes = library.pointers();
        for (int i = 0; i < a.people; ++i) {
            const auto& place = nearby[(static_cast<std::size_t>(i) * 73) % nearby.size()];
            const float jx = static_cast<float>((i * 17) % 11 - 5) * 0.7f, jz = static_cast<float>((i * 23) % 13 - 6) * 0.7f;
            crowd.spawn(*archetypes[static_cast<std::size_t>(i) % archetypes.size()], {place.pos.x + jx, place.pos.z + jz});
        }
        pm.agents = crowd.agents().size();
        for (const auto& ag : crowd.agents())
            if (!grid.walkable(ag.pos.x, ag.pos.z)) ++pm.spawnedBlocked;
        crowd.setHour(12.f);
        std::vector<ai::Vec2> lastPos(crowd.agents().size());
        std::vector<float> stillFor(crowd.agents().size(), 0.f);
        for (std::size_t i = 0; i < crowd.agents().size(); ++i) lastPos[i] = crowd.agents()[i].pos;
        std::vector<char> wasStuck(crowd.agents().size(), 0);
        for (long step = 0; step < steps; ++step) {
            crowd.setHour(12.f + static_cast<float>(step) * dt / 3600.f);
            crowd.update(dt, {a.focusX, a.focusZ});
            const auto& agents = crowd.agents();
            for (std::size_t i = 0; i < agents.size(); ++i) {
                const ai::Agent& ag = agents[i];
                ++pm.steps;
                if (buildings.depth(ag.pos.x, ag.pos.z) > kDepth) ++pm.inBuilding;
                if (water.depth(ag.pos.x, ag.pos.z) > kDepth) ++pm.inWater;
                const bool onRoad = roadway.depth(ag.pos.x, ag.pos.z) > kDepth;
                if (onRoad && crossings.contains(ag.pos.x, ag.pos.z)) ++pm.crossingSteps;
                else if (onRoad) ++pm.onRoadwayOutsideCrossing;
                if (ag.pos.x < minX || ag.pos.x > maxX || ag.pos.z < minZ || ag.pos.z > maxZ) ++pm.outsideNav;
                const float jump = std::hypot(ag.pos.x - lastPos[i].x, ag.pos.z - lastPos[i].z);
                pm.maxJump = std::max(pm.maxJump, jump);
                if (jump > 1.5f) {
                    ++pm.jumps;
                    if (ag.lod == ai::LodTier::Near || ag.lod == ai::LodTier::Mid) ++pm.nearJumps;  // дальние агенты обновляются реже по замыслу LOD
                }
                const bool moving = ag.state == ai::AgentState::Walking || ag.state == ai::AgentState::Fleeing;
                stillFor[i] = (moving && jump < 0.005f) ? stillFor[i] + dt : 0.f;
                if (stillFor[i] > 120.f && !wasStuck[i]) {
                    wasStuck[i] = 1;
                    ++pm.stuck;
                    if (pm.stuck <= 4)  // первые застрявшие: куда шли, есть ли путь, стоят ли на проходимой клетке
                        std::fprintf(stderr, "ЗАСТРЯЛ агент %u: t=%.0f с, состояние %d, LOD %d, поз %.1f %.1f, цель %.1f %.1f, путь %zu/%zu, needsPath %d, клетка проходима %d\n", ag.id, static_cast<double>(step) * static_cast<double>(dt),
                                     static_cast<int>(ag.state), static_cast<int>(ag.lod), static_cast<double>(ag.pos.x), static_cast<double>(ag.pos.z), static_cast<double>(ag.destination.x),
                                     static_cast<double>(ag.destination.z), ag.pathIndex, ag.path.size(), ag.needsPath ? 1 : 0, grid.walkable(ag.pos.x, ag.pos.z) ? 1 : 0);
                }
                pm.maxStuckSeconds = std::max(pm.maxStuckSeconds, static_cast<std::size_t>(stillFor[i]));
                lastPos[i] = ag.pos;
            }
        }
        pm.pathFailures = crowd.stats().pathFailures;
        pm.pathQueries = crowd.stats().pathQueries;
    }

    char buffer[4096];
    std::snprintf(buffer, sizeof(buffer),
        "{\n  \"minutes\": %.1f, \"focus\": [%.0f, %.0f], \"legacy_nav\": %s, \"smooth_turns\": %s,\n"
        "  \"cars\": {\"car_steps\": %ld, \"in_building\": %ld, \"in_water\": %ld, \"outside_own_road\": %ld, \"max_outside_m\": %.2f, \"position_jumps\": %ld, \"max_jump_m\": %.2f,\n"
        "    \"sharp_turns_25deg_per_step\": %ld, \"max_turn_deg_per_step\": %.1f, \"overlaps\": %ld, \"u_turns_at_dead_ends\": %ld, \"spawned\": %llu, \"despawned\": %llu, \"stuck_resets\": %llu, \"mean_speed_mps\": %.2f},\n"
        "  \"people\": {\"agents\": %zu, \"agent_steps\": %ld, \"spawned_on_blocked_cell\": %zu, \"in_building\": %ld, \"in_water\": %ld, \"on_roadway_outside_crossing\": %ld,\n"
        "    \"on_crossing\": %ld, \"outside_nav_area\": %ld, \"position_jumps\": %ld, \"near_mid_jumps\": %ld, \"max_jump_m\": %.2f, \"stuck_over_120s\": %zu, \"max_still_seconds\": %zu, \"path_queries\": %d, \"path_failures\": %d}\n}\n",
        static_cast<double>(a.minutes), static_cast<double>(a.focusX), static_cast<double>(a.focusZ), a.legacyNav ? "true" : "false", a.smooth ? "true" : "false",
        cm.steps, cm.inBuilding, cm.inWater, cm.outsideOwnRoad, static_cast<double>(cm.maxOutside), cm.jumps, static_cast<double>(cm.maxJump), cm.sharpTurns,
        static_cast<double>(cm.maxTurnDegrees), cm.overlaps, cm.uTurns, static_cast<unsigned long long>(cm.spawned), static_cast<unsigned long long>(cm.despawned),
        static_cast<unsigned long long>(cm.stuckResets), static_cast<double>(cm.meanSpeed), pm.agents, pm.steps, pm.spawnedBlocked, pm.inBuilding, pm.inWater,
        pm.onRoadwayOutsideCrossing, pm.crossingSteps, pm.outsideNav, pm.jumps, pm.nearJumps, static_cast<double>(pm.maxJump), pm.stuck, pm.maxStuckSeconds, pm.pathQueries, pm.pathFailures);
    std::fputs(buffer, stdout);
    if (!a.out.empty()) std::ofstream(a.out, std::ios::binary) << buffer;
    return 0;
}
