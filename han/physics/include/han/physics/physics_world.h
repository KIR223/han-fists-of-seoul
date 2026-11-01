// han/physics/physics_world.h — физический мир на Jolt (E5): статика, динамика, лучи/свипы/оверлапы, контроллер персонажа.
// Реализует контракты IPhysicsQuery и ICharacterController; Jolt наружу не торчит (pimpl).
// Единицы — метры, секунды, Y вверх. Слои для layerMask: бит 0 — статика (kLayerStatic), бит 1 — динамика и персонажи (kLayerMoving).
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "contracts/character_controller.h"
#include "contracts/physics_query.h"

namespace han::physics {
constexpr std::uint32_t kLayerStatic = 1u << 0;
constexpr std::uint32_t kLayerMoving = 1u << 1;

struct WorldSettings {
    contracts::Vec3 gravity{0.f, -9.81f, 0.f};
    std::uint32_t maxBodies = 16384;
    int workerThreads = 1;  // 1 — детерминированный тестовый режим; в игре будет job system из han_core (позже)
};

// true, если модуль собран с Jolt (иначе create() возвращает nullptr).
bool hasJolt();

class PhysicsWorld final : public contracts::IPhysicsQuery, public contracts::ICharacterController {
public:
    static std::unique_ptr<PhysicsWorld> create(const WorldSettings& settings = {});
    ~PhysicsWorld() override;

    // Тела. Возвращают EntityId (0 — ошибка). Материал нужен для звука и эффектов.
    contracts::EntityId addStaticBox(contracts::Vec3 center, contracts::Vec3 halfExtents,
                                     contracts::SurfaceMaterial material = contracts::SurfaceMaterial::Concrete,
                                     contracts::Quat rotation = {});
    contracts::EntityId addDynamicSphere(contracts::Vec3 center, float radius, float mass = 1.f,
                                         contracts::SurfaceMaterial material = contracts::SurfaceMaterial::Unknown);
    contracts::EntityId addDynamicBox(contracts::Vec3 center, contracts::Vec3 halfExtents, float mass = 1.f,
                                      contracts::SurfaceMaterial material = contracts::SurfaceMaterial::Unknown,
                                      contracts::Quat rotation = {});
    void removeBody(contracts::EntityId id);

    // Шаг мира: фиксированный dt (обычно 1/60), внутри — столкновения и персонажи.
    void step(float dt);

    contracts::Vec3 positionOf(contracts::EntityId id) const;  // центр масс тела или ноги персонажа
    contracts::Quat rotationOf(contracts::EntityId id) const;  // поворот тела (у персонажа — единичный)
    contracts::Vec3 velocityOf(contracts::EntityId id) const;
    bool isAwake(contracts::EntityId id) const;                // false — тело уснуло или не найдено

    // Прыжок: вертикальная скорость персонажа (м/с), только с земли. true — прыжок состоялся.
    bool jump(contracts::EntityId id, float speed);

    // Отладочная отрисовка: контуры тел и капсул персонажей линиями (цвета 0xAABBGGRR для ImGui).
    struct DebugLine {
        contracts::Vec3 a, b;
        std::uint32_t color = 0xFFFFFFFFu;
    };
    static constexpr std::uint32_t kColorStatic = 0xFF9A9A9Au, kColorAwake = 0xFF3C9BFFu, kColorAsleep = 0xFFD2A050u, kColorCharacter = 0xFF50E678u;
    void debugLines(std::vector<DebugLine>& out) const;
    std::uint32_t bodyCount() const;

    // IPhysicsQuery
    contracts::RayHit raycast(contracts::Vec3 from, contracts::Vec3 to, std::uint32_t layerMask = ~0u) const override;
    // from — нижняя точка капсулы (как base в FighterCapsule), свип идёт по прямой в to.
    contracts::RayHit sweepCapsule(contracts::Vec3 from, contracts::Vec3 to, float radius, float height,
                                   std::uint32_t layerMask = ~0u) const override;
    std::uint32_t overlapSphere(contracts::Vec3 center, float radius, contracts::EntityId* out, std::uint32_t maxOut,
                                std::uint32_t layerMask = ~0u) const override;

    // ICharacterController: position — точка под ногами.
    contracts::EntityId create(const contracts::CharacterDesc& desc, contracts::Vec3 position) override;
    void destroy(contracts::EntityId id) override;
    void setDesiredVelocity(contracts::EntityId id, contracts::Vec3 velocity) override;
    void applyImpulse(contracts::EntityId id, contracts::Vec3 impulse) override;
    contracts::CharacterState state(contracts::EntityId id) const override;

private:
    PhysicsWorld();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace han::physics
// Пример: auto w = PhysicsWorld::create(); w->addStaticBox({0,-0.5f,0},{50,0.5f,50}); auto hit = w->raycast({0,5,0},{0,-5,0});
