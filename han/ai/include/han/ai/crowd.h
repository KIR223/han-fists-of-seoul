// han/ai/crowd.h — толпа города (E11): прохожие по расписанию, уровни детализации (LOD), реакции на события мира, реплики.
// Агенты ходят по NavGrid. Чем дальше от камеры, тем реже обновляются: Near — каждый кадр с отталкиванием друг от друга,
// Mid — раз в 4 кадра, Far — раз в ~0,5 с, Dormant — раз в ~2 с (только вдоль пути, без столкновений).
// Тексты — только ключи локализации (people.react.*, people.line.*); их показывает и озвучивает игровой слой.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "han/ai/archetype.h"
#include "han/ai/navgrid.h"

namespace han::ai {
using AgentId = std::uint32_t;  // 0 — «нет»

enum class AgentState : std::uint8_t { Idle, Walking, Waiting, Fleeing, Watching, Filming, Calling, Intervening };
enum class LodTier : std::uint8_t { Near, Mid, Far, Dormant };

struct Poi {
    Vec2 pos;
    std::string category;  // "work", "cafe", "shop", "eat", "park", "campus", "market", …
};

struct WorldEvent {
    WorldEventType type = WorldEventType::Fight;
    Vec2 pos;
    float radius = 40.f;  // кого касается: слышимость и видимость события
};

struct Agent {
    AgentId id = 0;
    const Archetype* archetype = nullptr;
    Vec2 pos, vel, home, destination, eventPos;
    float heading = 0;  // радианы; 0 — на север (-Z), положительные — к востоку (+X)
    AgentState state = AgentState::Idle;
    Activity activity = Activity::Walk;
    LodTier lod = LodTier::Dormant;
    std::vector<Vec2> path;
    std::size_t pathIndex = 0;
    float timer = 0, accum = 0, speedScale = 1.f, lineCooldown = 0, reactCooldown = 0;
    const ScheduleEntry* entry = nullptr;  // запись расписания, по которой агент живёт сейчас
    bool needsPath = false;
};

struct Utterance {
    AgentId agent = 0;
    std::string key;  // ключ локализации
    Vec2 pos;
};

// События, которые толпа отдаёт игре (полиция, розыск, свидетели).
struct CrowdEvent {
    enum class Type : std::uint8_t { PoliceCalled, Filmed } type = Type::PoliceCalled;
    AgentId agent = 0;
    Vec2 pos;
};

struct CrowdSettings {
    std::size_t maxAgents = 1000;
    float nearRadius = 60.f, midRadius = 150.f, farRadius = 300.f;
    float agentRadius = 0.35f;
    int maxPathQueriesPerFrame = 4;
    std::uint32_t seed = 12345;
    float fleeSpeedScale = 1.9f;
};

struct CrowdStats {
    int agents = 0, near = 0, mid = 0, far = 0, dormant = 0;
    int pathQueries = 0, pathFailures = 0, pendingPaths = 0;
    int moving = 0;  // сколько агентов сейчас в пути (Walking, Fleeing, Intervening)
};

class Crowd {
public:
    Crowd(const NavGrid& grid, std::vector<Poi> pois, const CrowdSettings& settings = {});

    // Создаёт прохожего в ближайшей проходимой точке (дом — эта точка). 0 — не удалось (нет места, лимит агентов).
    // Архетип должен пережить толпу и не двигаться в памяти: толпа хранит на него указатель (держите ArchetypeLibrary
    // живой и не добавляйте в неё архетипы после создания агентов).
    AgentId spawn(const Archetype& archetype, Vec2 pos);
    // n прохожих в случайных проходимых местах карты, архетипы по кругу из списка (указатели на элементы библиотеки).
    std::size_t populate(const std::vector<const Archetype*>& archetypes, std::size_t n);

    void setHour(float hour) { hour_ = hour; }
    float hour() const { return hour_; }
    void postEvent(const WorldEvent& event);
    // dt — секунды игрового времени; camera — точка, от которой считаются уровни детализации.
    void update(float dt, Vec2 camera);

    const std::vector<Agent>& agents() const { return agents_; }
    const Agent* agent(AgentId id) const { return id >= 1 && id <= agents_.size() ? &agents_[id - 1] : nullptr; }
    const CrowdStats& stats() const { return stats_; }
    std::vector<Utterance> drainUtterances();
    std::vector<CrowdEvent> drainEvents();

private:
    const NavGrid& grid_;
    std::vector<Poi> pois_;
    CrowdSettings settings_;
    std::vector<Agent> agents_;
    float hour_ = 12.f;
    std::uint32_t rng_;
    std::uint64_t frame_ = 0;
    std::size_t pathCursor_ = 0;
    CrowdStats stats_;
    std::vector<Utterance> utterances_;
    std::vector<CrowdEvent> events_;

    float rand01();
    float randRange(float a, float b) { return a + (b - a) * rand01(); }
    void chooseDestination(Agent& a);
    void tickAgent(Agent& a, float dt, bool separate);
    void follow(Agent& a, float dt, bool collide, Vec2 push);
    void say(Agent& a, const std::string& key);
    void applyReaction(Agent& a, const WorldEvent& ev);
    void startPath(Agent& a, Vec2 destination, AgentState state);
};
}  // namespace han::ai
// Пример: Crowd crowd(grid, pois); crowd.populate(lib.all(), 500); crowd.setHour(9.f); crowd.update(dt, cameraXZ); crowd.postEvent({WorldEventType::Fight, p, 40});
