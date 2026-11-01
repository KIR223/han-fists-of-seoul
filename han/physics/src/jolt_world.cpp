// Реализация PhysicsWorld на Jolt (v5.x). Компилируется только если Jolt найден (см. CMakeLists.txt модуля).
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
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
    if (gInitUsers++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}
void joltRelease() {
    std::lock_guard<std::mutex> lock(gInitMutex);
    if (--gInitUsers == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}
}  // namespace

bool hasJolt() { return true; }

struct PhysicsWorld::Impl {
    // Что нарисовать в отладочных линиях: форма тела.
    struct ShapeInfo {
        bool sphere = false, isStatic = false;
        Vec3 half;         // для коробки: половины размеров
        float radius = 0;  // для шара
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

    EntityId nextId = 1;
    std::unordered_map<EntityId, JPH::BodyID> bodies;
    std::unordered_map<EntityId, contracts::SurfaceMaterial> materials;
    std::unordered_map<EntityId, ShapeInfo> shapes;
    std::unordered_map<EntityId, Character> characters;

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
    impl.jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::max(settings.workerThreads, 1));
    impl.system.Init(settings.maxBodies, 0, std::max<std::uint32_t>(settings.maxBodies, 1024), 4096, impl.broadPhase, impl.objectVsBroad, impl.pairFilter);
    impl.system.SetGravity(toJolt(settings.gravity));
    return world;
}

EntityId PhysicsWorld::addStaticBox(Vec3 center, Vec3 halfExtents, contracts::SurfaceMaterial material, contracts::Quat rotation) {
    JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(toJolt(halfExtents));
    return impl_->addBody(shape, center, rotation, JPH::EMotionType::Static, 0.f, material, {false, true, halfExtents, 0.f});
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

void PhysicsWorld::step(float dt) {
    Impl& im = *impl_;
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
    im.system.Update(dt, 1, &im.temp, im.jobs.get());
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
