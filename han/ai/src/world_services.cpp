#include "han/ai/world_services.h"

#include <cmath>

namespace han::ai {
namespace {
bool insidePolygon(const std::vector<Vec2>& poly, float x, float z) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if ((poly[i].z > z) != (poly[j].z > z) && x < (poly[j].x - poly[i].x) * (z - poly[i].z) / (poly[j].z - poly[i].z) + poly[i].x) in = !in;
    }
    return in;
}

contracts::Season seasonOf(std::uint16_t dayOfYear) {
    // Календарь Кореи: весна с 1 марта (день 59), лето с 1 июня, осень с 1 сентября, зима с 1 декабря.
    const int d = dayOfYear % 365;
    if (d >= 59 && d < 151) return contracts::Season::Spring;
    if (d >= 151 && d < 243) return contracts::Season::Summer;
    if (d >= 243 && d < 334) return contracts::Season::Autumn;
    return contracts::Season::Winter;
}
}  // namespace

void WorldServices::setTime(float hour, std::uint16_t dayOfYear) {
    hour = std::fmod(std::fmod(hour, 24.f) + 24.f, 24.f);
    time_.dayOfYear = static_cast<std::uint16_t>(dayOfYear % 365);
    time_.weekday = static_cast<std::uint8_t>(time_.dayOfYear % 7);
    time_.season = seasonOf(time_.dayOfYear);
    time_.hour = hour;
    clockSeconds_ = static_cast<double>(hour) * 3600.0 + static_cast<double>(time_.dayOfYear) * 86400.0;
}

void WorldServices::advance(float realSeconds, float gameSecondsPerRealSecond) {
    clockSeconds_ += static_cast<double>(realSeconds) * static_cast<double>(gameSecondsPerRealSecond);
    const double days = std::floor(clockSeconds_ / 86400.0);
    time_.dayOfYear = static_cast<std::uint16_t>(static_cast<long long>(days) % 365);
    time_.weekday = static_cast<std::uint8_t>(static_cast<long long>(days) % 7);
    time_.hour = static_cast<float>((clockSeconds_ - days * 86400.0) / 3600.0);
    time_.season = seasonOf(time_.dayOfYear);
}

std::vector<SpawnRequest> WorldServices::takeSpawnRequests() {
    std::vector<SpawnRequest> out;
    out.swap(spawns_);
    return out;
}

contracts::DistrictId WorldServices::districtAt(contracts::Vec3 p) const {
    for (const District& d : districts_)
        if (d.polygon.size() >= 3 && insidePolygon(d.polygon, p.x, p.z)) return d.id;
    return 0;  // 0 — «вне районов»
}

bool WorldServices::isSacred(contracts::Vec3 p) const {
    for (const SacredZone& z : sacred_) {
        const float dx = p.x - z.center.x, dz = p.z - z.center.z;
        if (dx * dx + dz * dz <= z.radius * z.radius) return true;
    }
    return false;
}

contracts::EntityId WorldServices::spawnNpc(std::uint32_t archetype, contracts::Vec3 position) {
    SpawnRequest r;
    r.kind = SpawnRequest::Kind::Npc;
    r.id = nextEntity_++;
    r.model = archetype;
    r.at.position = position;
    spawns_.push_back(r);
    return r.id;
}

contracts::EntityId WorldServices::spawnVehicle(std::uint32_t model, contracts::Transform at) {
    SpawnRequest r;
    r.kind = SpawnRequest::Kind::Vehicle;
    r.id = nextEntity_++;
    r.model = model;
    r.at = at;
    spawns_.push_back(r);
    return r.id;
}

std::uint32_t WorldServices::findPath(contracts::Vec3 from, contracts::Vec3 to, contracts::Vec3* out, std::uint32_t maxPoints) const {
    if (!nav_ || !nav_->valid() || !out || maxPoints == 0) return 0;
    std::vector<Vec2> path;
    if (!nav_->findPath({from.x, from.z}, {to.x, to.z}, path) || path.empty()) return 0;
    const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::size_t>(path.size(), maxPoints));
    for (std::uint32_t i = 0; i < n; ++i) out[i] = {path[i].x, from.y, path[i].z};
    if (path.size() > maxPoints) out[n - 1] = {path.back().x, from.y, path.back().z};  // не помещается — последняя точка всё равно цель
    return n;
}

void WorldServices::addCrime(std::uint32_t crimeType, contracts::Vec3 where, contracts::EntityId) {
    if (isSacred(where)) crimeType = 9;  // в святом месте любое насилие считается нарушением святыни (Crime::Trespass)
    if (wanted_ && wanted_->crime(crimeType, {where.x, where.z})) ++crimes_;
}

}  // namespace han::ai
