// han/ai/traffic.h — трафик города (G7): дорожный граф из OpenStreetMap и машины, которые едут по нему по правилам правостороннего движения.
// Машины кинематические (без физики): следование за лидером по модели IDM, перекрёстки пропускают по одной машине (очередь по прибытию),
// развороты — только в тупике. Физические машины (han_physics) заводятся только рядом с игроком; здесь они едут «издали».
// Данные OSM — только геометрия и классы дорог. Скорости — по классам (городские лимиты Кореи: 50 км/ч, жилые зоны 30, магистрали 80).
#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "han/ai/navgrid.h"
#include "han/world/osm.h"

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
    RoadClass cls = RoadClass::Local;
    int reverse = -1;            // встречное направление той же дороги (или -1: односторонняя)

    // Точка и направление на расстоянии s от начала; offset — смещение вправо от оси (правостороннее движение).
    Vec2 pointAt(float s, float offset = 0.f) const;
    Vec2 directionAt(float s) const;
};

struct RoadNode {
    Vec2 pos;
    std::vector<int> out, in;
    // Перекрёсток: есть выбор или слияние (больше одной входящей или исходящей дороги, не считая встречной пары одной дороги).
    bool junction = false;
};

struct RoadGraphSettings {
    float minEdgeLength = 3.f;  // короче — склеиваем с соседним узлом (шум данных)
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
    std::vector<int> heldNodes;         // перекрёстки, которые машина сейчас занимает (на коротких дорогах их может быть два)
    float stoppedFor = 0.f;             // сколько секунд почти стоит (сторожевой таймер)
    Vec2 pos;                           // положение в мире (обновляется каждый шаг)
    float heading = 0.f;                // радианы: 0 — на север (-Z), положительные — к востоку (+X)
};

struct TrafficSettings {
    int maxCars = 120;
    float carLength = 4.5f;
    float minGap = 2.0f, timeHeadway = 1.4f, maxAccel = 2.2f, comfortBrake = 3.2f;
    float stuckSeconds = 40.f;           // машина почти стоит дольше — убирается и появляется в другом месте
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
    // Шаг симуляции; dt до 0,25 с (больше разбивается).
    void update(float dt);
    const std::vector<Car>& cars() const { return cars_; }
    const TrafficStats& stats() const { return stats_; }
    // Ближайшая к точке машина в радиусе (для реакций на драку и угона); nullptr — нет.
    const Car* nearestCar(Vec2 p, float radius) const;
    // Проверка инварианта: машины в одной полосе не пересекаются. Возвращает число нарушений.
    int overlaps() const;

private:
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
    std::uint32_t nextId_ = 1;
    TrafficStats stats_;
};

}  // namespace han::ai
// Пример: auto g = RoadGraph::build(osm, proj); TrafficSim t(g, {.maxCars = 100}); for (;;) { t.update(1.f / 60.f); draw(t.cars()); }
