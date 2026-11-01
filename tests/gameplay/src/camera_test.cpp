// Тесты камеры третьего лица (E6): углы, следование, независимость от частоты кадров, столкновение со стеной, обзор на бегу.
#include <doctest/doctest.h>

#include <cmath>

#include "han/player/camera_rig.h"
#ifdef HAN_HAS_JOLT
#include "han/physics/physics_world.h"
#endif

using namespace han::player;
using han::contracts::RayHit;
using han::contracts::Vec3;

namespace {
// Заглушка мира: «стена» — плоскость z = wallZ; всё, что дальше (по -Z... нет: по +Z) от неё, недоступно. Для проверки математики камеры без физики.
struct WallQuery final : han::contracts::IPhysicsQuery {
    float wallZ;
    explicit WallQuery(float z) : wallZ(z) {}
    RayHit raycast(Vec3 from, Vec3 to, std::uint32_t) const override {
        RayHit h;
        const bool crosses = (from.z - wallZ) * (to.z - wallZ) < 0.f;
        if (!crosses) return h;
        const float t = (wallZ - from.z) / (to.z - from.z);
        h.hit = true;
        h.point = {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, wallZ};
        const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
        h.distance = t * std::sqrt(dx * dx + dy * dy + dz * dz);
        h.normal = {0, 0, from.z < wallZ ? -1.f : 1.f};
        return h;
    }
    RayHit sweepCapsule(Vec3, Vec3, float, float, std::uint32_t) const override { return {}; }
    std::uint32_t overlapSphere(Vec3, float, han::contracts::EntityId*, std::uint32_t, std::uint32_t) const override { return 0; }
};

float dist(Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
}  // namespace

TEST_CASE("camera: соглашения как у камеры движка — yaw 0 смотрит вдоль −Z, положительный yaw к +X, наклон вверх положителен") {
    CameraRig rig;
    rig.setAngles(0.f, 0.f);
    CHECK(rig.forward().z == doctest::Approx(-1.f));
    rig.setAngles(1.5707963f, 0.f);
    CHECK(rig.forward().x == doctest::Approx(1.f));
    rig.setAngles(0.f, 0.5f);
    CHECK(rig.forward().y > 0.f);
}

TEST_CASE("camera: мышь вправо поворачивает вправо, вниз — взгляд вниз; наклон зажат пределами") {
    CameraRig rig;
    rig.setAngles(0.f, 0.f);
    rig.look(100.f, 0.f);
    CHECK(rig.yaw() == doctest::Approx(100.f * rig.settings().sensitivity));
    rig.setAngles(0.f, 0.f);
    rig.look(0.f, 100.f);
    CHECK(rig.pitch() < 0.f);
    rig.look(0.f, 100000.f);
    CHECK(rig.pitch() == doctest::Approx(rig.settings().minPitch));
    rig.look(0.f, -1000000.f);
    CHECK(rig.pitch() == doctest::Approx(rig.settings().maxPitch));
    rig.setAngles(0.f, 99.f);
    CHECK(rig.pitch() == doctest::Approx(rig.settings().maxPitch));
}

TEST_CASE("camera: поворот не накапливает огромный угол при долгом вращении мышью") {
    CameraRig rig;
    for (int i = 0; i < 100000; ++i) rig.look(50.f, 0.f);
    CHECK(std::fabs(rig.yaw()) <= 6.2831853f + 1e-3f);
}

TEST_CASE("camera: камера стоит позади персонажа на заданном расстоянии, чуть правее (из-за плеча)") {
    CameraRigSettings s;
    s.shoulderOffset = 0.f;
    CameraRig rig(s);
    rig.setAngles(0.f, 0.f);
    rig.snapTo({0, 0, 0}, nullptr);
    CHECK(dist(rig.position(), rig.pivot()) == doctest::Approx(s.distance).epsilon(0.001));
    CHECK(rig.position().z > rig.pivot().z);  // позади: взгляд −Z, камера на +Z
    CHECK(rig.pivot().y == doctest::Approx(s.pivotHeight));
    CameraRig shoulder;
    shoulder.setAngles(0.f, 0.f);
    shoulder.snapTo({0, 0, 0}, nullptr);
    CHECK(shoulder.position().x > 0.f);
}

TEST_CASE("camera: следует за персонажем и догоняет его, не отставая навсегда") {
    CameraRig rig;
    rig.snapTo({0, 0, 0}, nullptr);
    for (int i = 0; i < 180; ++i) rig.update(1.f / 60.f, {10, 0, -20}, 0.f, nullptr);
    CHECK(rig.pivot().x == doctest::Approx(10.f).epsilon(0.001));
    CHECK(rig.pivot().z == doctest::Approx(-20.f).epsilon(0.001));
    CHECK(dist(rig.position(), rig.pivot()) == doctest::Approx(rig.settings().distance).epsilon(0.05));
}

TEST_CASE("camera: сглаживание не зависит от частоты кадров (секунда на 30, 60 и 144 Гц даёт один результат)") {
    const auto run = [](float fps) {
        CameraRig rig;
        rig.snapTo({0, 0, 0}, nullptr);
        const int n = static_cast<int>(fps * 0.25f);
        for (int i = 0; i < n; ++i) rig.update(1.f / fps, {5, 0, 0}, 0.f, nullptr);
        return rig.pivot().x;
    };
    CHECK(run(30.f) == doctest::Approx(run(144.f)).epsilon(0.02));
    CHECK(run(60.f) == doctest::Approx(run(144.f)).epsilon(0.02));
}

TEST_CASE("camera: обзор расширяется на бегу и возвращается") {
    CameraRig rig;
    rig.snapTo({0, 0, 0}, nullptr);
    for (int i = 0; i < 120; ++i) rig.update(1.f / 60.f, {0, 0, 0}, 1.f, nullptr);
    CHECK(rig.fov() == doctest::Approx(rig.settings().baseFov + rig.settings().sprintFovBonus).epsilon(0.01));
    for (int i = 0; i < 240; ++i) rig.update(1.f / 60.f, {0, 0, 0}, 0.f, nullptr);
    CHECK(rig.fov() == doctest::Approx(rig.settings().baseFov).epsilon(0.01));
    rig.update(1.f / 60.f, {0, 0, 0}, 5.f, nullptr);  // доля вне 0..1 зажимается
    CHECK(rig.fov() <= rig.settings().baseFov + rig.settings().sprintFovBonus + 1e-4f);
}

TEST_CASE("camera: стена позади — камера подтягивается ближе и не заходит за неё") {
    CameraRigSettings s;
    s.shoulderOffset = 0.f;
    CameraRig rig(s);
    rig.setAngles(0.f, 0.f);
    WallQuery wall(1.5f);  // камера хочет быть в 3,6 м позади (z = 3,6), стена на z = 1,5
    rig.snapTo({0, 0, 0}, &wall);
    CHECK(rig.position().z < 1.5f);
    CHECK(rig.position().z == doctest::Approx(1.5f - s.collisionMargin).epsilon(0.02));
    CHECK(dist(rig.position(), rig.pivot()) >= s.minDistance);
}

TEST_CASE("camera: в упор к стене камера не ближе минимального расстояния") {
    CameraRigSettings s;
    s.shoulderOffset = 0.f;
    CameraRig rig(s);
    rig.setAngles(0.f, 0.f);
    WallQuery wall(0.2f);
    rig.snapTo({0, 0, 0}, &wall);
    CHECK(dist(rig.position(), rig.pivot()) >= s.minDistance - 1e-4f);
}

TEST_CASE("camera: приближение к стене мгновенное, отдаление — плавное") {
    CameraRigSettings s;
    s.shoulderOffset = 0.f;
    CameraRig rig(s);
    rig.setAngles(0.f, 0.f);
    rig.snapTo({0, 0, 0}, nullptr);
    const float before = dist(rig.position(), rig.pivot());
    WallQuery wall(1.5f);
    rig.update(1.f / 60.f, {0, 0, 0}, 0.f, &wall);
    const float near = dist(rig.position(), rig.pivot());
    CHECK(near < before - 1.f);  // сразу
    rig.update(1.f / 60.f, {0, 0, 0}, 0.f, nullptr);  // стена пропала
    const float afterOne = dist(rig.position(), rig.pivot());
    CHECK(afterOne > near);
    CHECK(afterOne < before - 0.5f);  // за кадр далеко не вернулась
    for (int i = 0; i < 240; ++i) rig.update(1.f / 60.f, {0, 0, 0}, 0.f, nullptr);
    CHECK(dist(rig.position(), rig.pivot()) == doctest::Approx(before).epsilon(0.02));
}

#ifdef HAN_HAS_JOLT
TEST_CASE("camera: настоящая физика — камера не оказывается внутри здания за спиной") {
    auto world = han::physics::PhysicsWorld::create();
    REQUIRE(world);
    world->addStaticBox({0, -0.5f, 0}, {50, 0.5f, 50});
    world->addStaticBox({0, 2, 2.2f}, {10, 2, 0.3f});  // стена за спиной, на z = 1,9..2,5
    world->step(1.f / 60.f);
    CameraRigSettings s;
    s.shoulderOffset = 0.f;
    CameraRig rig(s);
    rig.setAngles(0.f, 0.f);
    rig.snapTo({0, 0, 0}, world.get());
    for (int i = 0; i < 120; ++i) rig.update(1.f / 60.f, {0, 0, 0}, 0.f, world.get());
    CHECK(rig.position().z < 1.9f);
    han::contracts::EntityId found[4];
    CHECK(world->overlapSphere(rig.position(), 0.05f, found, 4) == 0);  // в точке камеры нет геометрии
}
#endif
