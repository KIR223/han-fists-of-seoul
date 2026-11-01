#include "han/ai/traffic.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <queue>
#include <unordered_map>

#include "han/world/city_builder.h"

namespace han::ai {
namespace {

bool driveable(std::string_view h) {
    return h == "motorway" || h == "motorway_link" || h == "trunk" || h == "trunk_link" || h == "primary" || h == "primary_link" || h == "secondary" ||
           h == "secondary_link" || h == "tertiary" || h == "tertiary_link" || h == "unclassified" || h == "residential" || h == "living_street";
}

RoadClass classOf(std::string_view h) {
    if (h.rfind("motorway", 0) == 0 || h.rfind("trunk", 0) == 0) return RoadClass::Motorway;
    if (h.rfind("primary", 0) == 0) return RoadClass::Primary;
    if (h.rfind("secondary", 0) == 0 || h.rfind("tertiary", 0) == 0) return RoadClass::Secondary;
    return RoadClass::Local;
}

// Ограничения скорости (м/с): магистрали 80 км/ч, главные 60, второстепенные 50, жилые 30 (городские правила Кореи).
float speedOf(RoadClass c, std::string_view highway) {
    if (highway == "living_street") return 20.f / 3.6f;
    switch (c) {
        case RoadClass::Motorway: return 80.f / 3.6f;
        case RoadClass::Primary: return 60.f / 3.6f;
        case RoadClass::Secondary: return 50.f / 3.6f;
        case RoadClass::Local: return 30.f / 3.6f;
    }
    return 8.f;
}

float lengthOf(const std::vector<Vec2>& pts) {
    float l = 0.f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) l += std::sqrt((pts[i + 1].x - pts[i].x) * (pts[i + 1].x - pts[i].x) + (pts[i + 1].z - pts[i].z) * (pts[i + 1].z - pts[i].z));
    return l;
}
}  // namespace

Vec2 RoadEdge::directionAt(float s) const {
    if (pts.size() < 2) return {0.f, -1.f};
    std::size_t i = static_cast<std::size_t>(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin());
    i = std::clamp<std::size_t>(i, 1, pts.size() - 1);
    const Vec2 d = pts[i] - pts[i - 1];
    const float l = std::sqrt(d.x * d.x + d.z * d.z);
    return l > 1e-6f ? Vec2{d.x / l, d.z / l} : Vec2{0.f, -1.f};
}

Vec2 RoadEdge::pointAt(float s, float offset) const {
    if (pts.empty()) return {};
    s = std::clamp(s, 0.f, length);
    std::size_t i = static_cast<std::size_t>(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin());
    i = std::clamp<std::size_t>(i, 1, pts.size() - 1);
    const float seg = cum[i] - cum[i - 1];
    const float t = seg > 1e-6f ? (s - cum[i - 1]) / seg : 0.f;
    const Vec2 p = pts[i - 1] + (pts[i] - pts[i - 1]) * t;
    const Vec2 d = directionAt(s);
    return {p.x - d.z * offset, p.z + d.x * offset};  // вправо от направления движения: (-dz, dx)
}

void RoadGraph::finish() {
    for (RoadNode& n : nodes_) {
        n.out.clear();
        n.in.clear();
    }
    std::map<std::pair<int, int>, int> byEnds;
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        RoadEdge& e = edges_[i];
        e.cum.assign(e.pts.size(), 0.f);
        for (std::size_t k = 1; k < e.pts.size(); ++k) {
            const Vec2 d = e.pts[k] - e.pts[k - 1];
            e.cum[k] = e.cum[k - 1] + std::sqrt(d.x * d.x + d.z * d.z);
        }
        e.length = e.cum.empty() ? 0.f : e.cum.back();
        nodes_[static_cast<std::size_t>(e.from)].out.push_back(static_cast<int>(i));
        nodes_[static_cast<std::size_t>(e.to)].in.push_back(static_cast<int>(i));
        byEnds[{e.from, e.to}] = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        const auto it = byEnds.find({edges_[i].to, edges_[i].from});
        edges_[i].reverse = it == byEnds.end() ? -1 : it->second;
    }
    for (std::size_t n = 0; n < nodes_.size(); ++n) {
        std::vector<int> neighbours;
        for (int e : nodes_[n].out) neighbours.push_back(edges_[static_cast<std::size_t>(e)].to);
        for (int e : nodes_[n].in) neighbours.push_back(edges_[static_cast<std::size_t>(e)].from);
        std::sort(neighbours.begin(), neighbours.end());
        neighbours.erase(std::unique(neighbours.begin(), neighbours.end()), neighbours.end());
        nodes_[n].junction = neighbours.size() >= 3;
    }
}

RoadGraph RoadGraph::fromLines(const std::vector<Line>& lines) {
    RoadGraph g;
    std::map<std::pair<int, int>, int> nodeAt;  // координаты в сантиметрах → узел
    const auto nodeFor = [&](Vec2 p) {
        const std::pair<int, int> key{static_cast<int>(std::lround(p.x * 100.f)), static_cast<int>(std::lround(p.z * 100.f))};
        const auto it = nodeAt.find(key);
        if (it != nodeAt.end()) return it->second;
        const int id = static_cast<int>(g.nodes_.size());
        RoadNode n;
        n.pos = p;
        g.nodes_.push_back(n);
        nodeAt[key] = id;
        return id;
    };
    for (const Line& l : lines) {
        if (l.pts.size() < 2) continue;
        const auto add = [&](std::vector<Vec2> pts) {
            RoadEdge e;
            e.pts = std::move(pts);
            e.from = nodeFor(e.pts.front());
            e.to = nodeFor(e.pts.back());
            e.cls = l.cls;
            e.lanes = std::max(1, l.lanesPerDirection);
            e.speed = speedOf(l.cls, "");
            if (lengthOf(e.pts) < 0.5f || e.from == e.to) return;
            g.edges_.push_back(std::move(e));
        };
        add(l.pts);
        if (!l.oneway) add(std::vector<Vec2>(l.pts.rbegin(), l.pts.rend()));
    }
    g.finish();
    return g;
}

RoadGraph RoadGraph::build(const world::OsmData& data, const world::GeoProjection& projection, const RoadGraphSettings&) {
    RoadGraph g;
    // Сколько раз узел встречается в дорогах для машин; узел-ключ — перекрёсток, конец линии или самопересечение.
    std::unordered_map<std::int64_t, int> uses;
    for (const world::OsmWay& w : data.ways) {
        if (!driveable(w.tag("highway"))) continue;
        for (std::size_t i = 0; i < w.nodes.size(); ++i) {
            ++uses[w.nodes[i]];
            if (i == 0 || i + 1 == w.nodes.size()) ++uses[w.nodes[i]];
        }
    }
    std::unordered_map<std::int64_t, int> index;
    const auto nodeFor = [&](std::int64_t osmId, Vec2 p) {
        const auto it = index.find(osmId);
        if (it != index.end()) return it->second;
        const int id = static_cast<int>(g.nodes_.size());
        RoadNode n;
        n.pos = p;
        g.nodes_.push_back(n);
        index[osmId] = id;
        return id;
    };
    for (const world::OsmWay& w : data.ways) {
        const std::string_view highway = w.tag("highway");
        if (!driveable(highway)) continue;
        std::vector<Vec2> pts;
        std::vector<std::int64_t> ids;
        for (std::int64_t id : w.nodes) {
            const auto it = data.nodes.find(id);
            if (it == data.nodes.end()) continue;
            Vec2 p;
            projection.toLocal(it->second.lat, it->second.lon, p.x, p.z);
            pts.push_back(p);
            ids.push_back(id);
        }
        if (pts.size() < 2) continue;
        const RoadClass cls = classOf(highway);
        const std::string_view onewayTag = w.tag("oneway");
        bool oneway = onewayTag == "yes" || onewayTag == "true" || onewayTag == "1" || onewayTag == "-1" || w.tag("junction") == "roundabout";
        if (onewayTag == "no") oneway = false;
        else if (onewayTag.empty() && cls == RoadClass::Motorway) oneway = true;
        const bool backwards = onewayTag == "-1";
        const float width = std::max(world::roadWidth(highway), 4.f);
        int lanesTag = 0;
        const std::string_view lt = w.tag("lanes");
        if (!lt.empty()) lanesTag = std::atoi(std::string(lt).c_str());
        int perDir = lanesTag > 0 ? (oneway ? lanesTag : std::max(1, lanesTag / 2)) : (width >= 10.f || cls == RoadClass::Motorway ? 2 : 1);
        perDir = std::clamp(perDir, 1, 4);
        const float laneW = std::clamp(width / static_cast<float>(oneway ? perDir : 2 * perDir), 2.6f, 3.6f);

        std::size_t start = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            const bool key = i + 1 == pts.size() || uses[ids[i]] >= 2;
            if (!key) continue;
            std::vector<Vec2> seg(pts.begin() + static_cast<std::ptrdiff_t>(start), pts.begin() + static_cast<std::ptrdiff_t>(i) + 1);
            const int a = nodeFor(ids[start], pts[start]), b = nodeFor(ids[i], pts[i]);
            start = i;
            if (a == b || lengthOf(seg) < 0.5f) continue;
            const auto add = [&](int from, int to, std::vector<Vec2> line) {
                RoadEdge e;
                e.from = from;
                e.to = to;
                e.pts = std::move(line);
                e.cls = cls;
                e.lanes = perDir;
                e.laneWidth = laneW;
                e.speed = speedOf(cls, highway);
                g.edges_.push_back(std::move(e));
            };
            std::vector<Vec2> rev(seg.rbegin(), seg.rend());
            if (!oneway) {
                add(a, b, seg);
                add(b, a, rev);
            } else if (backwards) {
                add(b, a, rev);
            } else {
                add(a, b, seg);
            }
        }
    }
    g.finish();
    return g;
}

int RoadGraph::nearestNode(Vec2 p) const {
    int best = -1;
    float bestD = 1e30f;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const float dx = nodes_[i].pos.x - p.x, dz = nodes_[i].pos.z - p.z;
        const float d = dx * dx + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::vector<int> RoadGraph::shortestPath(int fromNode, int toNode) const {
    const std::size_t n = nodes_.size();
    if (fromNode < 0 || toNode < 0 || static_cast<std::size_t>(fromNode) >= n || static_cast<std::size_t>(toNode) >= n || fromNode == toNode) return {};
    std::vector<float> cost(n, 1e30f);
    std::vector<int> viaEdge(n, -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    cost[static_cast<std::size_t>(fromNode)] = 0.f;
    open.push({0.f, fromNode});
    while (!open.empty()) {
        const auto [c, u] = open.top();
        open.pop();
        if (c > cost[static_cast<std::size_t>(u)]) continue;
        if (u == toNode) break;
        for (int ei : nodes_[static_cast<std::size_t>(u)].out) {
            const RoadEdge& e = edges_[static_cast<std::size_t>(ei)];
            const float nc = c + e.length / std::max(1.f, e.speed);
            if (nc < cost[static_cast<std::size_t>(e.to)]) {
                cost[static_cast<std::size_t>(e.to)] = nc;
                viaEdge[static_cast<std::size_t>(e.to)] = ei;
                open.push({nc, e.to});
            }
        }
    }
    if (viaEdge[static_cast<std::size_t>(toNode)] < 0) return {};
    std::vector<int> path;
    for (int v = toNode; v != fromNode;) {
        const int ei = viaEdge[static_cast<std::size_t>(v)];
        path.push_back(ei);
        v = edges_[static_cast<std::size_t>(ei)].from;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

float RoadGraph::totalLength() const {
    float l = 0.f;
    for (const RoadEdge& e : edges_) l += e.length;
    return l;
}

// ----------------------------------------------------------------------------------------------------------------- Трафик

TrafficSim::TrafficSim(const RoadGraph& graph, const TrafficSettings& settings) : g_(graph), cfg_(settings), rng_(settings.seed ? settings.seed : 1u) {
    locks_.resize(g_.nodes().size());
    lanes_.resize(g_.edges().size());
    for (std::size_t e = 0; e < g_.edges().size(); ++e) lanes_[e].resize(static_cast<std::size_t>(std::max(1, g_.edges()[e].lanes)));
}

void TrafficSim::update(float dt) {
    int steps = std::max(1, static_cast<int>(std::ceil(dt / 0.1f)));
    steps = std::min(steps, 8);
    for (int i = 0; i < steps; ++i) step(dt / static_cast<float>(steps));
    float sum = 0.f;
    stats_.moving = stats_.waiting = 0;
    for (const Car& c : cars_) {
        sum += c.v;
        (c.v > 0.5f ? stats_.moving : stats_.waiting)++;
    }
    stats_.meanSpeed = cars_.empty() ? 0.f : sum / static_cast<float>(cars_.size());
}

int TrafficSim::pickNext(const Car& c, int edge) {
    const RoadEdge& e = g_.edges()[static_cast<std::size_t>(edge)];
    const RoadNode& n = g_.nodes()[static_cast<std::size_t>(e.to)];
    if (n.out.empty()) return -1;
    std::vector<int> options;
    for (int o : n.out)
        if (o != e.reverse) options.push_back(o);
    if (options.empty()) options = n.out;  // тупик: разворот по встречной
    std::vector<float> w;
    for (int o : options) w.push_back(g_.edges()[static_cast<std::size_t>(o)].cls == e.cls ? 2.f : 1.f);
    std::discrete_distribution<int> pick(w.begin(), w.end());
    (void)c;
    return options[static_cast<std::size_t>(pick(rng_))];
}

void TrafficSim::removeCar(std::size_t index) {
    const std::uint32_t id = cars_[index].id;
    for (Lock& l : locks_) {
        if (l.holder >= 0 && static_cast<std::uint32_t>(l.holder) == id) l.holder = -1;
        l.queue.erase(std::remove(l.queue.begin(), l.queue.end(), id), l.queue.end());
    }
    cars_.erase(cars_.begin() + static_cast<std::ptrdiff_t>(index));
    ++stats_.despawned;
}

void TrafficSim::spawnCars() {
    if (g_.edges().empty()) return;
    std::uniform_real_distribution<float> u(0.f, 1.f);
    int attempts = 12;
    while (static_cast<int>(cars_.size()) < cfg_.maxCars && attempts-- > 0) {
        const std::size_t ei = static_cast<std::size_t>(u(rng_) * static_cast<float>(g_.edges().size())) % g_.edges().size();
        const RoadEdge& e = g_.edges()[ei];
        if (e.length < 12.f) continue;
        const float s = e.length * (0.1f + 0.8f * u(rng_));
        const int lane = static_cast<int>(u(rng_) * static_cast<float>(e.lanes)) % e.lanes;
        if (cfg_.spawnRadius > 0.f) {
            const Vec2 p = e.pointAt(s);
            const float dx = p.x - focus_.x, dz = p.z - focus_.z;
            if (dx * dx + dz * dz > cfg_.spawnRadius * cfg_.spawnRadius) continue;
        }
        bool free = true;
        for (const Car& o : cars_)
            if (o.edge == static_cast<int>(ei) && o.lane == lane && std::fabs(o.s - s) < 18.f) free = false;
        if (!free) continue;
        Car c;
        c.id = nextId_++;
        c.edge = static_cast<int>(ei);
        c.lane = lane;
        c.s = s;
        c.length = cfg_.carLength * (0.92f + 0.2f * u(rng_));
        c.v0 = e.speed * (0.85f + 0.25f * u(rng_));
        c.v = c.v0 * 0.6f;
        c.next = pickNext(c, c.edge);
        cars_.push_back(c);
        ++stats_.spawned;
    }
}

float TrafficSim::gapAhead(const Car& c, float& leaderSpeed) const {
    const RoadEdge& e = g_.edges()[static_cast<std::size_t>(c.edge)];
    const auto& list = lanes_[static_cast<std::size_t>(c.edge)][static_cast<std::size_t>(c.lane)];
    float best = 1e9f;
    leaderSpeed = c.v0;
    for (int idx : list) {  // от переднего к заднему: нужен ближайший спереди
        const Car& o = cars_[static_cast<std::size_t>(idx)];
        if (o.id == c.id) continue;
        if (o.s > c.s) {
            const float gap = o.s - o.length - c.s;
            if (gap < best) {
                best = gap;
                leaderSpeed = o.v;
            }
        }
    }
    if (best < 1e8f || c.next < 0) return best;
    const RoadEdge& n = g_.edges()[static_cast<std::size_t>(c.next)];
    const int lane = std::min(c.lane, n.lanes - 1);
    for (int idx : lanes_[static_cast<std::size_t>(c.next)][static_cast<std::size_t>(lane)]) {
        const Car& o = cars_[static_cast<std::size_t>(idx)];
        const float gap = (e.length - c.s) + o.s - o.length;
        if (gap < best) {
            best = gap;
            leaderSpeed = o.v;
        }
    }
    return best;
}

bool TrafficSim::canEnter(const Car& c) const {
    if (c.next < 0) return true;
    const RoadEdge& n = g_.edges()[static_cast<std::size_t>(c.next)];
    const int lane = std::min(c.lane, n.lanes - 1);
    for (int idx : lanes_[static_cast<std::size_t>(c.next)][static_cast<std::size_t>(lane)]) {
        const Car& o = cars_[static_cast<std::size_t>(idx)];
        if (o.s - o.length < c.length + 3.f) return false;  // на входе не хватает места для машины
    }
    return true;
}

void TrafficSim::step(float dt) {
    // Появление и уход машин.
    if (cfg_.despawnRadius > 0.f) {
        for (std::size_t i = cars_.size(); i-- > 0;) {
            const float dx = cars_[i].pos.x - focus_.x, dz = cars_[i].pos.z - focus_.z;
            if (dx * dx + dz * dz > cfg_.despawnRadius * cfg_.despawnRadius) removeCar(i);
        }
    }
    spawnCars();

    // Полосы: машины спереди назад.
    for (auto& edgeLanes : lanes_)
        for (auto& l : edgeLanes) l.clear();
    for (std::size_t i = 0; i < cars_.size(); ++i) lanes_[static_cast<std::size_t>(cars_[i].edge)][static_cast<std::size_t>(cars_[i].lane)].push_back(static_cast<int>(i));
    for (auto& edgeLanes : lanes_)
        for (auto& l : edgeLanes) std::sort(l.begin(), l.end(), [&](int a, int b) { return cars_[static_cast<std::size_t>(a)].s > cars_[static_cast<std::size_t>(b)].s; });

    // Очереди на перекрёстки: свободный узел отдаётся первой по очереди машине, которой есть куда выехать (остальные не блокируются).
    std::unordered_map<std::uint32_t, std::size_t> byId;
    for (std::size_t i = 0; i < cars_.size(); ++i) byId[cars_[i].id] = i;
    for (std::size_t n = 0; n < locks_.size(); ++n) {
        Lock& lock = locks_[n];
        lock.queue.erase(std::remove_if(lock.queue.begin(), lock.queue.end(), [&](std::uint32_t id) { return !byId.count(id); }), lock.queue.end());
        if (lock.holder >= 0 || lock.queue.empty()) continue;
        for (auto it = lock.queue.begin(); it != lock.queue.end(); ++it) {
            Car& c = cars_[byId[*it]];
            if (g_.edges()[static_cast<std::size_t>(c.edge)].to != static_cast<int>(n)) continue;
            const auto& laneList = lanes_[static_cast<std::size_t>(c.edge)][static_cast<std::size_t>(c.lane)];
            if (laneList.empty() || cars_[static_cast<std::size_t>(laneList.front())].id != c.id) continue;  // впереди ещё машина: проедет она
            if (!canEnter(c)) continue;
            lock.holder = static_cast<int>(c.id);
            c.heldNodes.push_back(static_cast<int>(n));
            lock.queue.erase(it);
            break;
        }
    }

    // Ускорения (IDM).
    std::vector<float> accel(cars_.size(), 0.f);
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        Car& c = cars_[i];
        const RoadEdge& e = g_.edges()[static_cast<std::size_t>(c.edge)];
        float vl = c.v0;
        float gap = gapAhead(c, vl);
        if (c.next >= 0 && g_.nodes()[static_cast<std::size_t>(e.to)].junction) {
            Lock& lock = locks_[static_cast<std::size_t>(e.to)];
            const float toEnd = e.length - c.s;
            const bool holds = lock.holder >= 0 && static_cast<std::uint32_t>(lock.holder) == c.id;
            if (!holds) {
                const float brakeDist = c.v * c.v / (2.f * cfg_.comfortBrake);
                if (toEnd < std::max(14.f, brakeDist + 8.f) && std::find(lock.queue.begin(), lock.queue.end(), c.id) == lock.queue.end()) lock.queue.push_back(c.id);
            }
            if (!(lock.holder >= 0 && static_cast<std::uint32_t>(lock.holder) == c.id)) {  // не пропущен: стоп-линия — препятствие
                const float stopGap = toEnd - 1.0f;
                if (stopGap < gap) {
                    gap = stopGap;
                    vl = 0.f;
                }
            }
        }
        const float dv = c.v - vl;
        const float sStar = cfg_.minGap + std::max(0.f, c.v * cfg_.timeHeadway + c.v * dv / (2.f * std::sqrt(cfg_.maxAccel * cfg_.comfortBrake)));
        float a = cfg_.maxAccel * (1.f - std::pow(c.v / std::max(0.1f, c.v0), 4.f) - (gap < 1e8f ? (sStar / std::max(0.1f, gap)) * (sStar / std::max(0.1f, gap)) : 0.f));
        accel[i] = std::clamp(a, -9.f, cfg_.maxAccel);
    }

    // Движение, переезд на следующую дорогу, освобождение перекрёстков, сторожевой таймер.
    std::vector<std::size_t> remove;
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        Car& c = cars_[i];
        c.v = std::max(0.f, c.v + accel[i] * dt);
        c.s += c.v * dt;
        const RoadEdge* e = &g_.edges()[static_cast<std::size_t>(c.edge)];
        if (c.s >= e->length) {
            if (c.next < 0) {
                ++stats_.reachedEnd;
                remove.push_back(i);
                continue;
            }
            // Въезд на следующую дорогу. Если узел-перекрёсток не отдан этой машине (данные без ограничений), всё равно едем: стоп-линия уже удерживала.
            c.s -= e->length;
            c.edge = c.next;
            e = &g_.edges()[static_cast<std::size_t>(c.edge)];
            c.lane = std::min(c.lane, e->lanes - 1);
            c.v0 = std::min(c.v0, e->speed * 1.1f);
            c.v0 = std::max(c.v0, e->speed * 0.8f);
            c.next = pickNext(c, c.edge);
        }
        // Перекрёсток освобождается, когда хвост машины покинул узел.
        for (std::size_t h = c.heldNodes.size(); h-- > 0;) {
            const int node = c.heldNodes[h];
            const bool onApproach = e->to == node;                                                              // ещё на подъезде к узлу
            const bool leavingNode = e->from == node && c.s <= std::min(c.length + 1.f, 0.4f * e->length);  // хвост ещё внутри (на коротких дорогах отпускаем раньше)
            if (onApproach || leavingNode) continue;
            Lock& held = locks_[static_cast<std::size_t>(node)];
            if (held.holder >= 0 && static_cast<std::uint32_t>(held.holder) == c.id) held.holder = -1;
            c.heldNodes.erase(c.heldNodes.begin() + static_cast<std::ptrdiff_t>(h));
        }
        c.stoppedFor = c.v < 0.3f ? c.stoppedFor + dt : 0.f;
        if (c.stoppedFor > cfg_.stuckSeconds) {
            ++stats_.stuckResets;
            if (stats_.stuckEdges.size() < 200) stats_.stuckEdges.push_back(c.edge);
            if (stats_.stuckNotes.size() < 6) {
                float vl = 0.f;
                const float gap = gapAhead(c, vl);
                const RoadEdge& ce = g_.edges()[static_cast<std::size_t>(c.edge)];
                const Lock& lk = locks_[static_cast<std::size_t>(ce.to)];
                std::string note = "машина " + std::to_string(c.id) + " на ребре " + std::to_string(c.edge) + " s=" + std::to_string(c.s) + "/" + std::to_string(ce.length) + " v0=" + std::to_string(c.v0) +
                                   " зазор=" + std::to_string(gap) + " vл=" + std::to_string(vl) + " следующее=" + std::to_string(c.next) + " удерживает=" + std::to_string(c.heldNodes.size()) +
                                   " замок: держит=" + std::to_string(lk.holder) + " очередь=" + std::to_string(lk.queue.size()) + " можно_въехать=" + std::to_string(canEnter(c));
                stats_.stuckNotes.push_back(note);
            }
            remove.push_back(i);
            continue;
        }
        const float off = (static_cast<float>(c.lane) + 0.5f) * e->laneWidth;
        const float mid = std::max(0.f, c.s - c.length * 0.5f);
        c.pos = e->pointAt(mid, off);
        const Vec2 d = e->directionAt(mid);
        c.heading = std::atan2(d.x, -d.z);
    }
    for (std::size_t k = remove.size(); k-- > 0;) removeCar(remove[k]);
}

const Car* TrafficSim::nearestCar(Vec2 p, float radius) const {
    const Car* best = nullptr;
    float bestD = radius * radius;
    for (const Car& c : cars_) {
        const float dx = c.pos.x - p.x, dz = c.pos.z - p.z;
        const float d = dx * dx + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = &c;
        }
    }
    return best;
}

int TrafficSim::overlaps() const {
    std::map<std::pair<int, int>, std::vector<const Car*>> groups;
    for (const Car& c : cars_) groups[{c.edge, c.lane}].push_back(&c);
    int n = 0;
    for (auto& [key, list] : groups) {
        std::sort(list.begin(), list.end(), [](const Car* a, const Car* b) { return a->s > b->s; });
        for (std::size_t i = 0; i + 1 < list.size(); ++i)
            if (list[i]->s - list[i]->length < list[i + 1]->s - 0.01f) ++n;
    }
    return n;
}

}  // namespace han::ai
