#include "han/world/road_profile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>
#include <vector>

#include "han/core/json.h"
#include "han/world/city_builder.h"

namespace han::world {

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

struct Neighbor {
    std::int64_t id;
    float length;
};

struct BridgeNode {
    float x = 0, z = 0, ground = 0, lower = 0, upper = std::numeric_limits<float>::infinity(), height = 0;
    int layer = 1;
    int wayUses = 0;       // сколько раз узел встречается в мостовых путях
    bool endpoint = false; // начало или конец какого-то мостового пути
    bool anchor = false;   // стоит на земле: общий узел с обычной дорогой или тупиковый конец моста
    bool forced = false;   // перешёл на высоту рельефа: якоря несовместимы по уклону или габарит выше предела
    std::vector<Neighbor> next;
};
}  // namespace

bool isBridgeWay(const OsmWay& way) {
    const auto bridge = way.tag("bridge");
    return !way.tag("highway").empty() && !bridge.empty() && bridge != "no";
}

int wayLayer(const OsmWay& way) {
    const auto text = way.tag("layer");
    if (text.empty()) return isBridgeWay(way) ? 1 : 0;
    char* end = nullptr;
    const std::string value(text);
    const long layer = std::strtol(value.c_str(), &end, 10);
    return end == value.c_str() ? (isBridgeWay(way) ? 1 : 0) : static_cast<int>(std::clamp<long>(layer, -5, 9));
}

float RoadProfile::height(std::int64_t osmNode) const {
    const auto it = deck_.find(osmNode);
    return it == deck_.end() ? kNaN : it->second;
}

bool loadRoadProfileSettings(const std::string& file, RoadProfileSettings& out, std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(file, root, error)) return false;
    if (!root.isObject() || root["version"].asNumber() != 1) {
        error = file + ": неизвестная версия профиля дорог";
        return false;
    }
    RoadProfileSettings s;
    struct Field { const char* name; float* value; float lo, hi; };
    const Field fields[] = {
        {"max_grade", &s.maxGrade, 0.005f, 0.2f}, {"max_height_above_ground", &s.maxHeightAboveGround, 5.f, 200.f}, {"clearance_layer1", &s.clearanceLayer1, 0.5f, 60.f}, {"clearance_layer2", &s.clearanceLayer2, 0.5f, 60.f},
        {"clearance_layer3", &s.clearanceLayer3, 0.5f, 60.f}, {"deck_thickness", &s.deckThickness, 0.1f, 10.f}, {"pier_spacing", &s.pierSpacing, 5.f, 500.f},
        {"pier_size", &s.pierSize, 0.2f, 10.f}, {"min_pier_clearance", &s.minPierClearance, 0.f, 30.f}, {"collider_step", &s.colliderStep, 0.5f, 20.f},
        {"collider_thickness", &s.colliderThickness, 0.1f, 5.f},
    };
    for (const auto& f : fields) {
        const auto& value = root[f.name];
        const double n = value.asNumber();
        if (!value.isNumber() || !std::isfinite(n) || n < f.lo || n > f.hi) {
            error = file + ": некорректный параметр " + f.name;
            return false;
        }
        *f.value = static_cast<float>(n);
    }
    if (s.clearanceLayer2 < s.clearanceLayer1 || s.clearanceLayer3 < s.clearanceLayer2) {
        error = file + ": габариты слоёв должны не убывать";
        return false;
    }
    out = s;
    return true;
}

RoadProfile computeRoadProfile(const OsmData& data, const GeoProjection& projection, const Heightmap* terrain, const RoadProfileSettings& settings) {
    RoadProfile result;
    const auto groundAt = [&](float x, float z) { return terrain && terrain->valid() ? terrain->heightAt(x, z) : 0.f; };
    const auto clearanceFor = [&](int layer) {
        return layer <= 1 ? settings.clearanceLayer1 : (layer == 2 ? settings.clearanceLayer2 : settings.clearanceLayer3);
    };

    // Узлы, лежащие на обычных (не мостовых) рисуемых дорогах, — опоры на землю.
    std::unordered_set<std::int64_t> groundNodes;
    for (const OsmWay& way : data.ways) {
        if (way.tag("highway").empty() || roadWidth(way) <= 0.f) continue;
        if (way.tag("tunnel").empty() ? false : way.tag("tunnel") != "no") ++result.stats_.tunnelWays;
        if (isBridgeWay(way)) continue;
        for (const auto id : way.nodes) groundNodes.insert(id);
    }

    std::unordered_map<std::int64_t, BridgeNode> nodes;
    const auto position = [&](std::int64_t id, float& x, float& z) {
        const auto it = data.nodes.find(id);
        if (it == data.nodes.end()) return false;
        projection.toLocal(it->second.lat, it->second.lon, x, z);
        return true;
    };
    for (const OsmWay& way : data.ways) {
        if (!isBridgeWay(way) || roadWidth(way) <= 0.f) continue;
        std::vector<std::int64_t> ids;
        for (const auto id : way.nodes) {
            float x, z;
            if (position(id, x, z)) ids.push_back(id);
        }
        if (ids.size() < 2) continue;
        ++result.stats_.bridgeWays;
        const int layer = std::max(1, wayLayer(way));
        for (std::size_t i = 0; i < ids.size(); ++i) {
            BridgeNode& n = nodes[ids[i]];
            position(ids[i], n.x, n.z);
            n.layer = std::max(n.layer, layer);
            ++n.wayUses;
            if (i == 0 || i + 1 == ids.size()) n.endpoint = true;
        }
        for (std::size_t i = 0; i + 1 < ids.size(); ++i) {
            BridgeNode& a = nodes[ids[i]];
            BridgeNode& b = nodes[ids[i + 1]];
            const float length = std::hypot(b.x - a.x, b.z - a.z);
            if (length < 1e-3f) continue;
            a.next.push_back({ids[i + 1], length});
            b.next.push_back({ids[i], length});
        }
    }
    if (nodes.empty()) return result;

    for (auto& [id, n] : nodes) {
        n.ground = groundAt(n.x, n.z);
        n.lower = n.ground + clearanceFor(n.layer);
        // Якорь: узел общий с обычной дорогой, либо тупиковый конец (конец пути, который больше нигде не продолжается).
        n.anchor = groundNodes.count(id) != 0 || (n.endpoint && n.wayUses <= 1);
        if (n.anchor) ++result.stats_.anchorNodes;
    }
    // min по источникам (init + maxGrade * расстояние по графу): многоисточниковый Дейкстра. init — значение в источнике, inf — не источник.
    const auto spread = [&](const std::unordered_map<std::int64_t, float>& init) {
        std::unordered_map<std::int64_t, float> best;
        std::priority_queue<std::pair<float, std::int64_t>, std::vector<std::pair<float, std::int64_t>>, std::greater<>> open;
        for (const auto& [id, value] : init) {
            best[id] = value;
            open.push({value, id});
        }
        while (!open.empty()) {
            const auto [value, id] = open.top();
            open.pop();
            if (value > best[id]) continue;
            for (const Neighbor& nb : nodes[id].next) {
                const float candidate = value + settings.maxGrade * nb.length;
                const auto it = best.find(nb.id);
                if (it == best.end() || candidate < it->second) {
                    best[nb.id] = candidate;
                    open.push({candidate, nb.id});
                }
            }
        }
        return best;
    };
    const float inf = std::numeric_limits<float>::infinity();
    // Итерации: узлы, которым профиль не подходит (якоря несовместимы по уклону или габарит выше предела), переходят на рельеф и сами становятся
    // якорями; расчёт повторяется, пока набор не перестанет расти.
    for (int pass = 0; pass < 2000; ++pass) {
        result.stats_.solverPasses = pass + 1;
        if (pass == 1999) result.stats_.converged = false;
        std::unordered_map<std::int64_t, float> top, bottom;
        for (const auto& [id, n] : nodes)
            if (n.anchor || n.forced) {
                top[id] = n.ground;
                bottom[id] = -n.ground;
            }
        const auto upper = spread(top);    // не выше, чем можно подняться от якорей
        const auto bottomUp = spread(bottom);  // не ниже, чем нужно по уклону от якорей: -(min(-g + G*d))
        bool changed = false;
        for (auto& [id, n] : nodes) {
            if (n.anchor || n.forced) {
                n.height = n.ground;
                continue;
            }
            const auto up = upper.find(id);
            const auto down = bottomUp.find(id);
            n.upper = up == upper.end() ? inf : up->second;
            const float floor = down == bottomUp.end() ? -inf : -down->second;
            if (floor > n.upper + 0.002f) {  // якоря несовместимы по уклону: между ними нет высоты, подходящей обоим
                n.forced = true;
                changed = true;
                continue;
            }
            n.height = std::min(n.lower, n.upper);
        }
        if (changed) continue;
        // Неподвижная точка: подъём только от соседа с ограничением уклона, не выше верхней границы.
        std::queue<std::int64_t> work;
        for (const auto& [id, n] : nodes) work.push(id);
        for (std::size_t guard = 0; !work.empty() && guard < nodes.size() * 400 + 1000; ++guard) {
            const std::int64_t id = work.front();
            work.pop();
            const BridgeNode& a = nodes[id];
            for (const Neighbor& nb : a.next) {
                BridgeNode& b = nodes[nb.id];
                if (b.anchor || b.forced) continue;
                const float need = a.height - settings.maxGrade * nb.length;
                if (need > b.height + 1e-4f) {
                    b.height = std::min(need, b.upper);
                    work.push(nb.id);
                }
            }
        }
        for (auto& [id, n] : nodes) {
            if (n.anchor || n.forced) continue;
            // Слишком высокий виадук над крутым склоном или полотно глубже рельефа (якоря в низинах, узел на гребне): полотно идёт по рельефу.
            if (n.height - n.ground > settings.maxHeightAboveGround || n.height < n.ground - 0.5f) {
                n.forced = true;
                changed = true;
            }
        }
        if (!changed) break;
    }
    for (auto& [id, n] : nodes) {
        if (n.forced) {
            n.height = n.ground;
            ++result.stats_.terrainFollowingNodes;
        }
    }

    result.stats_.bridgeNodes = static_cast<int>(nodes.size());
    for (const auto& [id, n] : nodes) {  // участки между двумя узлами на рельефе рисуются как обычная дорога по земле и сюда не входят
        if (n.forced) result.terrain_.insert(id);
        else if (!n.anchor) result.free_.insert(id);
        for (const Neighbor& nb : n.next)
            if (id < nb.id && (!n.anchor && !n.forced ? true : (!nodes[nb.id].anchor && !nodes[nb.id].forced)) && nb.length > 10.f && std::fabs(n.height - nodes[nb.id].height) / nb.length > 0.12f)
                ++result.stats_.steepSegments;
    }
    for (const auto& [id, n] : nodes) {
        result.deck_[id] = n.height;
        result.stats_.maxClearance = std::max(result.stats_.maxClearance, n.height - n.ground);
        if (n.height < n.ground - 0.5f) ++result.stats_.buriedNodes;
        if (n.anchor || n.forced) continue;  // уклон считается там, где высоту задаёт профиль
        for (const Neighbor& nb : n.next) {
            const float grade = std::fabs(n.height - nodes[nb.id].height) / nb.length;
            if (grade > result.stats_.maxGradeUsed) {
                result.stats_.maxGradeUsed = grade;
                result.stats_.worstGradeNode = id;
            }
        }
    }
    return result;
}

}  // namespace han::world
