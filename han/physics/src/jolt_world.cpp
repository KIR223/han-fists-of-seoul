// Реализация PhysicsWorld на Jolt (v5.x). Компилируется только если Jolt найден (см. CMakeLists.txt модуля).
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "han/physics/physics_world.h"

namespace han::physics {
namespace {
using contracts::EntityId;
using contracts::Vec3;

constexpr JPH::ObjectLayer kObjStatic = 0;
constexpr JPH::ObjectLayer kObjMoving = 1;

class LayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == kObjMoving || b == kObjMoving; }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer == JPH::BroadPhaseLayer(0) ? "static" : "moving"; }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override {
        return object == kObjMoving || broad == JPH::BroadPhaseLayer(1);
    }
};

// Фильтр слоёв запросов по битовой маске контракта.
class MaskFilter final : public JPH::ObjectLayerFilter {
public:
    explicit MaskFilter(std::uint32_t mask) : mask_(mask) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return ((mask_ >> layer) & 1u) != 0; }

private:
    std::uint32_t mask_;
};

JPH::Vec3 toJolt(Vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
Vec3 fromJolt(JPH::Vec3Arg v) { return Vec3{v.GetX(), v.GetY(), v.GetZ()}; }

// Jolt инициализируется глобально один раз на процесс (счётчик ссылок, чтобы несколько миров жили вместе).
std::mutex gInitMutex;
int gInitUsers = 0;
void joltAcquire() {
    std::lock_guard<std::mutex> lock(gInitMutex);
    ++gInitUsers;
    static bool registered = false;
    if (!registered) {
        registered = true;
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}
void joltRelease() {
    std::lock_guard<std::mutex> lock(gInitMutex);
    // Типы Jolt регистрируются один раз на процесс и живут до его конца: повторная регистрация после выгрузки падает
    // (обнаружено тестами машин), а держать их в памяти ничего не стоит.
    --gInitUsers;
}
}  // namespace

bool hasJolt() { return true; }

namespace {
// Слушатель контактов: Jolt зовёт его из потоков физики, поэтому очередь под мьютексом.
class ContactCollector final : public JPH::ContactListener {
public:
    struct Raw {
        Vec3 point;
        float speed;
        JPH::uint64 a, b;
    };
    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override {
        if (manifold.mRelativeContactPointsOn1.empty()) return;
        const JPH::RVec3 p = manifold.GetWorldSpaceContactPointOn1(0);
        const float speed = std::fabs((b1.GetPointVelocity(p) - b2.GetPointVelocity(p)).Dot(manifold.mWorldSpaceNormal));
        if (speed < minSpeed) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (events.size() >= 1024) events.erase(events.begin());  // переполнение: теряем самое старое
        events.push_back({Vec3{p.GetX(), p.GetY(), p.GetZ()}, speed, b1.GetUserData(), b2.GetUserData()});
    }
    float minSpeed = 0.6f;
    std::mutex mutex;
    std::vector<Raw> events;
};
}  // namespace

struct PhysicsWorld::Impl {
    // Что нарисовать в отладочных линиях: форма тела.
    struct ShapeInfo {
        bool sphere = false, isStatic = false;
        Vec3 half;         // для коробки: половины размеров
        float radius = 0;  // для шара
        bool hidden = false;  // не рисовать в отладке (рельеф)
    };
    struct Character {
        JPH::Ref<JPH::CharacterVirtual> body;
        float radius = 0.3f, height = 1.8f;
        Vec3 desired;
        Vec3 knock;        // скорость от толчков, затухает
        float mass = 75.f;
        float stepHeight = 0.35f;
    };

    LayerPairFilter pairFilter;
    BroadPhaseLayers broadPhase;
    ObjectVsBroadPhase objectVsBroad;
    JPH::PhysicsSystem system;
    JPH::TempAllocatorImpl temp{8 * 1024 * 1024};
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    Vec3 gravity;
    int collisionSteps = 1;
    SurfaceTable surfaces;
    ContactCollector contacts;

    EntityId nextId = 1;
    std::unordered_map<EntityId, JPH::BodyID> bodies;
    std::unordered_map<EntityId, contracts::SurfaceMaterial> materials;
    std::unordered_map<EntityId, ShapeInfo> shapes;
    std::unordered_map<EntityId, Character> characters;

    struct Vehicle {
        JPH::Ref<JPH::VehicleConstraint> constraint;
        JPH::BodyID body;
        PhysicsWorld::VehicleInput input;
    };
    std::unordered_map<EntityId, Vehicle> vehicles;

    EntityId entityOf(JPH::BodyID id) const {
        JPH::BodyLockRead lock(system.GetBodyLockInterface(), id);
        return lock.Succeeded() ? static_cast<EntityId>(lock.GetBody().GetUserData()) : contracts::kNoEntity;
    }
    contracts::SurfaceMaterial materialOf(EntityId e) const {
        auto it = materials.find(e);
        return it == materials.end() ? contracts::SurfaceMaterial::Unknown : it->second;
    }

    EntityId addBody(const JPH::Shape* shape, Vec3 center, contracts::Quat rotation, JPH::EMotionType motion, float mass,
                     contracts::SurfaceMaterial material, ShapeInfo info) {
        const bool moving = motion != JPH::EMotionType::Static;
        JPH::BodyCreationSettings settings(shape, JPH::RVec3(center.x, center.y, center.z),
                                           JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized(), motion,
                                           moving ? kObjMoving : kObjStatic);
        if (moving) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = std::max(mass, 0.01f);
        }
        const EntityId id = nextId;
        settings.mUserData = id;
        const SurfaceProperties& surface = surfaces.get(material);
        settings.mFriction = surface.friction;
        settings.mRestitution = surface.restitution;
        if (moving) settings.mMotionQuality = JPH::EMotionQuality::LinearCast;  // CCD: быстрые тела не пролетают сквозь тонкие стены
        JPH::BodyInterface& bi = system.GetBodyInterface();
        const JPH::BodyID body = bi.CreateAndAddBody(settings, moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (body.IsInvalid()) return contracts::kNoEntity;
        ++nextId;
        bodies[id] = body;
        materials[id] = material;
        info.isStatic = !moving;
        shapes[id] = info;
        return id;
    }
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}

PhysicsWorld::~PhysicsWorld() {
    if (impl_) {
        impl_->characters.clear();
        JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
        // Машины: сначала ограничение (оно держит указатель на тело), потом тело.
        for (auto it = impl_->vehicles.begin(); it != impl_->vehicles.end();) {
            impl_->system.RemoveStepListener(it->second.constraint);
            impl_->system.RemoveConstraint(it->second.constraint);
            const JPH::BodyID body = it->second.body;
            impl_->bodies.erase(it->first);
            it = impl_->vehicles.erase(it);
            bi.RemoveBody(body);
            bi.DestroyBody(body);
        }
        for (auto& [id, body] : impl_->bodies) {
            bi.RemoveBody(body);
            bi.DestroyBody(body);
        }
        impl_->bodies.clear();
        impl_.reset();
        joltRelease();
    }
}

std::unique_ptr<PhysicsWorld> PhysicsWorld::create(const WorldSettings& settings) {
    joltAcquire();
    std::unique_ptr<PhysicsWorld> world(new PhysicsWorld());
    Impl& impl = *world->impl_;
    impl.gravity = settings.gravity;
    impl.collisionSteps = std::max(settings.collisionSteps, 1);
    impl.contacts.minSpeed = settings.contactSoundMinSpeed;
    impl.system.SetContactListener(&impl.contacts);
    impl.jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::max(settings.workerThreads, 1));
    impl.system.Init(settings.maxBodies, 0, std::max<std::uint32_t>(settings.maxBodies, 1024), 4096, impl.broadPhase, impl.objectVsBroad, impl.pairFilter);
    impl.system.SetGravity(toJolt(settings.gravity));
    return world;
}

EntityId PhysicsWorld::addStaticBox(Vec3 center, Vec3 halfExtents, contracts::SurfaceMaterial material, contracts::Quat rotation) {
    JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(toJolt(halfExtents));
    return impl_->addBody(shape, center, rotation, JPH::EMotionType::Static, 0.f, material, {false, true, halfExtents, 0.f});
}

EntityId PhysicsWorld::addStaticHeightfield(float minX, float minZ, float cell, int sampleCount, const std::vector<float>& heights, contracts::SurfaceMaterial material) {
    if (sampleCount < 4 || sampleCount > 256 || sampleCount % 2 != 0 || heights.size() != static_cast<std::size_t>(sampleCount) * static_cast<std::size_t>(sampleCount) || cell <= 0.f)
        return contracts::kNoEntity;
    JPH::HeightFieldShapeSettings settings(heights.data(), JPH::Vec3(minX, 0.f, minZ), JPH::Vec3(cell, 1.f, cell), static_cast<JPH::uint32>(sampleCount));
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) return contracts::kNoEntity;
    return impl_->addBody(result.Get(), {0.f, 0.f, 0.f}, {}, JPH::EMotionType::Static, 0.f, material, {false, true, {}, 0.f, true});
}

EntityId PhysicsWorld::addDynamicSphere(Vec3 center, float radius, float mass, contracts::SurfaceMaterial material) {
    JPH::Ref<JPH::Shape> shape = new JPH::SphereShape(radius);
    return impl_->addBody(shape, center, {}, JPH::EMotionType::Dynamic, mass, material, {true, false, {}, radius});
}

EntityId PhysicsWorld::addDynamicBox(Vec3 center, Vec3 halfExtents, float mass, contracts::SurfaceMaterial material, contracts::Quat rotation) {
    JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(toJolt(halfExtents));
    return impl_->addBody(shape, center, rotation, JPH::EMotionType::Dynamic, mass, material, {false, false, halfExtents, 0.f});
}

void PhysicsWorld::removeBody(EntityId id) {
    auto it = impl_->bodies.find(id);
    if (it == impl_->bodies.end()) return;
    JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
    bi.RemoveBody(it->second);
    bi.DestroyBody(it->second);
    impl_->bodies.erase(it);
    impl_->materials.erase(id);
    impl_->shapes.erase(id);
}

EntityId PhysicsWorld::createVehicle(const VehicleDesc& d, Vec3 position, contracts::Quat rotation) {
    Impl& im = *impl_;
    // Шасси с опущенным центром масс (машина устойчивее к опрокидыванию).
    JPH::Ref<JPH::Shape> shape = JPH::OffsetCenterOfMassShapeSettings(JPH::Vec3(0.f, -d.halfExtents.y, 0.f), new JPH::BoxShape(toJolt(d.halfExtents))).Create().Get();
    JPH::BodyCreationSettings bs(shape, JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized(),
                                 JPH::EMotionType::Dynamic, kObjMoving);
    bs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    bs.mMassPropertiesOverride.mMass = d.mass;
    const EntityId id = im.nextId;
    bs.mUserData = id;
    JPH::BodyInterface& bi = im.system.GetBodyInterface();
    const JPH::BodyID body = bi.CreateAndAddBody(bs, JPH::EActivation::Activate);
    if (body.IsInvalid()) return contracts::kNoEntity;

    // Колёса: передние (индексы 0, 1) и задние (2, 3); привод задаётся дифференциалами.
    const float w = d.halfExtents.x, h = d.halfExtents.y, l = d.halfExtents.z;
    const JPH::Vec3 positions[4] = {{w, -0.9f * h, l - 2.f * d.wheelRadius}, {-w, -0.9f * h, l - 2.f * d.wheelRadius},
                                    {w, -0.9f * h, -l + 2.f * d.wheelRadius}, {-w, -0.9f * h, -l + 2.f * d.wheelRadius}};
    JPH::VehicleConstraintSettings vs;
    vs.mDrawConstraintSize = 0.1f;
    vs.mMaxPitchRollAngle = JPH::DegreesToRadians(60.f);
    for (int i = 0; i < 4; ++i) {
        JPH::WheelSettingsWV* wheel = new JPH::WheelSettingsWV;
        wheel->mPosition = positions[i];
        wheel->mSuspensionDirection = JPH::Vec3(0.f, -1.f, 0.f);
        wheel->mRadius = d.wheelRadius;
        wheel->mWidth = d.wheelWidth;
        wheel->mMaxSteerAngle = i < 2 ? JPH::DegreesToRadians(d.maxSteerDegrees) : 0.f;
        wheel->mMaxBrakeTorque = d.brakeTorque;
        wheel->mMaxHandBrakeTorque = i < 2 ? 0.f : d.handbrakeTorque;
        vs.mWheels.push_back(wheel);
    }
    JPH::WheeledVehicleControllerSettings* controller = new JPH::WheeledVehicleControllerSettings;
    controller->mEngine.mMaxTorque = d.engineMaxTorque;
    controller->mEngine.mMinRPM = d.engineMinRpm;
    controller->mEngine.mMaxRPM = d.engineMaxRpm;
    controller->mTransmission.mClutchStrength = 10.f;
    const bool front = d.drive != Drive::Rear, rear = d.drive != Drive::Front;
    controller->mDifferentials.resize((front ? 1 : 0) + (rear ? 1 : 0));
    std::size_t di = 0;
    const float share = 1.f / static_cast<float>(controller->mDifferentials.size());
    if (front) {
        controller->mDifferentials[di].mLeftWheel = 0;
        controller->mDifferentials[di].mRightWheel = 1;
        controller->mDifferentials[di].mEngineTorqueRatio = share;
        ++di;
    }
    if (rear) {
        controller->mDifferentials[di].mLeftWheel = 2;
        controller->mDifferentials[di].mRightWheel = 3;
        controller->mDifferentials[di].mEngineTorqueRatio = share;
    }
    vs.mController = controller;

    Impl::Vehicle vehicle;
    vehicle.body = body;
    {
        JPH::BodyLockWrite lock(im.system.GetBodyLockInterface(), body);
        vehicle.constraint = new JPH::VehicleConstraint(lock.GetBody(), vs);
    }
    // Проверка земли считается по ссылкам и принадлежит ограничению: отдельно её удалять нельзя (двойное освобождение).
    vehicle.constraint->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterRay(kObjMoving));
    im.system.AddConstraint(vehicle.constraint);
    im.system.AddStepListener(vehicle.constraint);
    ++im.nextId;
    im.bodies[id] = body;
    im.materials[id] = contracts::SurfaceMaterial::Metal;
    im.shapes[id] = {false, false, d.halfExtents, 0.f};
    im.vehicles.emplace(id, std::move(vehicle));
    return id;
}

void PhysicsWorld::destroyVehicle(EntityId id) {
    Impl& im = *impl_;
    const auto it = im.vehicles.find(id);
    if (it == im.vehicles.end()) return;
    im.system.RemoveStepListener(it->second.constraint);
    im.system.RemoveConstraint(it->second.constraint);
    const JPH::BodyID body = it->second.body;
    im.vehicles.erase(it);  // освобождает ограничение и проверку земли до удаления тела, на которое они ссылаются
    JPH::BodyInterface& bi = im.system.GetBodyInterface();
    bi.RemoveBody(body);
    bi.DestroyBody(body);
    im.bodies.erase(id);
    im.materials.erase(id);
    im.shapes.erase(id);
}

void PhysicsWorld::destroyBody(EntityId id) {
    Impl& im = *impl_;
    const auto it = im.bodies.find(id);
    if (it == im.bodies.end() || im.vehicles.count(id)) return;
    JPH::BodyInterface& bi = im.system.GetBodyInterface();
    bi.RemoveBody(it->second);
    bi.DestroyBody(it->second);
    im.bodies.erase(it);
    im.materials.erase(id);
    im.shapes.erase(id);
}

void PhysicsWorld::setVehicleInput(EntityId id, const VehicleInput& input) {
    if (const auto it = impl_->vehicles.find(id); it != impl_->vehicles.end()) it->second.input = input;
}

PhysicsWorld::VehicleState PhysicsWorld::vehicleState(EntityId id) const {
    VehicleState s;
    const auto it = impl_->vehicles.find(id);
    if (it == impl_->vehicles.end()) return s;
    const JPH::VehicleConstraint& c = *it->second.constraint;
    const JPH::Body* body = c.GetVehicleBody();
    const auto* controller = static_cast<const JPH::WheeledVehicleController*>(c.GetController());
    s.valid = true;
    const JPH::RVec3 p = body->GetPosition();
    s.position = {p.GetX(), p.GetY(), p.GetZ()};
    s.velocity = fromJolt(body->GetLinearVelocity());
    const JPH::Quat q = body->GetRotation();
    s.rotation = {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
    s.speed = body->GetLinearVelocity().Dot(q.RotateAxisZ());
    s.rpm = controller->GetEngine().GetCurrentRPM();
    s.gear = controller->GetTransmission().GetCurrentGear();
    for (int i = 0; i < 4 && i < static_cast<int>(c.GetWheels().size()); ++i) {
        const JPH::RMat44 m = c.GetWheelWorldTransform(static_cast<JPH::uint>(i), JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY());
        const JPH::RVec3 wp = m.GetTranslation();
        const JPH::Quat wq = m.GetRotation().GetQuaternion();
        s.wheelPosition[i] = {wp.GetX(), wp.GetY(), wp.GetZ()};
        s.wheelRotation[i] = {wq.GetX(), wq.GetY(), wq.GetZ(), wq.GetW()};
        s.wheelContact[i] = c.GetWheel(static_cast<JPH::uint>(i))->HasContact();
    }
    return s;
}

void PhysicsWorld::setVelocity(EntityId id, Vec3 velocity) {
    if (auto it = impl_->bodies.find(id); it != impl_->bodies.end()) impl_->system.GetBodyInterface().SetLinearVelocity(it->second, toJolt(velocity));
}

void PhysicsWorld::step(float dt) {
    Impl& im = *impl_;
    for (auto& [id, v] : im.vehicles) {
        auto* controller = static_cast<JPH::WheeledVehicleController*>(v.constraint->GetController());
        const JPH::Body* body = v.constraint->GetVehicleBody();
        const JPH::Vec3 forward = body->GetRotation().RotateAxisZ();
        const float speed = body->GetLinearVelocity().Dot(forward);
        float throttle = std::clamp(v.input.throttle, -1.f, 1.f), brake = std::clamp(v.input.brake, 0.f, 1.f);
        // Газ против движения — это торможение, пока машина не остановится (иначе коробка дёргается между передачами).
        if ((throttle < 0.f && speed > 1.f) || (throttle > 0.f && speed < -1.f)) {
            brake = 1.f;
            throttle = 0.f;
        }
        controller->SetDriverInput(throttle, std::clamp(v.input.steer, -1.f, 1.f), brake, std::clamp(v.input.handbrake, 0.f, 1.f));
        if (std::fabs(throttle) > 0.01f || brake > 0.01f) im.system.GetBodyInterface().ActivateBody(v.body);
    }
    for (auto& [id, c] : im.characters) {
        JPH::CharacterVirtual& body = *c.body;
        const float groundVy = body.GetGroundVelocity().GetY();
        const float vy = body.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround
                             ? std::max(groundVy, 0.f)
                             : body.GetLinearVelocity().GetY() + im.gravity.y * dt;
        // Толчок затухает за ~0.3 с; на земле добавляется к желаемой скорости, в воздухе тоже (оттеснение от удара).
        c.knock.x *= std::exp(-6.f * dt);
        c.knock.z *= std::exp(-6.f * dt);
        body.SetLinearVelocity(JPH::Vec3(c.desired.x + c.knock.x, vy + c.knock.y, c.desired.z + c.knock.z));
        c.knock.y = 0.f;
        JPH::CharacterVirtual::ExtendedUpdateSettings ext;
        ext.mWalkStairsStepUp = JPH::Vec3(0.f, c.stepHeight, 0.f);
        body.ExtendedUpdate(dt, toJolt(im.gravity), ext, im.system.GetDefaultBroadPhaseLayerFilter(kObjMoving),
                            im.system.GetDefaultLayerFilter(kObjMoving), {}, {}, im.temp);
    }
    im.system.Update(dt, im.collisionSteps, &im.temp, im.jobs.get());
}

Vec3 PhysicsWorld::positionOf(EntityId id) const {
    if (auto c = impl_->characters.find(id); c != impl_->characters.end()) {
        const JPH::RVec3 p = c->second.body->GetPosition();
        return Vec3{p.GetX(), p.GetY(), p.GetZ()};
    }
    if (auto b = impl_->bodies.find(id); b != impl_->bodies.end()) {
        const JPH::RVec3 p = impl_->system.GetBodyInterface().GetCenterOfMassPosition(b->second);
        return Vec3{p.GetX(), p.GetY(), p.GetZ()};
    }
    return {};
}

Vec3 PhysicsWorld::velocityOf(EntityId id) const {
    if (auto c = impl_->characters.find(id); c != impl_->characters.end()) return fromJolt(c->second.body->GetLinearVelocity());
    if (auto b = impl_->bodies.find(id); b != impl_->bodies.end()) return fromJolt(impl_->system.GetBodyInterface().GetLinearVelocity(b->second));
    return {};
}

void PhysicsWorld::setSurfaceTable(const SurfaceTable& table) { impl_->surfaces = table; }

const SurfaceTable& PhysicsWorld::surfaceTable() const { return impl_->surfaces; }

void PhysicsWorld::drainContacts(std::vector<ContactEvent>& out) {
    std::vector<ContactCollector::Raw> raw;
    {
        std::lock_guard<std::mutex> lock(impl_->contacts.mutex);
        raw.swap(impl_->contacts.events);
    }
    for (const auto& r : raw) {
        ContactEvent e;
        e.point = r.point;
        e.impactSpeed = r.speed;
        e.a = static_cast<EntityId>(r.a);
        e.b = static_cast<EntityId>(r.b);
        e.materialA = impl_->materialOf(e.a);
        e.materialB = impl_->materialOf(e.b);
        out.push_back(e);
    }
}

bool PhysicsWorld::allFinite() const {
    const auto finite = [](Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    for (const auto& [id, body] : impl_->bodies)
        if (!finite(positionOf(id)) || !finite(velocityOf(id))) return false;
    for (const auto& [id, c] : impl_->characters)
        if (!finite(positionOf(id)) || !finite(velocityOf(id))) return false;
    return true;
}

contracts::Quat PhysicsWorld::rotationOf(EntityId id) const {
    if (auto b = impl_->bodies.find(id); b != impl_->bodies.end()) {
        const JPH::Quat q = impl_->system.GetBodyInterface().GetRotation(b->second);
        return contracts::Quat{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
    }
    return {};
}

bool PhysicsWorld::isAwake(EntityId id) const {
    if (auto b = impl_->bodies.find(id); b != impl_->bodies.end()) return impl_->system.GetBodyInterface().IsActive(b->second);
    return impl_->characters.count(id) != 0;
}

bool PhysicsWorld::jump(EntityId id, float speed) {
    auto it = impl_->characters.find(id);
    if (it == impl_->characters.end() || it->second.body->GetGroundState() != JPH::CharacterBase::EGroundState::OnGround) return false;
    it->second.knock.y = speed;  // шаг мира добавит это к вертикальной скорости один раз
    return true;
}

void PhysicsWorld::debugLines(std::vector<DebugLine>& out) const {
    // Поворот вектора кватернионом: v + 2w(q x v) + 2 q x (q x v).
    const auto quatRotate = [](contracts::Quat q, Vec3 v) {
        const Vec3 t{2 * (q.y * v.z - q.z * v.y), 2 * (q.z * v.x - q.x * v.z), 2 * (q.x * v.y - q.y * v.x)};
        return Vec3{v.x + q.w * t.x + (q.y * t.z - q.z * t.y), v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
                    v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
    };
    const auto add = [&](Vec3 a, Vec3 b, std::uint32_t color) { out.push_back({a, b, color}); };
    // Окружность вокруг оси (0 — X, 1 — Y, 2 — Z).
    const auto ring = [&](Vec3 center, int axis, float radius, std::uint32_t color) {
        constexpr int kSeg = 16;
        constexpr float kTwoPi = 6.2831853f;
        Vec3 prev{};
        for (int i = 0; i <= kSeg; ++i) {
            const float a = kTwoPi * static_cast<float>(i) / kSeg, c = std::cos(a) * radius, s = std::sin(a) * radius;
            const Vec3 p = axis == 0 ? Vec3{center.x, center.y + c, center.z + s}
                                     : (axis == 1 ? Vec3{center.x + c, center.y, center.z + s} : Vec3{center.x + c, center.y + s, center.z});
            if (i > 0) add(prev, p, color);
            prev = p;
        }
    };
    for (const auto& [id, info] : impl_->shapes) {
        if (info.hidden) continue;
        const Vec3 center = positionOf(id);
        const std::uint32_t color = info.isStatic ? kColorStatic : (isAwake(id) ? kColorAwake : kColorAsleep);
        if (info.sphere) {
            for (int axis = 0; axis < 3; ++axis) ring(center, axis, info.radius, color);
            continue;
        }
        const contracts::Quat q = rotationOf(id);
        Vec3 corner[8];
        for (int i = 0; i < 8; ++i) {
            const Vec3 local{(i & 1 ? 1.f : -1.f) * info.half.x, (i & 2 ? 1.f : -1.f) * info.half.y, (i & 4 ? 1.f : -1.f) * info.half.z};
            const Vec3 r = quatRotate(q, local);
            corner[i] = Vec3{center.x + r.x, center.y + r.y, center.z + r.z};
        }
        for (int i = 0; i < 8; ++i)
            for (int bit = 1; bit <= 4; bit <<= 1)
                if (!(i & bit)) add(corner[i], corner[i | bit], color);
    }
    for (const auto& [id, c] : impl_->characters) {
        const Vec3 base = positionOf(id);
        const float top = std::max(c.height - c.radius, c.radius);
        ring({base.x, base.y + c.radius, base.z}, 1, c.radius, kColorCharacter);
        ring({base.x, base.y + top, base.z}, 1, c.radius, kColorCharacter);
        ring({base.x, base.y + c.height * 0.5f, base.z}, 1, c.radius, kColorCharacter);
        for (int k = 0; k < 4; ++k) {
            const float a = 1.5707963f * static_cast<float>(k);
            const float dx = std::cos(a) * c.radius, dz = std::sin(a) * c.radius;
            add({base.x + dx, base.y + c.radius, base.z + dz}, {base.x + dx, base.y + top, base.z + dz}, kColorCharacter);
        }
    }
}

std::uint32_t PhysicsWorld::bodyCount() const { return static_cast<std::uint32_t>(impl_->bodies.size()); }

contracts::RayHit PhysicsWorld::raycast(Vec3 from, Vec3 to, std::uint32_t layerMask) const {
    contracts::RayHit out;
    const JPH::Vec3 delta = toJolt(to) - toJolt(from);
    const float length = delta.Length();
    if (length <= 1e-6f) return out;
    JPH::RRayCast ray(JPH::RVec3(from.x, from.y, from.z), delta);
    JPH::RayCastResult hit;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, MaskFilter(layerMask))) return out;
    out.hit = true;
    out.distance = hit.mFraction * length;
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    out.point = Vec3{point.GetX(), point.GetY(), point.GetZ()};
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
        out.entity = static_cast<EntityId>(lock.GetBody().GetUserData());
        out.normal = fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
    }
    out.material = impl_->materialOf(out.entity);
    return out;
}

contracts::RayHit PhysicsWorld::sweepCapsule(Vec3 from, Vec3 to, float radius, float height, std::uint32_t layerMask) const {
    contracts::RayHit out;
    const JPH::Vec3 delta = toJolt(to) - toJolt(from);
    const float length = delta.Length();
    const float cylinderHalf = std::max(height * 0.5f - radius, 0.01f);
    JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(cylinderHalf, radius);
    const JPH::RVec3 center(from.x, from.y + height * 0.5f, from.z);
    JPH::RShapeCast cast(capsule, JPH::Vec3::sReplicate(1.f), JPH::RMat44::sTranslation(center), delta);
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    impl_->system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, MaskFilter(layerMask));
    if (!collector.HadHit()) return out;
    out.hit = true;
    out.distance = collector.mHit.mFraction * length;
    out.point = fromJolt(JPH::Vec3(collector.mHit.mContactPointOn2));
    out.normal = fromJolt(-collector.mHit.mPenetrationAxis.Normalized());
    out.entity = impl_->entityOf(collector.mHit.mBodyID2);
    out.material = impl_->materialOf(out.entity);
    return out;
}

std::uint32_t PhysicsWorld::overlapSphere(Vec3 center, float radius, EntityId* out, std::uint32_t maxOut, std::uint32_t layerMask) const {
    JPH::Ref<JPH::Shape> sphere = new JPH::SphereShape(radius);
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    impl_->system.GetNarrowPhaseQuery().CollideShape(sphere, JPH::Vec3::sReplicate(1.f),
                                                     JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z)), settings,
                                                     JPH::RVec3::sZero(), collector, {}, MaskFilter(layerMask));
    std::vector<EntityId> found;
    for (const JPH::CollideShapeResult& r : collector.mHits) {
        const EntityId e = impl_->entityOf(r.mBodyID2);
        if (e != contracts::kNoEntity && std::find(found.begin(), found.end(), e) == found.end()) found.push_back(e);
    }
    for (std::uint32_t i = 0; i < found.size() && i < maxOut; ++i) out[i] = found[i];
    return static_cast<std::uint32_t>(found.size());
}

EntityId PhysicsWorld::create(const contracts::CharacterDesc& desc, Vec3 position) {
    const float cylinderHalf = std::max(desc.height * 0.5f - desc.radius, 0.01f);
    // Капсула смещена вверх, чтобы начало координат контроллера было под ногами.
    JPH::Ref<JPH::Shape> shape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.f, desc.height * 0.5f, 0.f), JPH::Quat::sIdentity(),
                                                                       new JPH::CapsuleShape(cylinderHalf, desc.radius))
                                     .Create()
                                     .Get();
    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mMass = desc.mass;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(desc.maxSlopeDegrees);
    settings.mUp = JPH::Vec3::sAxisY();
    Impl::Character c;
    c.body = new JPH::CharacterVirtual(&settings, JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(), &impl_->system);
    c.mass = desc.mass;
    c.radius = desc.radius;
    c.height = desc.height;
    c.stepHeight = desc.stepHeight;
    const EntityId id = impl_->nextId++;
    impl_->characters.emplace(id, std::move(c));
    return id;
}

void PhysicsWorld::destroy(EntityId id) { impl_->characters.erase(id); }

void PhysicsWorld::setDesiredVelocity(EntityId id, Vec3 velocity) {
    if (auto it = impl_->characters.find(id); it != impl_->characters.end()) it->second.desired = velocity;
}

void PhysicsWorld::applyImpulse(EntityId id, Vec3 impulse) {
    if (auto it = impl_->characters.find(id); it != impl_->characters.end()) {
        auto& c = it->second;
        const float inv = 1.f / std::max(c.mass, 1.f);
        c.knock.x += impulse.x * inv;
        c.knock.y += impulse.y * inv;
        c.knock.z += impulse.z * inv;
    }
}

contracts::CharacterState PhysicsWorld::state(EntityId id) const {
    contracts::CharacterState s;
    if (auto it = impl_->characters.find(id); it != impl_->characters.end()) {
        const JPH::CharacterVirtual& b = *it->second.body;
        s.position = fromJolt(JPH::Vec3(b.GetPosition()));
        s.velocity = fromJolt(b.GetLinearVelocity());
        s.grounded = b.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }
    return s;
}
}  // namespace han::physics
