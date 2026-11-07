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
#include "han/ai/city_crowd.h"
#include "han/ai/life_profile.h"
#include "han/ai/crowd.h"
#include "han/ai/navgrid.h"
#include "han/ai/traffic.h"
#include "han/world/city_style.h"
#include "han/render/vehicle_pose.h"
#include "han/world/osm.h"
#include "han/world/road_profile.h"
#include "han/world/terrain.h"
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
    std::string osm = "assets_src/osm/seoul_connected.osm", water, out, terrain, profile;
    float focusX = 700.f, focusZ = 280.f, minutes = 30.f;
    int cars = 120, people = 280;
    bool legacyNav = false, smooth = true, turnLanes = false, legacyCrowd = false;
    float hour = 12.f;
    bool carsExplicit = false, peopleExplicit = false;
    float flyX = 0.f, flyZ = 0.f, flySpeed = 0.f;  // --fly x z м/с: фокус (камера) движется к точке; видимые появления и исчезновения машин считаются отдельно
    float connectorRadius = 0.f;  // --connector-radius R: половина длины дуги, м (0 — по умолчанию движка)
    unsigned seed = 1;
};

struct CarMetrics {
    long steps = 0, inBuilding = 0, inWater = 0, outsideOwnRoad = 0, jumps = 0, sharpTurns = 0, overlaps = 0, uTurns = 0;
    float maxJump = 0.f, maxOutside = 0.f, maxTurnDegrees = 0.f;
    std::uint64_t spawned = 0, despawned = 0, stuckResets = 0;
    float meanSpeed = 0.f;
};

// Поза корпуса на реальном рельефе и мостах: прежняя горизонтальная поза против шага и крена по колёсам.
// Видимость появлений и исчезновений машин при движущейся камере (область видимости: до 300 м, в конусе ±62° по ходу, либо ближе 25 м).
struct PopMetrics {
    long spawned = 0, removed = 0, popInVisible = 0, popOutVisible = 0, popInUnder100 = 0, popOutUnder150 = 0;
    long activeSum = 0, activeSteps = 0, near150Sum = 0, near400Sum = 0;
    int minActive = 1 << 30, maxActive = 0;
};

struct PoseMetrics {
    long steps = 0, legacySinkSteps = 0, legacyHoverSteps = 0, tiltedSteps = 0, newSinkSteps = 0, bridgeSteps = 0;
    float legacySinkMax = 0.f, legacyHoverMax = 0.f, newSinkMax = 0.f, newHoverMax = 0.f, maxPitchDeg = 0.f, maxRollDeg = 0.f, rawStepMax = 0.f, filteredStepMax = 0.f;
};

struct PeopleMetrics {
    long steps = 0, inBuilding = 0, inWater = 0, onRoadwayOutsideCrossing = 0, jumps = 0, nearJumps = 0, outsideNav = 0, teleports = 0, visibleTeleports = 0;
    float maxJump = 0.f;
    std::size_t agents = 0, spawnedBlocked = 0, stuck = 0, maxStuckSeconds = 0;
    int pathFailures = 0, pathQueries = 0;
    long crossingSteps = 0;
    int rebuilds = 0;
    std::size_t recycled = 0;
    double maxRebuildMs = 0.0, within180Mean = 0.0;
    long within180Min = 0;
};
}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const auto has = [&](const char* name, int n) { return !std::strcmp(argv[i], name) && i + n < argc; };
        if (has("--osm", 1)) a.osm = argv[++i];
        else if (has("--water", 1)) a.water = argv[++i];
        else if (has("--out", 1)) a.out = argv[++i];
        else if (has("--fly", 3)) { a.flyX = static_cast<float>(std::atof(argv[i + 1])); a.flyZ = static_cast<float>(std::atof(argv[i + 2])); a.flySpeed = static_cast<float>(std::atof(argv[i + 3])); i += 3; }
        else if (has("--profile", 1)) a.profile = argv[++i];  // data/city/life_profile.json: политика трафика и толпы из данных (без него — прежнее поведение)
        else if (has("--hour", 1)) a.hour = static_cast<float>(std::atof(argv[++i]));
        else if (has("--terrain", 1)) a.terrain = argv[++i];  // карта высот .hht: включает метрики позы корпуса (этап 05)
        else if (has("--focus", 2)) { a.focusX = static_cast<float>(std::atof(argv[i + 1])); a.focusZ = static_cast<float>(std::atof(argv[i + 2])); i += 2; }
        else if (has("--minutes", 1)) a.minutes = static_cast<float>(std::atof(argv[++i]));
        else if (has("--cars", 1)) { a.cars = std::atoi(argv[++i]); a.carsExplicit = true; }
        else if (has("--people", 1)) { a.people = std::atoi(argv[++i]); a.peopleExplicit = true; }
        else if (has("--seed", 1)) a.seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (has("--connector-radius", 1)) a.connectorRadius = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--legacy-nav")) a.legacyNav = true;
        else if (!std::strcmp(argv[i], "--no-smooth")) a.smooth = false;
        else if (!std::strcmp(argv[i], "--legacy-crowd")) a.legacyCrowd = true;  // прежняя пересборка толпы: пересоздание у камеры
        else if (!std::strcmp(argv[i], "--turn-lanes")) a.turnLanes = true;
    }
    std::string error;
    world::OsmData osm;
    if (!world::loadOsmFile(a.osm, osm, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    world::CityStyle style;
    if (!world::loadCityStyle("data/city/streetscape.json", style, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    ai::LifeProfile lifeProfile;
    const bool useProfile = !a.profile.empty();
    if (useProfile && !ai::loadLifeProfile(a.profile, lifeProfile, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
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
    PopMetrics pop;
    PoseMetrics pose;
    {
        ai::RoadGraphSettings gs;
        world::Heightmap heights;
        world::RoadProfile profile;
        const bool withPose = !a.terrain.empty() && heights.load(a.terrain, error);
        if (!a.terrain.empty() && !withPose) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        if (withPose) {
            world::RoadProfileSettings rps;
            if (!world::loadRoadProfileSettings("data/city/road_profile.json", rps, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
            profile = world::computeRoadProfile(osm, projection, &heights, rps);
            gs.profile = &profile;
        }
        const auto graph = ai::RoadGraph::build(osm, projection, gs);
        ai::TrafficSettings ts;
        if (useProfile) {
            ts = lifeProfile.traffic;
            if (a.carsExplicit) ts.maxCars = a.cars;
        } else {
            ts.maxCars = a.cars;
            ts.spawnRadius = 350.f;
            ts.despawnRadius = 550.f;
        }
        ts.seed = a.seed;
        ts.smoothTurns = a.smooth;
        ts.turnLanes = a.turnLanes;
        ts.connectorRadius = a.connectorRadius;
        ai::TrafficSim sim(graph, ts);
        ai::Vec2 focus{a.focusX, a.focusZ}, viewDir{0.f, 0.f};
        sim.setFocus(focus);
        std::unordered_map<std::uint32_t, ai::Vec2> previousPositions;
        std::unordered_map<std::uint32_t, ai::Car> last;
        std::unordered_map<std::uint32_t, render::VehiclePoseFilter> filters;
        std::unordered_map<std::uint32_t, render::VehiclePose> lastRaw, lastSmooth;
        const float baseLength = style.carLength * 0.6f, trackWidth = style.carWidth * 0.85f;
        double speedSum = 0;
        long speedCount = 0;
        for (long step = 0; step < steps; ++step) {
            if (a.flySpeed > 0.f) {  // камера летит к цели с постоянной скоростью
                const float dx = a.flyX - focus.x, dz = a.flyZ - focus.z, left = std::hypot(dx, dz);
                if (left > 0.5f) {
                    const float move = std::min(left, a.flySpeed * dt);
                    viewDir = {dx / left, dz / left};
                    focus = {focus.x + viewDir.x * move, focus.z + viewDir.z * move};
                    sim.setFocus(focus);
                    sim.setView(viewDir);
                }
            }
            if (useProfile) sim.setDensityScale(lifeProfile.trafficDensityAt(a.hour + static_cast<float>(step) * dt / 3600.f));
            sim.update(dt);
            {
                // Появления и исчезновения за шаг: сравнение множеств идентификаторов с прошлым шагом.
                std::unordered_map<std::uint32_t, ai::Vec2> currentPositions;
                const auto visible = [&](ai::Vec2 p) {
                    const float dx = p.x - focus.x, dz = p.z - focus.z, d = std::hypot(dx, dz);
                    if (d < 25.f) return true;
                    if (d > 300.f || (viewDir.x == 0.f && viewDir.z == 0.f)) return d <= 300.f;
                    return (dx * viewDir.x + dz * viewDir.z) / d > 0.4695f;  // cos 62°
                };
                for (const ai::Car& car : sim.cars()) {
                    currentPositions[car.id] = car.pos;
                    if (previousPositions.count(car.id) == 0) {
                        ++pop.spawned;
                        if (visible(car.pos)) {
                            ++pop.popInVisible;
                            if (std::hypot(car.pos.x - focus.x, car.pos.z - focus.z) < 100.f) ++pop.popInUnder100;
                        }
                    }
                }
                for (const auto& [id, position] : previousPositions) {
                    if (currentPositions.count(id) != 0) continue;
                    ++pop.removed;
                    if (visible(position)) {
                        ++pop.popOutVisible;
                        if (std::hypot(position.x - focus.x, position.z - focus.z) < 150.f) ++pop.popOutUnder150;
                    }
                }
                previousPositions.swap(currentPositions);
                const int active = static_cast<int>(sim.cars().size());
                pop.activeSum += active;
                ++pop.activeSteps;
                pop.minActive = std::min(pop.minActive, active);
                pop.maxActive = std::max(pop.maxActive, active);
                for (const ai::Car& car : sim.cars()) {
                    const float d = std::hypot(car.pos.x - focus.x, car.pos.z - focus.z);
                    pop.near150Sum += d < 150.f;
                    pop.near400Sum += d < 400.f;
                }
            }
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
                if (withPose) {
                    ++pose.steps;
                    const bool deck = std::isfinite(car.surfaceYFront) && std::isfinite(car.surfaceYRear);
                    const float fx = std::sin(car.heading), fz = -std::cos(car.heading), rx = std::cos(car.heading), rz = std::sin(car.heading);
                    const float hb = baseLength * 0.5f, ht = trackWidth * 0.5f;
                    const auto at = [&](float along, float across) { return heights.heightAt(car.pos.x + fx * along + rx * across, car.pos.z + fz * along + rz * across); };
                    render::WheelHeights w;
                    if (deck) {
                        ++pose.bridgeSteps;
                        w = {car.surfaceYFront, car.surfaceYFront, car.surfaceYRear, car.surfaceYRear};
                    } else {
                        w = {at(hb, -ht), at(hb, ht), at(-hb, -ht), at(-hb, ht)};
                    }
                    const float centre = deck ? 0.5f * (car.surfaceYFront + car.surfaceYRear) : heights.heightAt(car.pos.x, car.pos.z);
                    const float wheelGround[4] = {w.frontLeft, w.frontRight, w.rearLeft, w.rearRight};
                    for (int k = 0; k < 4; ++k) {  // прежняя поза: корпус горизонтален на высоте дороги под центром
                        const float sink = wheelGround[k] - centre, hover = centre - wheelGround[k];
                        pose.legacySinkMax = std::max(pose.legacySinkMax, sink);
                        pose.legacyHoverMax = std::max(pose.legacyHoverMax, hover);
                    }
                    const float legacyWorst = std::max({w.frontLeft, w.frontRight, w.rearLeft, w.rearRight}) - centre;
                    const float legacyHover = centre - std::min({w.frontLeft, w.frontRight, w.rearLeft, w.rearRight});
                    if (legacyWorst > 0.05f) ++pose.legacySinkSteps;
                    if (legacyHover > 0.05f) ++pose.legacyHoverSteps;
                    const render::VehiclePose raw = render::solveVehiclePose(w, baseLength, trackWidth);
                    const render::VehiclePose smooth = filters[car.id].update(raw, dt);
                    const core::Mat4 m = render::vehicleTransform(0.f, 0.f, 0.f, raw);
                    const float lx[4] = {-ht, ht, -ht, ht}, lz[4] = {-hb, -hb, hb, hb};
                    for (int k = 0; k < 4; ++k) {
                        const float y = m.m[1] * lx[k] + m.m[9] * lz[k] + m.m[13];
                        pose.newSinkMax = std::max(pose.newSinkMax, wheelGround[k] - y);
                        pose.newHoverMax = std::max(pose.newHoverMax, y - wheelGround[k]);
                    }
                    if (wheelGround[0] - (m.m[1] * lx[0] + m.m[9] * lz[0] + m.m[13]) > 0.01f) ++pose.newSinkSteps;
                    const float pitchDeg = std::fabs(raw.pitch) * 57.29578f, rollDeg = std::fabs(raw.roll) * 57.29578f;
                    pose.maxPitchDeg = std::max(pose.maxPitchDeg, pitchDeg);
                    pose.maxRollDeg = std::max(pose.maxRollDeg, rollDeg);
                    if (pitchDeg > 3.f) ++pose.tiltedSteps;
                    const auto prevRaw = lastRaw.find(car.id);
                    if (prevRaw != lastRaw.end()) {
                        pose.rawStepMax = std::max(pose.rawStepMax, std::fabs(raw.pitch - prevRaw->second.pitch));
                        pose.filteredStepMax = std::max(pose.filteredStepMax, std::fabs(smooth.pitch - lastSmooth[car.id].pitch));
                    }
                    lastRaw[car.id] = raw;
                    lastSmooth[car.id] = smooth;
                }
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
        // Толпа — та же библиотека, что в демо (CityCrowd): сетка вокруг камеры, пересборка при смещении, переселение вне взгляда.
        world::WalkLayout crowdLayout = layout;
        std::vector<std::vector<world::Vec2>> crowdBuildings = layout.buildings;
        if (a.legacyNav) {  // прежняя сетка: только здания, без воды, проезжей части и переходов
            crowdLayout.water.clear();
            crowdLayout.carriageways.clear();
            crowdLayout.crossings.clear();
        }
        crowdLayout.buildings.clear();
        ai::ArchetypeLibrary library;
        if (!library.loadDirectory("data/people/archetypes", error) || library.size() == 0) { std::fprintf(stderr, "архетипы: %s\n", error.c_str()); return 1; }
        ai::CityCrowdSettings cs;
        cs.navRadius = style.crowdNavRadius;
        cs.spawnRadius = style.crowdRadius;
        cs.navCell = style.crowdNavCell;
        cs.pathExpansions = static_cast<std::size_t>(style.crowdPathExpansions);
        cs.pathQueriesPerFrame = style.crowdPathQueries > 0 ? static_cast<int>(style.crowdPathQueries) : 4;
        cs.agentCount = static_cast<std::size_t>(a.people);
        if (useProfile) {  // радиусы, число прохожих и конвейер — из профиля; шаг сетки и поиск пути — из оформления города
            const auto& pc = lifeProfile.crowd;
            cs.navRadius = pc.navRadius;
            cs.spawnRadius = pc.spawnRadius;
            cs.recenterFraction = pc.recenterFraction;
            cs.minRecycleDistance = pc.minRecycleDistance;
            cs.farVisible = pc.farVisible;
            cs.viewHalfAngleDegrees = pc.viewHalfAngleDegrees;
            cs.treadmillInterval = pc.treadmillInterval;
            cs.treadmillBatch = pc.treadmillBatch;
            cs.treadmillMinDistance = pc.treadmillMinDistance;
            if (!a.peopleExplicit) cs.agentCount = pc.agentCount;
        }
        cs.seed = 12345u + a.seed;
        cs.keepAgents = !a.legacyCrowd;
        ai::CityCrowd city(crowdBuildings, crowdLayout, library.pointers(), cs);
        ai::Vec2 focus{a.focusX, a.focusZ}, viewDir{0.f, 0.f};
        city.update(0.f, focus, viewDir, 12.f);
        if (!city.crowd() || city.crowd()->agents().empty()) { std::fprintf(stderr, "рядом с точкой %.0f %.0f нет зданий для спавна толпы\n", static_cast<double>(a.focusX), static_cast<double>(a.focusZ)); return 1; }
        pm.agents = city.crowd()->agents().size();
        for (const auto& ag : city.crowd()->agents())
            if (!city.grid().walkable(ag.pos.x, ag.pos.z)) ++pm.spawnedBlocked;
        std::vector<ai::Vec2> lastPos(pm.agents);
        std::vector<float> stillFor(pm.agents, 0.f);
        std::vector<char> wasStuck(pm.agents, 0);
        for (std::size_t i = 0; i < pm.agents; ++i) lastPos[i] = city.crowd()->agents()[i].pos;
        const auto visible = [&](ai::Vec2 p) {
            const float dx = p.x - focus.x, dz = p.z - focus.z, d = std::hypot(dx, dz);
            if (d < 25.f) return true;
            if (d > 300.f) return false;
            if (viewDir.x == 0.f && viewDir.z == 0.f) return true;
            return (dx * viewDir.x + dz * viewDir.z) / d > 0.4695f;
        };
        long densitySum = 0, densityMin = 1 << 30;
        int rebuildsSeen = 0;
        for (long step = 0; step < steps; ++step) {
            if (a.flySpeed > 0.f) {
                const float dx = a.flyX - focus.x, dz = a.flyZ - focus.z, left = std::hypot(dx, dz);
                if (left > 0.5f) {
                    const float move = std::min(left, a.flySpeed * dt);
                    viewDir = {dx / left, dz / left};
                    focus = {focus.x + viewDir.x * move, focus.z + viewDir.z * move};
                }
            }
            city.update(dt, focus, viewDir, 12.f + static_cast<float>(step) * dt / 3600.f);
            if (city.stats().rebuilds != rebuildsSeen) {
                rebuildsSeen = city.stats().rebuilds;
                pm.maxRebuildMs = std::max(pm.maxRebuildMs, city.stats().lastRebuildMs);
            }
            const auto& agents = city.crowd()->agents();
            if (agents.size() != pm.agents) { std::fprintf(stderr, "число агентов изменилось: %zu -> %zu\n", pm.agents, agents.size()); return 1; }
            int within180 = 0;
            for (std::size_t i = 0; i < agents.size(); ++i) {
                const ai::Agent& ag = agents[i];
                ++pm.steps;
                if (std::hypot(ag.pos.x - focus.x, ag.pos.z - focus.z) < style.crowdDrawRadius) ++within180;
                if (buildings.depth(ag.pos.x, ag.pos.z) > kDepth) ++pm.inBuilding;
                if (water.depth(ag.pos.x, ag.pos.z) > kDepth) ++pm.inWater;
                const bool onRoad = roadway.depth(ag.pos.x, ag.pos.z) > kDepth;
                if (onRoad && crossings.contains(ag.pos.x, ag.pos.z)) ++pm.crossingSteps;
                else if (onRoad) ++pm.onRoadwayOutsideCrossing;
                const float minX = city.grid().minX(), minZ = city.grid().minZ(), maxX = city.grid().maxX(), maxZ = city.grid().maxZ();
                if (ag.pos.x < minX || ag.pos.x > maxX || ag.pos.z < minZ || ag.pos.z > maxZ) ++pm.outsideNav;
                const float jump = std::hypot(ag.pos.x - lastPos[i].x, ag.pos.z - lastPos[i].z);
                pm.maxJump = std::max(pm.maxJump, jump);
                if (jump > 25.f) {  // телепорт: переселение при пересборке толпы; видимым считается, если до или после точка в поле зрения
                    ++pm.teleports;
                    if (visible(ag.pos) || visible(lastPos[i])) ++pm.visibleTeleports;
                } else if (jump > 1.5f) {
                    ++pm.jumps;
                    if (ag.lod == ai::LodTier::Near || ag.lod == ai::LodTier::Mid) ++pm.nearJumps;  // дальние агенты обновляются реже по замыслу LOD
                }
                const bool moving = ag.state == ai::AgentState::Walking || ag.state == ai::AgentState::Fleeing;
                stillFor[i] = (moving && jump < 0.005f) ? stillFor[i] + dt : 0.f;
                if (stillFor[i] > 120.f && !wasStuck[i]) { wasStuck[i] = 1; ++pm.stuck; }
                pm.maxStuckSeconds = std::max(pm.maxStuckSeconds, static_cast<std::size_t>(stillFor[i]));
                lastPos[i] = ag.pos;
            }
            densitySum += within180;
            densityMin = std::min<long>(densityMin, within180);
        }
        pm.pathFailures = city.crowd()->stats().pathFailures;
        pm.pathQueries = city.crowd()->stats().pathQueries;
        pm.rebuilds = city.stats().rebuilds;
        pm.recycled = city.stats().recycled;
        pm.within180Mean = steps > 0 ? static_cast<double>(densitySum) / static_cast<double>(steps) : 0.0;
        pm.within180Min = densityMin == (1 << 30) ? 0 : densityMin;
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
    std::string text = buffer;
    {
        char popBuffer[1024];
        const double steps2 = pop.activeSteps > 0 ? static_cast<double>(pop.activeSteps) : 1.0;
        std::snprintf(popBuffer, sizeof(popBuffer),
            "{\"traffic_flow\": {\"fly_m_per_s\": %.1f, \"spawned\": %ld, \"removed\": %ld, \"visible_pop_in\": %ld, \"visible_pop_in_under_100m\": %ld, \"visible_pop_out\": %ld, \"visible_pop_out_under_150m\": %ld,\n"
            "  \"active_cars_mean\": %.1f, \"active_min\": %d, \"active_max\": %d, \"cars_within_150m_mean\": %.1f, \"cars_within_400m_mean\": %.1f}}\n",
            static_cast<double>(a.flySpeed), pop.spawned, pop.removed, pop.popInVisible, pop.popInUnder100, pop.popOutVisible, pop.popOutUnder150, static_cast<double>(pop.activeSum) / steps2,
            pop.minActive == (1 << 30) ? 0 : pop.minActive, pop.maxActive, static_cast<double>(pop.near150Sum) / steps2, static_cast<double>(pop.near400Sum) / steps2);
        text += popBuffer;
    }
    if (!a.terrain.empty()) {
        char poseBuffer[2048];
        std::snprintf(poseBuffer, sizeof(poseBuffer),
            "{\"pose\": {\"car_steps\": %ld, \"on_bridge_steps\": %ld, \"tilted_over_3deg_steps\": %ld, \"max_pitch_deg\": %.1f, \"max_roll_deg\": %.1f,\n"
            "  \"legacy_flat_body\": {\"steps_wheel_inside_road_over_5cm\": %ld, \"steps_wheel_floating_over_5cm\": %ld, \"max_wheel_inside_road_m\": %.2f, \"max_wheel_floating_m\": %.2f},\n"
            "  \"new_pose\": {\"steps_wheel_inside_road_over_1cm\": %ld, \"max_wheel_inside_road_m\": %.3f, \"max_wheel_floating_m\": %.2f},\n"
            "  \"pitch_step_max_rad_per_0_1s\": {\"raw\": %.3f, \"filtered\": %.3f}}}\n",
            pose.steps, pose.bridgeSteps, pose.tiltedSteps, static_cast<double>(pose.maxPitchDeg), static_cast<double>(pose.maxRollDeg), pose.legacySinkSteps, pose.legacyHoverSteps,
            static_cast<double>(pose.legacySinkMax), static_cast<double>(pose.legacyHoverMax), pose.newSinkSteps, static_cast<double>(pose.newSinkMax), static_cast<double>(pose.newHoverMax),
            static_cast<double>(pose.rawStepMax), static_cast<double>(pose.filteredStepMax));
        text += poseBuffer;
    }
    {
        char crowdBuffer[768];
        std::snprintf(crowdBuffer, sizeof(crowdBuffer),
            "{\"crowd_flow\": {\"rebuilds\": %d, \"recycled_agents\": %zu, \"teleports_over_25m\": %ld, \"visible_teleports\": %ld, \"max_rebuild_ms\": %.1f, \"agents_within_draw_radius_mean\": %.1f, \"agents_within_draw_radius_min\": %ld}}\n",
            pm.rebuilds, pm.recycled, pm.teleports, pm.visibleTeleports, pm.maxRebuildMs, pm.within180Mean, pm.within180Min);
        text += crowdBuffer;
    }
    std::fputs(text.c_str(), stdout);
    if (!a.out.empty()) std::ofstream(a.out, std::ios::binary) << text;
    return 0;
}
