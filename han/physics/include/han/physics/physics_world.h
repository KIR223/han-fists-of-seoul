// han/physics/physics_world.h — физический мир на Jolt (E5): статика, динамика, лучи/свипы/оверлапы, контроллер персонажа.
// Реализует контракты IPhysicsQuery и ICharacterController; Jolt наружу не торчит (pimpl).
// Единицы — метры, секунды, Y вверх. Слои для layerMask: бит 0 — статика (kLayerStatic), бит 1 — динамика и персонажи (kLayerMoving).
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "contracts/character_controller.h"
#include "contracts/physics_query.h"
#include "han/physics/surface_table.h"

namespace han::physics {
constexpr std::uint32_t kLayerStatic = 1u << 0;
constexpr std::uint32_t kLayerMoving = 1u << 1;

struct WorldSettings {
    contracts::Vec3 gravity{0.f, -9.81f, 0.f};
    std::uint32_t maxBodies = 16384;
    int collisionSteps = 1;  // субшаги столкновений за один step(): 2–4 для быстрых и сложных сцен (дороже, но стабильнее)
    float contactSoundMinSpeed = 0.6f;  // удары тише этой скорости сближения (м/с) не попадают в события звука
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
    // Рельеф (E17): квадратная сетка высот sampleCount x sampleCount (чётное, до 256) с углом (minX, minZ), шагом cell метров, строки по z, высоты абсолютные.
    // Для больших карт создаётся по плитке рядом с игроком и убирается через destroy().
    contracts::EntityId addStaticHeightfield(float minX, float minZ, float cell, int sampleCount, const std::vector<float>& heights,
                                             contracts::SurfaceMaterial material = contracts::SurfaceMaterial::Concrete);
    contracts::EntityId addDynamicSphere(contracts::Vec3 center, float radius, float mass = 1.f,
                                         contracts::SurfaceMaterial material = contracts::SurfaceMaterial::Unknown);
    contracts::EntityId addDynamicBox(contracts::Vec3 center, contracts::Vec3 halfExtents, float mass = 1.f,
                                      contracts::SurfaceMaterial material = contracts::SurfaceMaterial::Unknown,
                                      contracts::Quat rotation = {});
    void removeBody(contracts::EntityId id);
    void setVelocity(contracts::EntityId id, contracts::Vec3 velocity);  // линейная скорость динамического тела, м/с

    // Свойства поверхностей (трение, упругость, ключ звука). Применяются к телам, созданным ПОСЛЕ вызова.
    void setSurfaceTable(const SurfaceTable& table);
    const SurfaceTable& surfaceTable() const;

    // Звук из физики (P1): удары тел о поверхности за последние шаги. Очередь ограничена (старые теряются), вызывать раз в кадр.
    struct ContactEvent {
        contracts::Vec3 point;
        float impactSpeed = 0;  // скорость сближения вдоль нормали, м/с
        contracts::EntityId a = contracts::kNoEntity, b = contracts::kNoEntity;
        contracts::SurfaceMaterial materialA = contracts::SurfaceMaterial::Unknown, materialB = contracts::SurfaceMaterial::Unknown;
    };
    void drainContacts(std::vector<ContactEvent>& out);
    // Машины (E12): шасси-коробка на подвеске с четырьмя колёсами, двигатель, автоматическая коробка передач, привод.
    // Оси: +Z — вперёд, +X — вправо (если смотреть вперёд, это −X в левой системе — в контрактах «вправо» = +X при взгляде вдоль −Z; поэтому
    // машина создаётся с поворотом rotation, чтобы её нос смотрел куда нужно). Значения по умолчанию взяты у проверенного примера Jolt.
    enum class Drive : std::uint8_t { Front, Rear, All };
    struct VehicleDesc {
        contracts::Vec3 halfExtents{0.9f, 0.2f, 2.0f};  // шасси: половины размеров, м
        float mass = 1500.f;
        float wheelRadius = 0.3f, wheelWidth = 0.1f;
        float maxSteerDegrees = 30.f;
        float engineMaxTorque = 500.f, engineMinRpm = 1000.f, engineMaxRpm = 6000.f;
        float brakeTorque = 1500.f, handbrakeTorque = 4000.f;
        Drive drive = Drive::Rear;
    };
    struct VehicleInput {
        float throttle = 0.f;  // −1..1: назад — задний ход (на ходу вперёд превращается в торможение)
        float steer = 0.f;     // −1..1: вправо положительное
        float brake = 0.f;     // 0..1
        float handbrake = 0.f; // 0..1
    };
    struct VehicleState {
        bool valid = false;
        contracts::Vec3 position, velocity;
        contracts::Quat rotation;
        float speed = 0.f;  // скорость вдоль носа, м/с (минус — задним ходом)
        float rpm = 0.f;
        int gear = 0;       // −1 задний, 0 нейтраль, 1… передачи
        contracts::Vec3 wheelPosition[4];
        contracts::Quat wheelRotation[4];
        bool wheelContact[4] = {};
    };
    contracts::EntityId createVehicle(const VehicleDesc& desc, contracts::Vec3 position, contracts::Quat rotation = {});
    void destroyVehicle(contracts::EntityId id);
    // Убирает обычное тело (статику, ящик, шар, плитку рельефа); персонажей убирает destroy(), машины — destroyVehicle().
    void destroyBody(contracts::EntityId id);
    void setVehicleInput(contracts::EntityId id, const VehicleInput& input);
    VehicleState vehicleState(contracts::EntityId id) const;

    // Проверка стабильности: все ли тела и персонажи имеют конечные координаты и скорости (нет NaN и бесконечностей).
    bool allFinite() const;

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
