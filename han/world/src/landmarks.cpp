#include "han/world/landmarks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "han/core/json.h"

namespace han::world {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr std::int64_t kFirstId = -9000000000000LL;

float number(const core::Json& j, const char* key, float fallback) {
    const auto& v = j[key];
    return v.isNumber() ? static_cast<float>(v.asNumber()) : fallback;
}

double metersPerDegLon(double lat) { return 111320.0 * std::cos(lat * kPi / 180.0); }

float tagNumber(std::string_view text) {
    if (text.empty()) return 0.f;
    const std::string s(text);
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end == s.c_str() ? 0.f : v;
}
}  // namespace

bool loadLandmarks(const std::string& file, std::vector<Landmark>& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    if (!root.isObject() || root["version"].asNumber() != 1 || !root["landmarks"].isArray()) {
        error = file + ": неизвестная версия или нет списка landmarks";
        return false;
    }
    std::vector<Landmark> result;
    const auto& list = root["landmarks"];
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& e = list.at(i);
        Landmark l;
        l.id = e["id"].asString();
        l.lat = e["lat"].asNumber();
        l.lon = e["lon"].asNumber();
        l.radiusMeters = number(e, "radius_m", 20.f);
        l.sides = static_cast<int>(number(e, "sides", 8.f));
        l.heightMeters = number(e, "height_m", 0.f);
        l.skipIfTallerWithin = number(e, "skip_if_taller_within_m", 150.f);
        l.source = e["source"].asString();
        if (l.id.empty() || !e["lat"].isNumber() || !e["lon"].isNumber() || !std::isfinite(l.lat) || !std::isfinite(l.lon) || std::fabs(l.lat) > 90 ||
            std::fabs(l.lon) > 180 || !(l.radiusMeters >= 2.f && l.radiusMeters <= 200.f) || l.sides < 3 || l.sides > 32 || !(l.heightMeters >= 20.f && l.heightMeters <= 1000.f) ||
            !(l.skipIfTallerWithin >= 0.f && l.skipIfTallerWithin <= 1000.f) || l.source.empty()) {
            error = file + ": некорректный ориентир №" + std::to_string(i);
            return false;
        }
        result.push_back(std::move(l));
    }
    out = std::move(result);
    return true;
}

LandmarkStats applyLandmarks(OsmData& data, const std::vector<Landmark>& landmarks) {
    LandmarkStats stats;
    std::int64_t nextNode = kFirstId, nextWay = kFirstId;
    for (const Landmark& l : landmarks) {
        const double mx = metersPerDegLon(l.lat), my = 110540.0;
        bool present = false;
        for (const OsmWay& way : data.ways) {
            if (way.tag("building").empty() || way.nodes.empty()) continue;
            const float h = tagNumber(way.tag("height")) > 0.f ? tagNumber(way.tag("height")) : tagNumber(way.tag("building:levels")) * 3.2f + 1.f;
            if (h < l.heightMeters * 0.8f) continue;
            double cx = 0, cy = 0;
            int n = 0;
            for (const auto id : way.nodes) {
                const auto it = data.nodes.find(id);
                if (it == data.nodes.end()) continue;
                cx += it->second.lat;
                cy += it->second.lon;
                ++n;
            }
            if (!n) continue;
            const double dy = (cx / n - l.lat) * my, dx = (cy / n - l.lon) * mx;
            if (std::hypot(dx, dy) <= l.skipIfTallerWithin) {
                present = true;
                break;
            }
        }
        if (present) {
            ++stats.skippedPresent;
            continue;
        }
        OsmWay way;
        way.id = nextWay--;
        for (int k = 0; k < l.sides; ++k) {
            const double a = 2.0 * kPi * k / l.sides;
            OsmNode node;
            node.lat = l.lat + std::sin(a) * l.radiusMeters / my;
            node.lon = l.lon + std::cos(a) * l.radiusMeters / mx;
            const std::int64_t id = nextNode--;
            data.nodes[id] = node;
            way.nodes.push_back(id);
        }
        way.nodes.push_back(way.nodes.front());
        char height[32];
        std::snprintf(height, sizeof(height), "%.1f", static_cast<double>(l.heightMeters));
        way.tags = {{"building", "yes"}, {"height", height}, {"han:landmark", l.id}};
        data.ways.push_back(std::move(way));
        ++stats.added;
    }
    return stats;
}

}  // namespace han::world
