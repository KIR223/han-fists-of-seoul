// han/world/terrain_physics.h — физика рельефа (E17): плитки сетки высот Jolt вокруг игрока создаются и убираются по мере движения.
// Плитка — 64 x 64 отсчёта с шагом 12,5 м (787,5 м), соседние плитки перекрываются одним отсчётом, поэтому швов нет.
#pragma once
#include <map>

#include "han/physics/physics_world.h"
#include "han/world/terrain.h"

namespace han::world {

class TerrainPhysics {
public:
    struct Settings {
        int samples = 64;           // отсчётов на плитку по стороне (чётное)
        float cell = 12.5f;         // шаг отсчётов, м
        float loadRadius = 1200.f;  // плитки ближе — создаются
        float unloadRadius = 1900.f;  // дальше — убираются
        contracts::SurfaceMaterial material = contracts::SurfaceMaterial::Concrete;
    };
    TerrainPhysics(physics::PhysicsWorld& world, const Heightmap& map) : world_(world), map_(map) {}
    TerrainPhysics(physics::PhysicsWorld& world, const Heightmap& map, const Settings& settings) : world_(world), map_(map), cfg_(settings) {}
    ~TerrainPhysics() { clear(); }
    TerrainPhysics(const TerrainPhysics&) = delete;
    TerrainPhysics& operator=(const TerrainPhysics&) = delete;

    // Создаёт плитки рядом с точкой и убирает далёкие. Возвращает число новых плиток.
    int update(float x, float z);
    std::size_t tileCount() const { return tiles_.size(); }
    float tileSize() const { return static_cast<float>(cfg_.samples - 1) * cfg_.cell; }
    void clear();

private:
    struct Key {
        int x, z;
        bool operator<(const Key& o) const { return x != o.x ? x < o.x : z < o.z; }
    };
    physics::PhysicsWorld& world_;
    const Heightmap& map_;
    Settings cfg_;
    std::map<Key, contracts::EntityId> tiles_;
};

}  // namespace han::world
// Пример: TerrainPhysics terrain(*world, heightmap); terrain.update(player.x, player.z);  // каждый кадр или раз в секунду
