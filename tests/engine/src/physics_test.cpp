// Тесты физики (E5). Мир на Jolt проверяется только если модуль собран с Jolt (на CI — да, локально без vcpkg — нет).
#include <doctest/doctest.h>

#include <cmath>

#include "han/physics/physics_world.h"

using namespace han;
using contracts::EntityId;
using contracts::SurfaceMaterial;
using contracts::Vec3;

TEST_CASE("physics: hasJolt согласован с макросом сборки") {
#ifdef HAN_HAS_JOLT
    CHECK(physics::hasJolt());
#else
    CHECK_FALSE(physics::hasJolt());
#endif
}

#ifdef HAN_HAS_JOLT
namespace {
std::unique_ptr<physics::PhysicsWorld> makeWorldWithFloor(EntityId* floorOut = nullptr) {
    auto w = physics::PhysicsWorld::create();
    REQUIRE(w);
    const EntityId floor = w->addStaticBox({0, -0.5f, 0}, {50, 0.5f, 50}, SurfaceMaterial::Asphalt);
    REQUIRE(floor != contracts::kNoEntity);
    if (floorOut) *floorOut = floor;
    return w;
}
void run(physics::PhysicsWorld& w, int steps) {
    for (int i = 0; i < steps; ++i) w.step(1.f / 60.f);
}
}  // namespace

TEST_CASE("physics: луч вниз попадает в пол, отдаёт расстояние, нормаль, сущность и материал") {
    EntityId floor = 0;
    auto w = makeWorldWithFloor(&floor);
    w->step(1.f / 60.f);  // оптимизируем широкую фазу
    const auto hit = w->raycast({0, 5, 0}, {0, -5, 0});
    REQUIRE(hit.hit);
    CHECK(hit.distance == doctest::Approx(5.f).epsilon(0.01));
    CHECK(hit.point.y == doctest::Approx(0.f).epsilon(0.01));
    CHECK(hit.normal.y > 0.99f);
    CHECK(hit.entity == floor);
    CHECK(hit.material == SurfaceMaterial::Asphalt);
}

TEST_CASE("physics: луч мимо и нулевой луч не дают попадания; маска слоёв фильтрует") {
    auto w = makeWorldWithFloor();
    w->step(1.f / 60.f);
    CHECK_FALSE(w->raycast({0, 5, 0}, {0, 1, 0}).hit);
    CHECK_FALSE(w->raycast({0, 5, 0}, {0, 5, 0}).hit);
    CHECK(w->raycast({0, 5, 0}, {0, -5, 0}, physics::kLayerStatic).hit);
    CHECK_FALSE(w->raycast({0, 5, 0}, {0, -5, 0}, physics::kLayerMoving).hit);
}

TEST_CASE("physics: шар падает и останавливается на полу") {
    auto w = makeWorldWithFloor();
    const EntityId ball = w->addDynamicSphere({0, 3, 0}, 0.5f, 2.f);
    REQUIRE(ball != contracts::kNoEntity);
    run(*w, 180);
    const Vec3 p = w->positionOf(ball);
    CHECK(p.y == doctest::Approx(0.5f).epsilon(0.1));
    CHECK(std::fabs(w->velocityOf(ball).y) < 0.2f);
}

TEST_CASE("physics: overlapSphere находит тела и считает полное число при малом буфере") {
    auto w = makeWorldWithFloor();
    const EntityId a = w->addStaticBox({0, 1, 0}, {0.5f, 0.5f, 0.5f});
    const EntityId b = w->addStaticBox({1.5f, 1, 0}, {0.5f, 0.5f, 0.5f});
    w->step(1.f / 60.f);
    EntityId out[8] = {};
    CHECK(w->overlapSphere({0.75f, 1.5f, 0}, 0.8f, out, 8) == 2);
    CHECK(((out[0] == a && out[1] == b) || (out[0] == b && out[1] == a)));
    EntityId one[1] = {};
    CHECK(w->overlapSphere({0.75f, 1.5f, 0}, 0.8f, one, 1) == 2);
    CHECK(w->overlapSphere({30, 1, 30}, 0.5f, out, 8) == 0);
}

TEST_CASE("physics: свип капсулы упирается в стену") {
    auto w = makeWorldWithFloor();
    const EntityId wall = w->addStaticBox({5, 1, 0}, {0.25f, 1, 5}, SurfaceMaterial::Concrete);
    w->step(1.f / 60.f);
    const auto hit = w->sweepCapsule({0, 0.05f, 0}, {10, 0.05f, 0}, 0.3f, 1.8f);
    REQUIRE(hit.hit);
    CHECK(hit.entity == wall);
    CHECK(hit.distance > 4.0f);
    CHECK(hit.distance < 4.6f);
    CHECK(hit.normal.x < -0.9f);
    CHECK_FALSE(w->sweepCapsule({0, 0.05f, 20}, {10, 0.05f, 20}, 0.3f, 1.8f).hit);
}

TEST_CASE("physics: персонаж стоит на полу, идёт с заданной скоростью и останавливается") {
    auto w = makeWorldWithFloor();
    const EntityId c = w->create({}, {0, 0.05f, 0});
    REQUIRE(c != contracts::kNoEntity);
    run(*w, 60);
    CHECK(w->state(c).grounded);
    CHECK(w->state(c).position.y == doctest::Approx(0.f).epsilon(0.1).scale(1.0));
    w->setDesiredVelocity(c, {0, 0, 3.f});
    run(*w, 60);
    CHECK(w->state(c).position.z == doctest::Approx(3.f).epsilon(0.15));
    w->setDesiredVelocity(c, {0, 0, 0});
    run(*w, 30);
    const float z = w->state(c).position.z;
    run(*w, 30);
    CHECK(w->state(c).position.z == doctest::Approx(z).epsilon(0.01));
    w->destroy(c);
    CHECK(w->state(c).position.z == 0.f);  // после destroy состояние пустое
}

TEST_CASE("physics: персонаж поднимается на ступеньку ниже stepHeight и не лезет на стену") {
    auto w = makeWorldWithFloor();
    w->addStaticBox({0, 0.15f, 3}, {5, 0.15f, 0.5f});  // ступенька высотой 0.3 м
    w->addStaticBox({0, 1.5f, 8}, {5, 1.5f, 0.5f});    // стена 3 м
    const EntityId c = w->create({}, {0, 0.05f, 0});
    run(*w, 30);
    w->setDesiredVelocity(c, {0, 0, 2.f});
    run(*w, 150);  // 2.5 с -> прошёл ступеньку и упёрся в стену около z = 7.5 - радиус
    const auto s = w->state(c);
    CHECK(s.position.z > 3.3f);
    CHECK(s.position.z < 7.5f);
    CHECK(s.position.y < 0.4f);
}

TEST_CASE("physics: толчок отбрасывает персонажа и затухает") {
    auto w = makeWorldWithFloor();
    const EntityId c = w->create({}, {0, 0.05f, 0});
    run(*w, 30);
    w->applyImpulse(c, {0, 0, 300.f});  // 300 / 75 = 4 м/с
    run(*w, 60);
    const float z = w->state(c).position.z;
    CHECK(z > 0.5f);
    CHECK(z < 4.f);
    run(*w, 60);
    CHECK(w->state(c).position.z == doctest::Approx(z).epsilon(0.05).scale(1.0));
}

TEST_CASE("physics: удаление тела и счётчик тел") {
    auto w = makeWorldWithFloor();
    const EntityId b = w->addDynamicBox({0, 3, 0}, {0.5f, 0.5f, 0.5f});
    CHECK(w->bodyCount() == 2);
    w->removeBody(b);
    CHECK(w->bodyCount() == 1);
    w->removeBody(b);  // повторное удаление безопасно
    CHECK(w->bodyCount() == 1);
}

TEST_CASE("physics: два мира живут одновременно") {
    auto a = makeWorldWithFloor();
    auto b = makeWorldWithFloor();
    a->step(1.f / 60.f);
    b->step(1.f / 60.f);
    a.reset();
    CHECK(b->raycast({0, 5, 0}, {0, -5, 0}).hit);
}
#endif
