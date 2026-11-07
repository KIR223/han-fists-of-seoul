#include "han/world/road_profile.h"

#include <algorithm>
#include <cmath>
#include <set>
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

bool isTunnelWay(const OsmWay& way) {
    const auto tunnel = way.tag("tunnel");
    return !way.tag("highway").empty() && !tunnel.empty() && tunnel != "no" && tunnel != "building_passage" && tunnel != "culvert";
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
        {"collider_thickness", &s.colliderThickness, 0.1f, 5.f}, {"tunnel_cover", &s.tunnelCover, 0.5f, 30.f},
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
        if (isBridgeWay(way) || isTunnelWay(way)) continue;
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
    const auto solveTunnels = [&]() {
        // Тоннели: путь режется на цепочки между порталами. Портал — узел, общий с обычной дорогой, узел с другой высотой (мост), тупик или развилка тоннелей;
        // он стоит на земле. Между порталами полотно идёт по прямой между отметками порталов, но не выше рельефа за вычетом слоя грунта,
        // который нарастает от портала не круче maxGrade. Поверхность земли закрывает дорогу сверху.
        std::unordered_map<std::int64_t, std::vector<std::int64_t>> tunnelAdj;
        for (const OsmWay& way : data.ways) {
            if (!isTunnelWay(way) || roadWidth(way) <= 0.f || isBridgeWay(way)) continue;
            std::int64_t previous = 0;
            bool havePrevious = false;
            for (const auto id : way.nodes) {
                float x, z;
                if (!position(id, x, z)) continue;
                if (havePrevious && previous != id) {
                    auto& a = tunnelAdj[previous];
                    auto& b = tunnelAdj[id];
                    if (std::find(a.begin(), a.end(), id) == a.end()) a.push_back(id);
                    if (std::find(b.begin(), b.end(), previous) == b.end()) b.push_back(previous);
                }
                previous = id;
                havePrevious = true;
            }
        }
        const auto isPortal = [&](std::int64_t id) { return groundNodes.count(id) != 0 || result.deck_.count(id) != 0 || tunnelAdj[id].size() != 2; };
        std::set<std::pair<std::int64_t, std::int64_t>> walked;
        const auto edgeKey = [](std::int64_t a, std::int64_t b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
        std::vector<std::int64_t> portalIds;
        for (const auto& [id, neighbours] : tunnelAdj) {
            (void)neighbours;
            if (isPortal(id)) portalIds.push_back(id);
        }
        std::sort(portalIds.begin(), portalIds.end());  // детерминированный порядок
        for (const std::int64_t portal : portalIds) {
            for (const std::int64_t first : std::vector<std::int64_t>(tunnelAdj[portal])) {
                if (walked.count(edgeKey(portal, first))) continue;
                std::vector<std::int64_t> ids = {portal, first};
                walked.insert(edgeKey(portal, first));
                std::int64_t previous = portal, current = first;
                while (!isPortal(current)) {
                    std::int64_t next = 0;
                    for (const auto candidate : tunnelAdj[current])
                        if (candidate != previous) next = candidate;
                    if (next == 0 || walked.count(edgeKey(current, next))) break;
                    walked.insert(edgeKey(current, next));
                    ids.push_back(next);
                    previous = current;
                    current = next;
                }
                std::vector<float> xs(ids.size()), zs(ids.size()), chain(ids.size(), 0.f);
                for (std::size_t i = 0; i < ids.size(); ++i) {
                    position(ids[i], xs[i], zs[i]);
                    if (i > 0) chain[i] = chain[i - 1] + std::hypot(xs[i] - xs[i - 1], zs[i] - zs[i - 1]);
                }
                const std::size_t last = ids.size() - 1;
                const float hA = groundAt(xs[0], zs[0]), hB = groundAt(xs[last], zs[last]), total = std::max(chain[last], 0.01f);
                std::vector<float> planned(ids.size());
                for (std::size_t i = 0; i <= last; ++i) {
                    const float ground = groundAt(xs[i], zs[i]);
                    planned[i] = ground;
                    if (i != 0 && i != last) {
                        const float d = std::min(chain[i], chain[last] - chain[i]);
                        const float line = hA + (hB - hA) * (chain[i] / total);
                        planned[i] = std::min(line, ground - std::min(settings.tunnelCover, settings.maxGrade * d));
                    }
                }
                // Уклон: полотно может только опускаться (оно остаётся под землёй), поэтому ограничиваем рост высоты от соседа в обе стороны.
                for (std::size_t i = 1; i < last; ++i) planned[i] = std::min(planned[i], planned[i - 1] + settings.maxGrade * (chain[i] - chain[i - 1]));
                for (std::size_t i = last - 1; i > 0; --i) planned[i] = std::min(planned[i], planned[i + 1] + settings.maxGrade * (chain[i + 1] - chain[i]));
                for (std::size_t i = 0; i <= last; ++i) {
                    if (result.deck_.count(ids[i])) continue;  // узел уже получил высоту (портал соседней цепочки)
                    const float ground = groundAt(xs[i], zs[i]);
                    const float h = planned[i];
                    result.deck_[ids[i]] = h;
                    if (i != 0 && i != last) {
                        result.free_.insert(ids[i]);
                        ++result.stats_.tunnelNodes;
                        result.stats_.maxTunnelDepth = std::max(result.stats_.maxTunnelDepth, ground - h);
                        if (h > ground + 0.1f) ++result.stats_.tunnelAboveGround;
                    }
                }
                for (std::size_t i = 0; i < last; ++i) {
                    const float length = chain[i + 1] - chain[i];
                    if (length > 3.f) result.stats_.tunnelMaxGrade = std::max(result.stats_.tunnelMaxGrade, std::fabs(result.deck_[ids[i]] - result.deck_[ids[i + 1]]) / length);
                    if (length > 10.f) {
                        ++result.stats_.tunnelSegments;
                        if (std::fabs(groundAt(xs[i], zs[i]) - groundAt(xs[i + 1], zs[i + 1])) / length > 0.12f) ++result.stats_.tunnelSurfaceSteepSegments;
                        if (std::fabs(result.deck_[ids[i]] - result.deck_[ids[i + 1]]) / length > 0.12f) ++result.stats_.tunnelSteepSegments;
                    }
                }
            }
        }
    };
    if (nodes.empty()) {
        solveTunnels();
        return result;
    }

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
    solveTunnels();
    return result;
}

}  // namespace han::world
