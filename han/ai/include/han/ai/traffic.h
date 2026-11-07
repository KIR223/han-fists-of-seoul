// han/ai/traffic.h — трафик города (G7): дорожный граф из OpenStreetMap и машины, которые едут по нему по правилам правостороннего движения.
// Машины кинематические (без физики): следование за лидером по модели IDM, перекрёстки пропускают по одной машине (очередь по прибытию),
// развороты — только в тупике. Физические машины (han_physics) заводятся только рядом с игроком; здесь они едут «издали».
// Данные OSM — только геометрия и классы дорог. Скорости — по классам (городские лимиты Кореи: 50 км/ч, жилые зоны 30, магистрали 80).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "han/ai/navgrid.h"
#include "han/world/osm.h"
#include "han/world/road_profile.h"

namespace han::ai {

enum class RoadClass : std::uint8_t { Motorway, Primary, Secondary, Local };

struct RoadEdge {
    int from = -1, to = -1;
    std::vector<Vec2> pts;       // ось дороги по направлению движения
    std::vector<float> cum;      // накопленная длина до каждой точки
    float length = 0.f;
    float speed = 13.f;          // м/с, ограничение
    int lanes = 1;               // полос в этом направлении
    float laneWidth = 3.2f;
    float centerOffset = 0.f;  // половина разделительной полосы (метры), если она есть в данных дороги
    RoadClass cls = RoadClass::Local;
    int reverse = -1;            // встречное направление той же дороги (или -1: односторонняя)
    std::vector<float> ys;       // абсолютная высота полотна в точках pts (мост или эстакада); пусто — дорога идёт по рельефу

    // Точка и направление на расстоянии s от начала; offset — смещение вправо от оси (правостороннее движение).
    Vec2 pointAt(float s, float offset = 0.f) const;
    Vec2 directionAt(float s) const;
    // Высота полотна на расстоянии s от начала; NaN — дорога идёт по рельефу (высоту берёт вызывающий из карты высот).
    float surfaceAt(float s) const;
};

struct RoadNode {
    Vec2 pos;
    std::vector<int> out, in;
    // Перекрёсток: есть выбор или слияние (больше одной входящей или исходящей дороги, не считая встречной пары одной дороги).
    bool junction = false;
};

struct RoadGraphSettings {
    float minEdgeLength = 3.f;  // короче — склеиваем с соседним узлом (шум данных)
    const world::RoadProfile* profile = nullptr;  // профиль мостов: рёбра на мосту получают высоту полотна (тот же, что у геометрии города)
};

class RoadGraph {
public:
    // Строит граф по линиям highway из OSM (дороги для машин; пешеходные, лестницы, велодорожки и «proposed» пропускаются).
    static RoadGraph build(const world::OsmData& data, const world::GeoProjection& projection, const RoadGraphSettings& settings = {});
    // Граф из готовых ломаных (для тестов и сценариев): направление, ширина полосы по умолчанию; oneway — одно направление.
    struct Line {
        std::vector<Vec2> pts;
        RoadClass cls = RoadClass::Local;
        bool oneway = false;
        int lanesPerDirection = 1;
    };
    static RoadGraph fromLines(const std::vector<Line>& lines);

    const std::vector<RoadNode>& nodes() const { return nodes_; }
    const std::vector<RoadEdge>& edges() const { return edges_; }
    int nearestNode(Vec2 p) const;
    float totalLength() const;  // сумма длин направленных дорог, м
    // Кратчайший по времени путь между узлами: индексы дорог от from до to (пусто — пути нет или from == to).
    std::vector<int> shortestPath(int fromNode, int toNode) const;

private:
    std::vector<RoadNode> nodes_;
    std::vector<RoadEdge> edges_;
    void finish();
    friend class TrafficSim;
};

struct Car {
    std::uint32_t id = 0;
    int edge = -1, lane = 0;
    float s = 0.f, v = 0.f, v0 = 13.f;  // положение по дороге, скорость, желаемая скорость
    float length = 4.5f;
    int next = -1;                      // следующая дорога (выбирается заранее)
    int prev = -1, prevLane = 0;        // дорога и полоса, с которых машина только что съехала (для плавной дуги через перекрёсток)
    std::vector<int> heldNodes;         // перекрёстки, которые машина сейчас занимает (на коротких дорогах их может быть два)
    float stoppedFor = 0.f;             // сколько секунд почти стоит (сторожевой таймер)
    Vec2 pos;                           // положение в мире (обновляется каждый шаг)
    // Высота полотна под передней и задней осью на мосту (по оси дороги); NaN — по рельефу. По ним вызывающий считает шаг корпуса.
    float surfaceYFront = std::numeric_limits<float>::quiet_NaN(), surfaceYRear = std::numeric_limits<float>::quiet_NaN();
    float surfaceY = std::numeric_limits<float>::quiet_NaN();  // высота полотна под машиной на мосту; NaN — по рельефу
    float heading = 0.f;                // радианы: 0 — на север (-Z), положительные — к востоку (+X)
};

struct TrafficSettings {
    int maxCars = 120;
    float carLength = 4.5f;
    float wheelbase = 2.6f;              // расстояние между осями, м: по нему считаются высоты полотна под осями на мосту
    float minGap = 2.0f, timeHeadway = 1.4f, maxAccel = 2.2f, comfortBrake = 3.2f;
    float stuckSeconds = 40.f;           // машина почти стоит дольше — убирается и появляется в другом месте
    // Политика появления и удаления (этап 06). Значения по умолчанию сохраняют прежнее поведение: появляться где угодно в радиусе, удаляться за despawnRadius.
    float spawnMinDistanceInView = 0.f;   // > 0 — в конусе зрения (и ближе 25 м) машины не появляются ближе этого расстояния
    float spawnMinDistanceBehind = 0.f;   // > 0 — не появляться ближе этого к камере в любом месте
    float despawnInViewRadius = 0.f;      // > 0 — видимая машина удаляется только за этим расстоянием (а не за despawnRadius)
    float viewHalfAngleDegrees = 62.f;    // половина угла конуса зрения
    float spawnGap = 18.f;                // свободное место на полосе вокруг точки появления, м
    bool weightByLength = false;          // выбор дороги для появления пропорционален длине и весу класса (иначе равновероятно по дорогам)
    float classWeight[4] = {1.f, 1.f, 1.f, 1.f};  // вес по RoadClass: Motorway, Primary, Secondary, Local
    bool smoothTurns = true;             // дуга Безье через перекрёсток и полоса по типу поворота; false — прежний рывок на смещение полосы
    bool turnLanes = false;              // правый поворот — в крайнюю правую полосу, левый — в ближайшую к оси; false (по умолчанию) — полоса по номеру:
                                         // на реальной карте крайние полосы вплотную к зданиям, аудит показал рост наездов на здания в 1,8 раза
    float connectorRadius = 0.f;         // половина длины дуги, м; 0 — по ширине полос соединяемых дорог
    float spawnRadius = 0.f;             // > 0 — появляться только вокруг точки фокуса (игрока) в этом радиусе
    float despawnRadius = 0.f;           // > 0 — убирать машины дальше этого расстояния от фокуса
    std::uint32_t seed = 1;
};

struct TrafficStats {
    std::uint64_t spawned = 0, despawned = 0, stuckResets = 0, reachedEnd = 0;
    float meanSpeed = 0.f;               // средняя скорость машин сейчас, м/с
    int moving = 0, waiting = 0;
    std::vector<std::string> stuckNotes; // подробности первых застрявших машин (отладка)
    std::vector<int> stuckEdges;         // дороги, на которых сбрасывались застрявшие машины (до 200; для отладки)
};

class TrafficSim {
public:
    TrafficSim(const RoadGraph& graph, const TrafficSettings& settings = {});

    void setFocus(Vec2 p) { focus_ = p; }
    // Направление взгляда (единичный вектор; нулевой — неизвестно, всё вокруг считается видимым): появление и удаление машин не на глазах.
    void setView(Vec2 direction) { view_ = direction; }
    // Доля от maxCars, которая должна быть на дорогах сейчас (кривая по часу суток из данных); лишние машины уходят, когда они вне взгляда.
    void setDensityScale(float scale) { densityScale_ = std::clamp(scale, 0.f, 4.f); }
    // Шаг симуляции; dt до 0,25 с (больше разбивается).
    void update(float dt);
    const std::vector<Car>& cars() const { return cars_; }
    const TrafficStats& stats() const { return stats_; }
    // Ближайшая к точке машина в радиусе (для реакций на драку и угона); nullptr — нет.
    const Car* nearestCar(Vec2 p, float radius) const;
    // Проверка инварианта: машины в одной полосе не пересекаются. Возвращает число нарушений.
    int overlaps() const;

private:
    struct CarPose {
        Vec2 pos;
        float heading = 0.f, surfaceY = std::numeric_limits<float>::quiet_NaN();
    };
    CarPose poseOf(const Car& c) const;
    int laneOnto(int lane, int fromEdge, int toEdge) const;

    struct Lock {
        int holder = -1;                    // индекс машины в cars_ (по id), -1 — свободен
        std::vector<std::uint32_t> queue;   // id машин в порядке прибытия
    };

    void step(float dt);
    void spawnCars();
    void removeCar(std::size_t index);
    int pickNext(const Car& c, int edge);
    float gapAhead(const Car& c, float& leaderSpeed) const;
    bool canEnter(const Car& c) const;

    const RoadGraph& g_;
    TrafficSettings cfg_;
    std::vector<Car> cars_;
    std::vector<Lock> locks_;                           // по узлам
    std::vector<std::vector<std::vector<int>>> lanes_;  // edge → lane → индексы машин, спереди назад (пересобирается каждый шаг)
    std::mt19937 rng_;
    Vec2 focus_;
    Vec2 view_;
    float densityScale_ = 1.f;
    std::vector<float> spawnCumulative_;  // накопленные веса кандидатов (weightByLength)
    bool visibleFromFocus(Vec2 p) const;
    int targetCars() const { return std::max(0, static_cast<int>(std::lround(static_cast<float>(cfg_.maxCars) * densityScale_))); }
    Vec2 spawnFocus_;
    bool spawnCandidatesReady_ = false;
    std::vector<std::size_t> spawnCandidates_;
    std::uint32_t nextId_ = 1;
    TrafficStats stats_;
};

}  // namespace han::ai
// Пример: auto g = RoadGraph::build(osm, proj); TrafficSim t(g, {.maxCars = 100}); for (;;) { t.update(1.f / 60.f); draw(t.cars()); }
