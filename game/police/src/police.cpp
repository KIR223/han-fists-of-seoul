#include "han/police/police.h"

#include <algorithm>
#include <cmath>

namespace han::police {
namespace {
float dist(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
}  // namespace

const char* moduleName() { return "police"; }

CrimeRule ruleOf(Crime c) {
    switch (c) {
        case Crime::Assault: return {12.f, 25.f, true};
        case Crime::AssaultOfficer: return {40.f, 60.f, false};  // полицейский сам свидетель
        case Crime::Vandalism: return {6.f, 20.f, true};
        case Crime::Theft: return {10.f, 20.f, true};
        case Crime::CarTheft: return {22.f, 30.f, true};
        case Crime::HitAndRun: return {30.f, 40.f, true};
        case Crime::Speeding: return {4.f, 30.f, true};
        case Crime::Murder: return {80.f, 50.f, true};
        case Crime::Trespass: return {15.f, 25.f, true};
    }
    return {};
}

PoliceSystem::PoliceSystem(const ai::NavGrid* nav, const ai::RoadGraph* roads, const Settings& settings, std::uint32_t seed)
    : nav_(nav), roads_(roads), cfg_(settings), rng_(seed ? seed : 1u) {}

int PoliceSystem::levelForHeat(float heat) const {
    int level = 0;
    for (int i = 1; i <= 5; ++i)
        if (heat >= cfg_.levelHeat[i]) level = i;
    return level;
}

bool PoliceSystem::sees(Vec2 from, Vec2 to, float range) const {
    if (dist(from, to) > range) return false;
    return !nav_ || !nav_->valid() || nav_->lineOfSight(from, to);
}

bool PoliceSystem::cameraSees(const Camera& c, Vec2 target) const {
    const float d = dist(c.pos, target);
    if (d > c.range) return false;
    if (d > 0.5f) {
        const float angle = std::atan2(target.x - c.pos.x, -(target.z - c.pos.z));
        float diff = std::fabs(angle - c.yaw);
        if (diff > 3.14159265f) diff = 6.2831853f - diff;
        if (diff > c.fovDeg * 0.5f * 0.01745329f) return false;
    }
    return !nav_ || !nav_->valid() || nav_->lineOfSight(c.pos, target);
}

bool PoliceSystem::addCrime(Crime crime, Vec2 where, bool witnessed) {
    if (busted_) return false;
    const CrimeRule rule = ruleOf(crime);
    bool seenByPolice = false;
    for (const Unit& u : units_)
        if (u.mode != UnitMode::Leave && sees(u.pos, where, rule.witnessRadius)) seenByPolice = true;
    for (const Camera& c : cameras_)
        if (cameraSees(c, where)) seenByPolice = true;
    if (rule.needsWitness && !witnessed && !seenByPolice) return false;  // никто не видел — не засчитано
    const int before = level_;
    heat_ += rule.heat;
    level_ = levelForHeat(heat_);
    lastKnown_ = where;  // вызов полиции или камера: известно, где это случилось
    unseen_ = 0.f;
    sinceEscape_ = 1e9f;
    searchRadius_ = cfg_.searchRadiusBase + 25.f * static_cast<float>(std::max(0, level_ - 1));
    push(EventType::CrimeCounted, level_, where, crime);
    if (level_ != before) push(EventType::LevelChanged, level_, where);
    return true;
}

std::vector<Event> PoliceSystem::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

void PoliceSystem::clear() {
    units_.clear();
    events_.clear();
    heat_ = 0.f;
    level_ = 0;
    seen_ = busted_ = false;
    unseen_ = 0.f;
    searchRadius_ = 0.f;
}

void PoliceSystem::spawnUnit(UnitKind kind, Vec2 player) {
    if (stations_.empty() || (kind == UnitKind::Car && (!roads_ || roads_->edges().empty()))) return;
    // Подкрепление выезжает с ближайшей подходящей станции: не ближе minSpawnDistance и не на глазах у героя.
    std::vector<std::size_t> order(stations_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return dist(stations_[a], player) < dist(stations_[b], player); });
    for (std::size_t idx : order) {
        const Vec2 station = stations_[idx];
        if (dist(station, player) < cfg_.minSpawnDistance) continue;
        if (nav_ && nav_->valid() && nav_->lineOfSight(station, player) && dist(station, player) < cfg_.minSpawnDistance * 1.4f) continue;  // подкрепление приходит не на глазах
        Unit u;
        u.id = nextId_++;
        u.kind = kind;
        u.mode = UnitMode::Respond;
        u.home = station;
        u.pos = station;
        if (kind == UnitKind::Car) {
            const int node = roads_->nearestNode(station);
            if (node >= 0) u.pos = roads_->nodes()[static_cast<std::size_t>(node)].pos;
        }
        u.repath = 0.f;
        units_.push_back(u);
        push(EventType::UnitSpawned, level_, u.pos);
        return;
    }
}

void PoliceSystem::dispatch(Vec2 player) {
    const int wantFoot = level_ >= 1 ? 1 + level_ : 0;
    const int wantCars = (level_ >= 2 && roads_) ? level_ - 1 : 0;
    int foot = 0, cars = 0;
    for (const Unit& u : units_) {
        if (u.mode == UnitMode::Leave) continue;
        if (u.kind == UnitKind::Foot) ++foot;
        else if (!u.deployed) ++cars;  // высадившая офицера машина уже «потрачена»: офицер считается пешим
    }
    if (foot < wantFoot) spawnUnit(UnitKind::Foot, player);
    if (cars < wantCars) spawnUnit(UnitKind::Car, player);
}

void PoliceSystem::planFoot(Unit& u, Vec2 target) {
    u.path.clear();
    u.pathIndex = 0;
    if (nav_ && nav_->valid()) {
        if (!nav_->findPath(u.pos, target, u.path)) u.path = {u.pos, target};
    } else {
        u.path = {u.pos, target};
    }
    u.pathIndex = u.path.size() > 1 ? 1 : 0;
}

void PoliceSystem::planCar(Unit& u, Vec2 target) {
    u.path.clear();
    u.pathIndex = 0;
    if (!roads_) return;
    const int from = roads_->nearestNode(u.pos), to = roads_->nearestNode(target);
    const auto edges = roads_->shortestPath(from, to);
    u.path.push_back(u.pos);
    for (int ei : edges) {
        const ai::RoadEdge& e = roads_->edges()[static_cast<std::size_t>(ei)];
        for (float s = 0.f; s < e.length; s += 12.f) u.path.push_back(e.pointAt(s, 1.6f));
        u.path.push_back(e.pointAt(e.length, 1.6f));
    }
    u.pathIndex = u.path.size() > 1 ? 1 : 0;
}

void PoliceSystem::moveAlong(Unit& u, float speed, float dt) {
    float budget = speed * dt;
    while (budget > 0.f && u.pathIndex < u.path.size()) {
        const Vec2 target = u.path[u.pathIndex];
        const float d = dist(u.pos, target);
        if (d <= budget) {
            u.pos = target;
            budget -= d;
            ++u.pathIndex;
        } else {
            u.heading = std::atan2(target.x - u.pos.x, -(target.z - u.pos.z));
            u.pos = {u.pos.x + (target.x - u.pos.x) / d * budget, u.pos.z + (target.z - u.pos.z) / d * budget};
            budget = 0.f;
        }
    }
}

void PoliceSystem::updateUnit(Unit& u, float dt, Vec2 player) {
    u.seesTarget = u.mode != UnitMode::Leave && sees(u.pos, player, cfg_.officerSight);
    u.ttl += dt;
    if (u.mode == UnitMode::Leave) {
        if (u.path.empty() || u.repath <= 0.f) {
            if (u.kind == UnitKind::Foot) planFoot(u, u.home);
            else planCar(u, u.home);
            u.repath = 5.f;
        }
        u.repath -= dt;
        moveAlong(u, u.kind == UnitKind::Car ? cfg_.carSpeed : cfg_.footSpeed * 0.6f, dt);
        return;
    }
    if (u.seesTarget) u.mode = UnitMode::Chase;
    else if (u.mode == UnitMode::Chase && !seen_ && dist(u.pos, lastKnown_) < 4.f) u.mode = UnitMode::Search;
    else if (u.mode == UnitMode::Respond && dist(u.pos, lastKnown_) < 6.f) u.mode = UnitMode::Search;

    const Vec2 target = (u.mode == UnitMode::Chase && seen_) ? player : lastKnown_;
    if (u.kind == UnitKind::Car) {
        // Машина едет к цели по дорогам; рядом высаживает офицера и остаётся стоять.
        if (!u.deployed && dist(u.pos, target) < 35.f) {
            u.deployed = true;
            Unit officer;
            officer.id = nextId_++;
            officer.kind = UnitKind::Foot;
            officer.mode = UnitMode::Chase;
            officer.home = u.home;
            officer.pos = u.pos;
            units_.push_back(officer);
            push(EventType::UnitSpawned, level_, officer.pos);
            return;
        }
        if (u.deployed) return;
        u.repath -= dt;
        if (u.path.empty() || u.pathIndex >= u.path.size() || u.repath <= 0.f) {
            planCar(u, target);
            u.repath = cfg_.repathSeconds * 2.f;
        }
        moveAlong(u, cfg_.carSpeed, dt);
        return;
    }
    // Пеший офицер: бег к цели; в поиске ходит по случайным точкам зоны.
    u.repath -= dt;
    if (u.mode == UnitMode::Search) {
        if (u.pathIndex >= u.path.size()) {
            u.wait -= dt;
            if (u.wait <= 0.f) {
                std::uniform_real_distribution<float> a(0.f, 6.2831853f), r(0.f, 1.f);
                const float ang = a(rng_), rad = std::sqrt(r(rng_)) * std::max(10.f, searchRadius_);
                planFoot(u, {lastKnown_.x + std::cos(ang) * rad, lastKnown_.z + std::sin(ang) * rad});
                u.wait = 1.5f;
            }
        }
        moveAlong(u, cfg_.footSpeed * 0.65f, dt);
        return;
    }
    if (u.path.empty() || u.pathIndex >= u.path.size() || u.repath <= 0.f) {
        planFoot(u, target);
        std::uniform_real_distribution<float> j(0.8f, 1.2f);
        u.repath = cfg_.repathSeconds * j(rng_);
    }
    moveAlong(u, cfg_.footSpeed, dt);
}

void PoliceSystem::update(float dt, Vec2 playerPos) {
    if (busted_) return;
    sinceEscape_ += dt;

    if (level_ > 0) {
        bool seen = false;
        for (const Unit& u : units_)
            if (u.mode != UnitMode::Leave && sees(u.pos, playerPos, cfg_.officerSight)) seen = true;
        for (const Camera& c : cameras_)
            if (cameraSees(c, playerPos)) seen = true;
        if (seen && !seen_) push(EventType::Spotted, level_, playerPos);
        if (!seen && seen_) push(EventType::LostPlayer, level_, lastKnown_);
        seen_ = seen;
        if (seen_) {
            lastKnown_ = playerPos;
            unseen_ = 0.f;
        } else {
            unseen_ += dt;
        }
        // Зона поиска: вокруг последнего известного места, сужается, пока героя не видно.
        const float base = cfg_.searchRadiusBase + 25.f * static_cast<float>(std::max(0, level_ - 1));
        searchRadius_ = std::max(15.f, base - cfg_.searchShrinkPerSecond * unseen_);
        // Остывание: после «льготного» времени без контакта; вне зоны поиска — быстрее.
        bool officerNear = false;  // пока рядом ходит патруль, оторваться нельзя
        for (const Unit& u : units_)
            if (u.mode != UnitMode::Leave && dist(u.pos, playerPos) < cfg_.officerSight * 1.5f) officerNear = true;
        if (!seen_ && !officerNear && unseen_ > cfg_.graceSeconds + cfg_.graceSecondsPerStar * static_cast<float>(level_)) {
            const bool outside = dist(playerPos, lastKnown_) > searchRadius_;
            heat_ -= cfg_.decayPerSecond * (outside ? cfg_.decayOutsideZoneBoost : cfg_.decayInsideZoneFactor) * dt;
        }
        const int newLevel = levelForHeat(std::max(0.f, heat_));
        if (heat_ <= 0.f || newLevel == 0) {  // упал ниже порога первой звезды — розыск снят
            heat_ = 0.f;
            level_ = 0;
            seen_ = false;
            push(EventType::LevelChanged, 0, playerPos);
            push(EventType::Escaped, 0, playerPos);
            sinceEscape_ = 0.f;
            for (Unit& u : units_) {
                u.mode = UnitMode::Leave;
                u.path.clear();
                u.ttl = 0.f;
            }
        } else if (newLevel != level_) {
            level_ = newLevel;
            push(EventType::LevelChanged, level_, playerPos);
        }

        dispatchTimer_ -= dt;
        if (dispatchTimer_ <= 0.f && level_ > 0) {
            dispatch(playerPos);
            dispatchTimer_ = 1.5f;
        }
    }

    for (std::size_t i = 0; i < units_.size(); ++i) updateUnit(units_[i], dt, playerPos);  // units_ может расти (высадка), индекс безопасен

    if (level_ > 0) {
        for (const Unit& u : units_)
            if (u.kind == UnitKind::Foot && u.mode != UnitMode::Leave && dist(u.pos, playerPos) < cfg_.arrestRadius) {
                busted_ = true;
                push(EventType::Busted, level_, playerPos);
                break;
            }
    }
    // Уходящие патрули пропадают, дойдя до станции или спустя leaveSeconds.
    units_.erase(std::remove_if(units_.begin(), units_.end(),
                                [&](const Unit& u) { return u.mode == UnitMode::Leave && (dist(u.pos, u.home) < 3.f || u.ttl > cfg_.leaveSeconds); }),
                 units_.end());
}

}  // namespace han::police
