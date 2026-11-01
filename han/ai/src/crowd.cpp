#include "han/ai/crowd.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace han::ai {
namespace {
float length(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
Vec2 normalized(Vec2 v) {
    const float l = length(v);
    return l > 1e-5f ? Vec2{v.x / l, v.z / l} : Vec2{0.f, 0.f};
}
float headingOf(Vec2 v) { return std::atan2(v.x, -v.z); }  // 0 — на север (-Z)

bool movingState(AgentState s) { return s == AgentState::Walking || s == AgentState::Fleeing || s == AgentState::Intervening; }
bool reacting(AgentState s) { return s == AgentState::Fleeing || s == AgentState::Watching || s == AgentState::Filming || s == AgentState::Calling || s == AgentState::Intervening; }

// Как долго агент остаётся на месте после прихода (секунды игрового времени).
void waitRange(Activity a, float& lo, float& hi) {
    switch (a) {
        case Activity::Work:
        case Activity::Home:
        case Activity::Sleep: lo = 300.f; hi = 900.f; break;
        case Activity::Eat:
        case Activity::Cafe: lo = 120.f; hi = 400.f; break;
        case Activity::Shop: lo = 60.f; hi = 200.f; break;
        default: lo = 8.f; hi = 40.f; break;
    }
}
}  // namespace

Crowd::Crowd(const NavGrid& grid, std::vector<Poi> pois, const CrowdSettings& settings)
    : grid_(grid), pois_(std::move(pois)), settings_(settings), rng_(settings.seed ? settings.seed : 1u) {
    agents_.reserve(settings_.maxAgents);
}

float Crowd::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

AgentId Crowd::spawn(const Archetype& archetype, Vec2 pos) {
    if (agents_.size() >= settings_.maxAgents || !grid_.valid()) return 0;
    Vec2 p;
    if (!grid_.nearestWalkable(pos, 15.f, p)) return 0;
    Agent a;
    a.id = static_cast<AgentId>(agents_.size() + 1);
    a.archetype = &archetype;
    a.pos = a.home = a.destination = p;
    a.heading = rand01() * 6.2831853f - 3.1415926f;
    a.lineCooldown = randRange(0.f, 20.f);
    agents_.push_back(std::move(a));
    return agents_.back().id;
}

std::size_t Crowd::populate(const std::vector<const Archetype*>& archetypes, std::size_t n) {
    if (archetypes.empty() || !grid_.valid()) return 0;
    std::size_t made = 0;
    for (std::size_t i = 0; i < n && agents_.size() < settings_.maxAgents; ++i) {
        for (int attempt = 0; attempt < 60; ++attempt) {
            const Vec2 p{randRange(grid_.minX(), grid_.maxX()), randRange(grid_.minZ(), grid_.maxZ())};
            if (!grid_.walkable(p.x, p.z)) continue;
            if (archetypes[i % archetypes.size()] && spawn(*archetypes[i % archetypes.size()], p)) {
                ++made;
                break;
            }
        }
    }
    return made;
}

void Crowd::say(Agent& a, const std::string& key) {
    if (a.lineCooldown > 0.f) return;
    a.lineCooldown = 8.f;
    utterances_.push_back({a.id, key, a.pos});
}

void Crowd::startPath(Agent& a, Vec2 destination, AgentState state) {
    a.destination = destination;
    a.state = state;
    a.path.clear();
    a.pathIndex = 0;
    a.needsPath = true;
}

void Crowd::chooseDestination(Agent& a) {
    const ScheduleEntry* entry = a.entry;
    const Activity activity = entry ? entry->activity : Activity::Walk;
    std::string category = entry && !entry->placeCategory.empty() ? entry->placeCategory : defaultPlaceCategory(activity);
    Vec2 dest = a.pos;
    bool chosen = false;
    if (activity == Activity::Home || activity == Activity::Sleep || category == "home") {
        dest = a.home;
        chosen = true;
    } else if (!category.empty()) {
        // Из нескольких ближайших мест нужной категории выбираем случайное.
        std::vector<std::pair<float, std::size_t>> near;
        for (std::size_t i = 0; i < pois_.size(); ++i)
            if (pois_[i].category == category) near.push_back({distance(pois_[i].pos, a.pos), i});
        if (!near.empty()) {
            const std::size_t k = std::min<std::size_t>(4, near.size());
            std::partial_sort(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(k), near.end());
            dest = pois_[near[static_cast<std::size_t>(rand01() * static_cast<float>(k)) % k].second].pos;
            chosen = true;
        }
    }
    if (!chosen) {  // прогулка: случайная проходимая точка поблизости
        for (int attempt = 0; attempt < 8 && !chosen; ++attempt) {
            const float ang = rand01() * 6.2831853f, r = randRange(30.f, 120.f);
            Vec2 p{a.pos.x + std::sin(ang) * r, a.pos.z - std::cos(ang) * r}, snapped;
            if (grid_.nearestWalkable(p, 10.f, snapped)) {
                dest = snapped;
                chosen = true;
            }
        }
    }
    a.activity = activity;
    if (distance(dest, a.pos) < 3.f) {
        float lo, hi;
        waitRange(activity, lo, hi);
        a.state = AgentState::Waiting;
        a.timer = randRange(lo, hi);
        a.needsPath = false;
        a.path.clear();
        return;
    }
    startPath(a, dest, AgentState::Walking);
}

void Crowd::follow(Agent& a, float dt, bool collide, Vec2 push) {
    float remaining = a.archetype->walkSpeed * a.speedScale * dt;
    Vec2 dir = normalized(a.vel);
    while (remaining > 0.f && a.pathIndex < a.path.size()) {
        const Vec2 d = a.path[a.pathIndex] - a.pos;
        const float dist = length(d);
        if (dist <= remaining) {
            a.pos = a.path[a.pathIndex];
            remaining -= dist;
            ++a.pathIndex;
            if (dist > 1e-4f) dir = d * (1.f / dist);
        } else {
            dir = d * (1.f / dist);
            a.pos = a.pos + dir * remaining;
            remaining = 0.f;
        }
    }
    a.vel = dir * (a.archetype->walkSpeed * a.speedScale);
    if (length(dir) > 0.f) {
        // Поворот к направлению движения плавно (до ~360°/с), чтобы тело не дёргалось на углах пути.
        float target = headingOf(dir), delta = target - a.heading;
        while (delta > 3.1415926f) delta -= 6.2831853f;
        while (delta < -3.1415926f) delta += 6.2831853f;
        a.heading += std::clamp(delta, -6.2831853f * dt, 6.2831853f * dt);
    }
    if (collide && (push.x != 0.f || push.z != 0.f)) {
        const Vec2 moved = a.pos + push * dt;
        if (grid_.walkable(moved.x, moved.z) && grid_.lineOfSight(a.pos, moved)) a.pos = moved;
    }
}

void Crowd::applyReaction(Agent& a, const WorldEvent& ev) {
    if (a.reactCooldown > 0.f) return;
    const Reaction r = a.archetype->reactionTo(ev.type);
    if (r == Reaction::Ignore) return;
    const float d = distance(a.pos, ev.pos);
    // Дальние от события замечают его не всегда: вероятность падает с расстоянием.
    if (rand01() > 1.f - 0.6f * (d / std::max(ev.radius, 1.f))) return;
    a.reactCooldown = 12.f;
    a.eventPos = ev.pos;
    a.entry = nullptr;  // после реакции расписание выберется заново
    a.lineCooldown = 0.f;
    switch (r) {
        case Reaction::Flee: {
            Vec2 away = normalized(a.pos - ev.pos);
            if (length(away) == 0.f) away = {std::sin(rand01() * 6.28f), -std::cos(rand01() * 6.28f)};
            Vec2 target, snapped;
            target = a.pos + away * randRange(70.f, 110.f);
            if (!grid_.nearestWalkable(target, 25.f, snapped)) snapped = a.home;
            a.speedScale = settings_.fleeSpeedScale;
            a.timer = 25.f;  // предел бегства
            startPath(a, snapped, AgentState::Fleeing);
            break;
        }
        case Reaction::Watch:
            a.state = AgentState::Watching;
            a.timer = randRange(6.f, 15.f);
            a.path.clear();
            a.needsPath = false;
            break;
        case Reaction::Film:
            a.state = AgentState::Filming;
            a.timer = randRange(8.f, 16.f);
            a.path.clear();
            a.needsPath = false;
            events_.push_back({CrowdEvent::Type::Filmed, a.id, a.pos});
            break;
        case Reaction::CallPolice:
            a.state = AgentState::Calling;
            a.timer = randRange(3.f, 6.f);
            a.path.clear();
            a.needsPath = false;
            break;
        case Reaction::Intervene: {
            Vec2 snapped;
            if (grid_.nearestWalkable(ev.pos, 15.f, snapped) && distance(snapped, a.pos) > 3.f) startPath(a, snapped, AgentState::Intervening);
            else {
                a.state = AgentState::Watching;
                a.timer = 8.f;
            }
            break;
        }
        case Reaction::Ignore: break;
    }
    say(a, std::string("people.react.") + reactionName(r));
}

void Crowd::postEvent(const WorldEvent& ev) {
    for (Agent& a : agents_)
        if (distance(a.pos, ev.pos) <= ev.radius) applyReaction(a, ev);
}

void Crowd::tickAgent(Agent& a, float dt, bool separate) {
    a.lineCooldown = std::max(0.f, a.lineCooldown - dt);
    a.reactCooldown = std::max(0.f, a.reactCooldown - dt);

    // Расписание: сменилась запись — идём к новому занятию (реакции на события расписание не трогает).
    if (!reacting(a.state)) {
        const ScheduleEntry* e = a.archetype->entryAt(hour_);
        if (e != a.entry) {
            a.entry = e;
            a.state = AgentState::Idle;
            a.needsPath = false;
            a.path.clear();
        }
    }
    switch (a.state) {
        case AgentState::Idle: chooseDestination(a); break;
        case AgentState::Waiting:
            a.timer -= dt;
            a.vel = {};
            if (a.timer <= 0.f) {
                // Дела на месте закончены: у «домашних» занятий остаёмся (новый выбор даст ту же точку и снова ожидание).
                a.state = AgentState::Idle;
            }
            break;
        case AgentState::Walking:
        case AgentState::Fleeing:
        case AgentState::Intervening: {
            if (a.needsPath) break;  // ждём очередь поиска пути
            follow(a, dt, separate, {});  // отталкивание соседей добавляет update для ближних агентов
            if (a.state == AgentState::Fleeing) {
                a.timer -= dt;
                if (a.timer <= 0.f) a.pathIndex = a.path.size();  // выдохлись
            }
            if (a.pathIndex >= a.path.size()) {
                a.path.clear();
                a.vel = {};
                if (a.state == AgentState::Fleeing) {
                    a.speedScale = 1.f;
                    a.state = AgentState::Waiting;
                    a.timer = 3.f;
                } else if (a.state == AgentState::Intervening) {
                    a.state = AgentState::Watching;
                    a.timer = 8.f;
                } else {
                    float lo, hi;
                    waitRange(a.activity, lo, hi);
                    a.state = AgentState::Waiting;
                    a.timer = randRange(lo, hi);
                }
            }
            break;
        }
        case AgentState::Watching:
        case AgentState::Filming:
        case AgentState::Calling:
            a.vel = {};
            a.heading = headingOf(a.eventPos - a.pos);
            a.timer -= dt;
            if (a.timer <= 0.f) {
                if (a.state == AgentState::Calling) events_.push_back({CrowdEvent::Type::PoliceCalled, a.id, a.pos});
                a.state = AgentState::Idle;
                a.entry = nullptr;
            }
            break;
    }
}

void Crowd::update(float dt, Vec2 camera) {
    ++frame_;
    stats_ = {};
    stats_.agents = static_cast<int>(agents_.size());
    const float nearR2 = settings_.nearRadius * settings_.nearRadius, midR2 = settings_.midRadius * settings_.midRadius, farR2 = settings_.farRadius * settings_.farRadius;

    // 1. Уровень детализации и расписание тактов.
    std::vector<Agent*> due;
    due.reserve(agents_.size());
    for (Agent& a : agents_) {
        const Vec2 d = a.pos - camera;
        const float d2 = d.x * d.x + d.z * d.z;
        a.lod = d2 <= nearR2 ? LodTier::Near : (d2 <= midR2 ? LodTier::Mid : (d2 <= farR2 ? LodTier::Far : LodTier::Dormant));
        a.accum += dt;
        const std::uint64_t id = a.id;
        bool tick = false;
        switch (a.lod) {
            case LodTier::Near: tick = true; ++stats_.near; break;
            case LodTier::Mid: tick = (frame_ + id) % 4 == 0; ++stats_.mid; break;
            case LodTier::Far: tick = (frame_ + id) % 30 == 0; ++stats_.far; break;
            case LodTier::Dormant: tick = (frame_ + id) % 120 == 0; ++stats_.dormant; break;
        }
        if (tick) due.push_back(&a);
    }

    // 2. Ближние агенты: отталкивание друг от друга (сетка соседства 2 м).
    std::unordered_map<std::int64_t, std::vector<Agent*>> cells;
    const auto key = [](int x, int z) { return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(z); };
    for (Agent& a : agents_)
        if (a.lod == LodTier::Near) cells[key(static_cast<int>(std::floor(a.pos.x / 2.f)), static_cast<int>(std::floor(a.pos.z / 2.f)))].push_back(&a);

    // 3. Очередь поиска путей: не больше maxPathQueriesPerFrame за кадр, по кругу для справедливости.
    for (const Agent& a : agents_) stats_.pendingPaths += a.needsPath ? 1 : 0;
    int budget = settings_.maxPathQueriesPerFrame;
    if (!agents_.empty() && stats_.pendingPaths > 0) {
        for (std::size_t n = 0; n < agents_.size() && budget > 0; ++n) {
            Agent& a = agents_[(pathCursor_ + n) % agents_.size()];
            if (!a.needsPath) continue;
            --budget;
            ++stats_.pathQueries;
            a.needsPath = false;
            if (grid_.findPath(a.pos, a.destination, a.path) && a.path.size() >= 2) {
                a.pathIndex = 1;
            } else {
                ++stats_.pathFailures;
                a.path.clear();
                a.state = AgentState::Waiting;  // пути нет: подождём и выберем другое место
                a.timer = randRange(4.f, 10.f);
                a.speedScale = 1.f;
            }
        }
        pathCursor_ = (pathCursor_ + 1) % agents_.size();
    }

    // 4. Такт агентов своего уровня.
    for (Agent* a : due) {
        const float step = a->accum;
        a->accum = 0.f;
        const bool near = a->lod == LodTier::Near;
        if (near && movingState(a->state) && !a->needsPath) {
            // Отталкивание: сумма сил от соседей ближе 1 м.
            Vec2 push{};
            const int cx = static_cast<int>(std::floor(a->pos.x / 2.f)), cz = static_cast<int>(std::floor(a->pos.z / 2.f));
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto it = cells.find(key(cx + dx, cz + dz));
                    if (it == cells.end()) continue;
                    for (const Agent* b : it->second) {
                        if (b == a) continue;
                        const Vec2 d = a->pos - b->pos;
                        const float dist = length(d);
                        if (dist < 1.f && dist > 1e-4f) push = push + d * ((1.f - dist) / dist * 1.5f);
                        else if (dist <= 1e-4f) push = push + Vec2{0.3f, 0.f};
                    }
                }
            const Vec2 before = a->pos;
            tickAgent(*a, step, false);
            if (movingState(a->state) || a->state == AgentState::Waiting) {
                const Vec2 moved = a->pos + push * step;
                if ((push.x != 0.f || push.z != 0.f) && grid_.walkable(moved.x, moved.z) && grid_.lineOfSight(a->pos, moved)) a->pos = moved;
            }
            (void)before;
        } else {
            tickAgent(*a, step, false);
        }
        // Редкие реплики ближних прохожих (раз в ~50 с на человека).
        if (near && !a->archetype->lineKeys.empty() && a->lineCooldown <= 0.f && rand01() < 0.0003f * step * 60.f && a->state != AgentState::Idle) {
            const auto& keys = a->archetype->lineKeys;
            say(*a, keys[static_cast<std::size_t>(rand01() * static_cast<float>(keys.size())) % keys.size()]);
            a->lineCooldown = 30.f;
        }
    }
    for (const Agent& a : agents_) stats_.moving += movingState(a.state) ? 1 : 0;
}

std::vector<Utterance> Crowd::drainUtterances() {
    std::vector<Utterance> out;
    out.swap(utterances_);
    return out;
}

std::vector<CrowdEvent> Crowd::drainEvents() {
    std::vector<CrowdEvent> out;
    out.swap(events_);
    return out;
}
}  // namespace han::ai
