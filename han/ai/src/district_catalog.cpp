#include "han/ai/district_catalog.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "han/core/json.h"

namespace han::ai {
namespace {
using core::Json;

bool insidePolygon(const std::vector<Vec2>& poly, float x, float z) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if ((poly[i].z > z) != (poly[j].z > z) && x < (poly[j].x - poly[i].x) * (z - poly[i].z) / (poly[j].z - poly[i].z) + poly[i].x) in = !in;
    }
    return in;
}

bool parseDistrict(const Json& j, DistrictInfo& out, std::string& error) {
    if (!j.isObject()) {
        error = "ожидался объект";
        return false;
    }
    out.id = j["id"].asString();
    out.nameKey = j["name_key"].asString();
    out.kind = j["kind"].asString();
    out.numericId = static_cast<contracts::DistrictId>(j["numeric_id"].asNumber(0));
    out.sacredAllowed = j["sacred_allowed"].asBool(false);
    out.danger = static_cast<int>(j["danger"].asNumber(0));
    if (out.id.empty() || out.nameKey.rfind("world.", 0) != 0 || out.kind.empty()) {
        error = "нужны id, name_key вида world.* и kind";
        return false;
    }
    if (out.numericId == 0) {
        error = out.id + ": numeric_id должен быть больше 0 (0 — «вне районов»)";
        return false;
    }
    const Json& b = j["bounds"];
    if (!b.isArray() || b.size() < 3) {
        error = out.id + ": bounds — многоугольник минимум из трёх точек";
        return false;
    }
    for (const Json& p : b.items()) {
        if (!p.isArray() || p.size() != 2) {
            error = out.id + ": точка контура — пара [x, z]";
            return false;
        }
        out.polygon.push_back({static_cast<float>(p.at(0).asNumber()), static_cast<float>(p.at(1).asNumber())});
    }
    for (const Json& t : j["tags"].items()) out.tags.push_back(t.asString());
    if (j.has("ambience")) {
        const Json& a = j["ambience"];
        out.ambience.lut = a["lut"].asString();
        out.ambience.saturation = static_cast<float>(a["saturation"].asNumber(1.0));
        out.ambience.contrast = static_cast<float>(a["contrast"].asNumber(1.2));
        out.ambience.neon = static_cast<float>(a["neon"].asNumber(1.0));
        out.ambience.crowdDensity = static_cast<float>(a["crowd_density"].asNumber(1.0));
        out.ambience.musicKey = a["music_key"].asString();
        if (a["lamp_color"].size() == 3)
            for (std::size_t i = 0; i < 3; ++i) out.ambience.lampColor[i] = static_cast<float>(a["lamp_color"].at(i).asNumber(1.0));
        if (out.ambience.saturation < 0.5f || out.ambience.saturation > 2.f || out.ambience.neon < 0.f || out.ambience.neon > 4.f) {
            error = out.id + ": ambience вне допустимых пределов (saturation 0,5–2, neon 0–4)";
            return false;
        }
    }
    return true;
}
}  // namespace

bool DistrictCatalog::loadDirectory(const std::string& dir, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        error = "нет папки " + dir;
        return false;
    }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    std::string problems;
    for (const fs::path& f : files) {
        Json json;
        std::string err;
        DistrictInfo d;
        if (!core::loadJsonFile(f.string(), json, err) || !parseDistrict(json, d, err)) {
            problems += f.filename().string() + ": " + err + "\n";
            continue;
        }
        bool duplicate = false;
        for (const DistrictInfo& o : districts_)
            if (o.id == d.id || o.numericId == d.numericId) duplicate = true;
        if (duplicate) {
            problems += f.filename().string() + ": повторяющийся id или numeric_id\n";
            continue;
        }
        districts_.push_back(std::move(d));
    }
    error += problems;
    return problems.empty();
}

const DistrictInfo* DistrictCatalog::infoAt(Vec2 p) const {
    for (const DistrictInfo& d : districts_)
        if (insidePolygon(d.polygon, p.x, p.z)) return &d;
    return nullptr;
}

const DistrictInfo* DistrictCatalog::byId(const std::string& id) const {
    for (const DistrictInfo& d : districts_)
        if (d.id == id) return &d;
    return nullptr;
}

void DistrictCatalog::registerIn(WorldServices& services) const {
    for (const DistrictInfo& d : districts_) services.addDistrict({d.numericId, d.polygon});
}

Ambience DistrictCatalog::ambienceAt(Vec2 p) const {
    const DistrictInfo* d = infoAt(p);
    return d ? d->ambience : Ambience{};
}

void blendAmbience(Ambience& cur, const Ambience& target, float dt, float tau) {
    const float k = 1.f - std::exp(-std::max(0.f, dt) / std::max(0.01f, tau));
    cur.saturation += (target.saturation - cur.saturation) * k;
    cur.contrast += (target.contrast - cur.contrast) * k;
    cur.neon += (target.neon - cur.neon) * k;
    cur.crowdDensity += (target.crowdDensity - cur.crowdDensity) * k;
    for (int i = 0; i < 3; ++i) cur.lampColor[i] += (target.lampColor[i] - cur.lampColor[i]) * k;
    cur.lut = target.lut;
    cur.musicKey = target.musicKey;
}

}  // namespace han::ai
