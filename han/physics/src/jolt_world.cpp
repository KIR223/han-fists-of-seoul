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
    struct Character {
        JPH::Ref<JPH::CharacterVirtual> body;
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
    std::unordered_map<EntityId, Character> characters;

    EntityId entityOf(JPH::BodyID id) const {
        JPH::BodyLockRead lock(system.GetBodyLockInterface(), id);
        return lock.Succeeded() ? static_cast<EntityId>(lock.GetBody().GetUserData()) : contracts::kNoEntity;
    }
    contracts::SurfaceMaterial materialOf(EntityId e) const {
        auto it = materials.find(e);
        return it == materials.end() ? contracts::SurfaceMaterial::Unknown : it->second;
    }

    EntityId addBody(const JPH::Shape* shape, Vec3 center, JPH::EMotionType motion, float mass, contracts::SurfaceMaterial material) {
        const bool moving = motion != JPH::EMotionType::Static;
        JPH::BodyCreationSettings settings(shape, JPH::RVec3(center.x, center.y, center.z), JPH::Quat::sIdentity(), motion,
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

EntityId PhysicsWorld::addStaticBox(Vec3 center, Vec3 halfExtents, contracts::SurfaceMaterial material) {
    JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(toJolt(halfExtents));
    return impl_->addBody(shape, center, JPH::EMotionType::Static, 0.f, material);
}

EntityId PhysicsWorld::addDynamicSphere(Vec3 center, float radius, float mass, contracts::SurfaceMaterial material) {
    JPH::Ref<JPH::Shape> shape = new JPH::SphereShape(radius);
    return impl_->addBody(shape, center, JPH::EMotionType::Dynamic, mass, material);
}

EntityId PhysicsWorld::addDynamicBox(Vec3 center, Vec3 halfExtents, float mass, contracts::SurfaceMaterial material) {
    JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(toJolt(halfExtents));
    return impl_->addBody(shape, center, JPH::EMotionType::Dynamic, mass, material);
}

void PhysicsWorld::removeBody(EntityId id) {
    auto it = impl_->bodies.find(id);
    if (it == impl_->bodies.end()) return;
    JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
    bi.RemoveBody(it->second);
    bi.DestroyBody(it->second);
    impl_->bodies.erase(it);
    impl_->materials.erase(id);
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
