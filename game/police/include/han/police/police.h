// han/police/police.h — полиция и розыск (G8): преступления копят «жар», жар даёт звёзды, патрули и машины ищут героя в зоне поиска,
// камеры и офицеры его замечают, потеряли — зона сужается, жар остывает; поймали — «задержан», ушёл — «оторвался».
// Всё — кинематика на навигационной сетке (пешие) и дорожном графе (машины). Тексты — ключи локализации (police.*), решения о наказании —
// у игрового слоя (B). Контракт: IWorldServices::wantedLevel()/addCrime() — тонкая обёртка над этим классом.
#pragma once
#include <cstdint>
#include <random>
#include <vector>

#include "han/ai/navgrid.h"
#include "han/ai/traffic.h"

namespace han::police {
using ai::Vec2;
const char* moduleName();

// Коды совпадают с contracts::IWorldServices::addCrime(crimeType).
enum class Crime : std::uint32_t {
    Assault = 1,         // драка с прохожим
    AssaultOfficer = 2,  // нападение на полицейского
    Vandalism = 3,
    Theft = 4,
    CarTheft = 5,
    HitAndRun = 6,
    Speeding = 7,
    Murder = 8,
    Trespass = 9,        // нарушение в святом месте
};

struct CrimeRule {
    float heat = 0.f;            // сколько «жара» добавляет
    float witnessRadius = 25.f;  // кто увидит: полицейский или камера ближе этого расстояния
    bool needsWitness = true;    // без свидетеля преступление не засчитывается
};
CrimeRule ruleOf(Crime c);

struct Camera {
    Vec2 pos;
    float yaw = 0.f;       // радианы: 0 — на север (-Z), положительные — к востоку (+X)
    float fovDeg = 70.f;   // полный угол обзора
    float range = 55.f;
};

struct Settings {
    float levelHeat[6] = {0.f, 10.f, 30.f, 60.f, 100.f, 160.f};  // жар, с которого начинается 0..5 звёзд
    float graceSeconds = 25.f;       // после потери героя жар не остывает столько секунд
    float decayPerSecond = 2.5f;     // скорость остывания, жар/с
    float decayInsideZoneFactor = 0.35f; // внутри зоны поиска жар остывает медленнее (патрули ещё ищут рядом)
    float decayOutsideZoneBoost = 2.5f;  // вне зоны поиска — быстрее
    float graceSecondsPerStar = 8.f;     // к льготному времени добавляется по столько секунд за каждую звезду
    float searchRadiusBase = 70.f;   // радиус зоны поиска на 1 звезде; каждая следующая +25 м
    float searchShrinkPerSecond = 1.5f;
    float officerSight = 40.f;
    float arrestRadius = 1.6f;
    float footSpeed = 5.2f;          // м/с, бег офицера
    float carSpeed = 14.f;
    float minSpawnDistance = 90.f;   // подкрепление появляется не ближе этого расстояния от героя и вне линии взгляда
    float repathSeconds = 1.2f;
    float leaveSeconds = 30.f;       // сколько после «оторвался» ещё видны уходящие патрули
};

enum class UnitKind : std::uint8_t { Foot, Car };
enum class UnitMode : std::uint8_t { Respond, Chase, Search, Leave };

struct Unit {
    std::uint32_t id = 0;
    UnitKind kind = UnitKind::Foot;
    UnitMode mode = UnitMode::Respond;
    Vec2 pos;
    float heading = 0.f;
    std::vector<Vec2> path;
    std::size_t pathIndex = 0;
    float repath = 0.f, wait = 0.f, ttl = 0.f;
    bool deployed = false;       // машина уже высадила офицера
    bool seesTarget = false;
    Vec2 home;                   // станция, куда уходит при Leave
};

enum class EventType : std::uint8_t { CrimeCounted, LevelChanged, Spotted, LostPlayer, Escaped, Busted, UnitSpawned };
struct Event {
    EventType type = EventType::LevelChanged;
    int level = 0;
    Vec2 pos;
    Crime crime = Crime::Assault;
};

class PoliceSystem {
public:
    // nav — для проходимости и линии взгляда, roads — для машин (оба могут быть nullptr: тогда только пешие без путей / без машин).
    PoliceSystem(const ai::NavGrid* nav, const ai::RoadGraph* roads, const Settings& settings = {}, std::uint32_t seed = 1);

    void addStation(Vec2 pos) { stations_.push_back(pos); }
    void addCamera(const Camera& c) { cameras_.push_back(c); }

    // Преступление героя. witnessed — видел ли кто-то посторонний (толпа, E11); полицейские и камеры проверяются здесь.
    // Возвращает true, если преступление засчитано.
    bool addCrime(Crime crime, Vec2 where, bool witnessed = false);
    // Шаг: пока сидит вызов, 0 звёзд — ничего не происходит. playerPos — положение героя.
    void update(float dt, Vec2 playerPos);

    int level() const { return level_; }
    float heat() const { return heat_; }
    bool busted() const { return busted_; }
    bool playerSeen() const { return seen_; }
    Vec2 lastKnown() const { return lastKnown_; }
    float searchRadius() const { return searchRadius_; }
    float unseenFor() const { return unseen_; }
    const std::vector<Unit>& units() const { return units_; }
    const std::vector<Camera>& cameras() const { return cameras_; }
    std::vector<Event> takeEvents();
    // Сброс после «задержан» или загрузки.
    void clear();

private:
    bool sees(Vec2 from, Vec2 to, float range) const;
    bool cameraSees(const Camera& c, Vec2 target) const;
    int levelForHeat(float heat) const;
    void dispatch(Vec2 player);
    void spawnUnit(UnitKind kind, Vec2 player);
    void moveAlong(Unit& u, float speed, float dt);
    void planFoot(Unit& u, Vec2 target);
    void planCar(Unit& u, Vec2 target);
    void updateUnit(Unit& u, float dt, Vec2 player);
    void push(EventType t, int level, Vec2 pos, Crime crime = Crime::Assault) { events_.push_back({t, level, pos, crime}); }

    const ai::NavGrid* nav_;
    const ai::RoadGraph* roads_;
    Settings cfg_;
    std::mt19937 rng_;
    std::vector<Vec2> stations_;
    std::vector<Camera> cameras_;
    std::vector<Unit> units_;
    std::vector<Event> events_;
    int level_ = 0;
    float heat_ = 0.f;
    bool seen_ = false, busted_ = false;
    Vec2 lastKnown_;
    float searchRadius_ = 0.f, unseen_ = 0.f, sinceEscape_ = 1e9f;
    std::uint32_t nextId_ = 1;
    float dispatchTimer_ = 0.f;
};

}  // namespace han::police
// Пример: PoliceSystem p(&nav, &roads); p.addStation({0, 0}); p.addCrime(Crime::Assault, pos, true); p.update(dt, playerPos); if (p.busted()) ...
