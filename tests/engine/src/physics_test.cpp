// Тесты физики (E5). Мир на Jolt проверяется только если модуль собран с Jolt (на CI — да, локально без vcpkg — нет).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

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

TEST_CASE("physics: таблица поверхностей читает data/materials/surfaces.tsv") {
    physics::SurfaceTable t;
    std::string error;
    REQUIRE_MESSAGE(t.load(std::filesystem::path(HAN_REPO_DIR) / "data" / "materials" / "surfaces.tsv", error), error);
    CHECK(t.get(SurfaceMaterial::Metal).restitution > t.get(SurfaceMaterial::Carpet).restitution);
    CHECK(t.get(SurfaceMaterial::Asphalt).friction > t.get(SurfaceMaterial::Water).friction);
    CHECK(t.get(SurfaceMaterial::Wood).impactSound == "phys.impact.wood");
    for (int i = 0; i < physics::SurfaceTable::kCount; ++i) {  // каждый материал контракта есть в таблице
        const auto m = static_cast<SurfaceMaterial>(i);
        CHECK(t.get(m).impactSound != "");
        SurfaceMaterial back;
        REQUIRE(physics::SurfaceTable::materialFromName(physics::SurfaceTable::name(m), back));
        CHECK(back == m);
    }
}

TEST_CASE("physics: таблица поверхностей отклоняет ошибки с номером строки и не портит прежние значения") {
    physics::SurfaceTable t;
    std::string error;
    CHECK_FALSE(t.parse("Wood\t0.5\t0.2\n", error));  // 3 колонки
    CHECK(error.find("строка 1") != std::string::npos);
    CHECK_FALSE(t.parse("# комментарий\nSteel\t0.5\t0.2\tx\n", error));
    CHECK(error.find("строка 2") != std::string::npos);
    CHECK_FALSE(t.parse("Wood\t9\t0.2\tx\n", error));
    CHECK_FALSE(t.parse("Wood\t0.5\t1.5\tx\n", error));
    CHECK_FALSE(t.parse("Wood\t0.5\t0.2\tx\nWood\t0.5\t0.2\ty\n", error));
    CHECK_FALSE(t.parse("Wood\tабв\t0.2\tx\n", error));
    CHECK(t.get(SurfaceMaterial::Wood).friction == doctest::Approx(0.5f));  // значения по умолчанию целы
    REQUIRE(t.parse("Wood\t0.9\t0.3\tphys.x\r\n\r\n", error));
    CHECK(t.get(SurfaceMaterial::Wood).friction == doctest::Approx(0.9f));
    CHECK(t.get(SurfaceMaterial::Wood).impactSound == "phys.x");
    CHECK_FALSE(t.load("нет_такого_файла.tsv", error));
}

#ifdef HAN_HAS_JOLT
namespace {
void run(physics::PhysicsWorld& w, int steps) {
    for (int i = 0; i < steps; ++i) w.step(1.f / 60.f);
}
std::unique_ptr<physics::PhysicsWorld> makeWorldWithTable(const physics::WorldSettings& settings = {}) {
    auto w = physics::PhysicsWorld::create(settings);
    REQUIRE(w);
    physics::SurfaceTable t;
    std::string error;
    REQUIRE_MESSAGE(t.load(std::filesystem::path(HAN_REPO_DIR) / "data" / "materials" / "surfaces.tsv", error), error);
    w->setSurfaceTable(t);
    return w;
}
}  // namespace

TEST_CASE("physics P1: CCD — быстрый шар не пролетает сквозь тонкую стену") {
    physics::WorldSettings s;
    s.gravity = {0, 0, 0};
    auto w = makeWorldWithTable(s);
    w->addStaticBox({10, 0, 0}, {0.05f, 5, 5});  // стена толщиной 10 см
    const EntityId ball = w->addDynamicSphere({0, 0, 0}, 0.1f, 1.f);
    w->setVelocity(ball, {200, 0, 0});  // за один шаг 1/60 с летит 3.3 м, стена в 10 см
    run(*w, 60);
    CHECK(w->positionOf(ball).x < 10.f);
    CHECK(w->allFinite());
}

TEST_CASE("physics P1: упругость из таблицы — металлический шар отскакивает выше ковёрного") {
    auto w = makeWorldWithTable();
    w->addStaticBox({0, -0.5f, 0}, {50, 0.5f, 50}, SurfaceMaterial::Concrete);
    const EntityId metal = w->addDynamicSphere({-3, 3, 0}, 0.25f, 1.f, SurfaceMaterial::Metal);
    const EntityId carpet = w->addDynamicSphere({3, 3, 0}, 0.25f, 1.f, SurfaceMaterial::Carpet);
    float peakMetal = 0, peakCarpet = 0;
    bool bounced = false;
    for (int i = 0; i < 150; ++i) {
        w->step(1.f / 60.f);
        if (w->velocityOf(metal).y > 0.5f) bounced = true;  // после первого удара
        if (bounced) {
            peakMetal = std::max(peakMetal, w->positionOf(metal).y);
            peakCarpet = std::max(peakCarpet, w->positionOf(carpet).y);
        }
    }
    CHECK(peakMetal > peakCarpet + 0.1f);
}

TEST_CASE("physics P1: удар шара об пол даёт событие звука с материалами и скоростью") {
    auto w = makeWorldWithTable();
    const EntityId floor = w->addStaticBox({0, -0.5f, 0}, {50, 0.5f, 50}, SurfaceMaterial::Tile);
    const EntityId ball = w->addDynamicSphere({0, 3, 0}, 0.25f, 1.f, SurfaceMaterial::Metal);
    std::vector<physics::PhysicsWorld::ContactEvent> events;
    for (int i = 0; i < 120; ++i) {
        w->step(1.f / 60.f);
        w->drainContacts(events);
    }
    REQUIRE_FALSE(events.empty());
    const auto& e = events.front();
    CHECK(e.impactSpeed > 5.f);  // падение с 3 м: ~7.5 м/с
    CHECK(((e.a == ball && e.b == floor) || (e.a == floor && e.b == ball)));
    CHECK(((e.materialA == SurfaceMaterial::Metal && e.materialB == SurfaceMaterial::Tile) ||
           (e.materialA == SurfaceMaterial::Tile && e.materialB == SurfaceMaterial::Metal)));
    std::vector<physics::PhysicsWorld::ContactEvent> again;
    w->drainContacts(again);
    CHECK(again.empty());  // очередь после чтения пуста
}

TEST_CASE("physics P1: покоящееся тело не шумит звуком, тихие касания отфильтрованы") {
    auto w = makeWorldWithTable();
    w->addStaticBox({0, -0.5f, 0}, {50, 0.5f, 50});
    w->addDynamicBox({0, 0.5f, 0}, {0.5f, 0.5f, 0.5f});
    std::vector<physics::PhysicsWorld::ContactEvent> events;
    run(*w, 300);
    w->drainContacts(events);
    CHECK(events.empty());
}

TEST_CASE("physics P1: 10 000 кадров, 160 тел в ящике — без NaN, все остаются внутри") {
    physics::WorldSettings s;
    s.collisionSteps = 2;
    auto w = makeWorldWithTable(s);
    w->addStaticBox({0, -0.5f, 0}, {8, 0.5f, 8});
    w->addStaticBox({8.5f, 3, 0}, {0.5f, 4, 8}, SurfaceMaterial::Concrete);
    w->addStaticBox({-8.5f, 3, 0}, {0.5f, 4, 8}, SurfaceMaterial::Concrete);
    w->addStaticBox({0, 3, 8.5f}, {8, 4, 0.5f}, SurfaceMaterial::Concrete);
    w->addStaticBox({0, 3, -8.5f}, {8, 4, 0.5f}, SurfaceMaterial::Concrete);
    std::vector<EntityId> ids;
    for (int i = 0; i < 160; ++i) {
        const float x = -6.f + static_cast<float>(i % 8) * 1.7f, z = -6.f + static_cast<float>((i / 8) % 8) * 1.7f;
        const float y = 1.f + static_cast<float>(i / 64) * 1.5f;
        ids.push_back(i % 2 ? w->addDynamicSphere({x, y, z}, 0.4f, 3.f, SurfaceMaterial::Wood)
                            : w->addDynamicBox({x, y, z}, {0.4f, 0.4f, 0.4f}, 4.f, SurfaceMaterial::Metal));
    }
    const EntityId hero = w->create({}, {0, 0.05f, 0});
    for (int frame = 0; frame < 10000; ++frame) {
        if (frame % 120 == 0) w->setDesiredVelocity(hero, {std::sin(static_cast<float>(frame)) * 3.f, 0, std::cos(static_cast<float>(frame)) * 3.f});
        if (frame % 600 == 0) w->jump(hero, 5.f);
        w->step(1.f / 60.f);
        if (frame % 500 == 0) REQUIRE_MESSAGE(w->allFinite(), "NaN на кадре " << frame);
        if (frame % 100 == 0) {
            std::vector<physics::PhysicsWorld::ContactEvent> sink;
            w->drainContacts(sink);
        }
    }
    CHECK(w->allFinite());
    for (EntityId id : ids) {
        const Vec3 p = w->positionOf(id);
        CHECK(std::fabs(p.x) < 9.f);
        CHECK(std::fabs(p.z) < 9.f);
        CHECK(p.y > -1.f);
    }
}

TEST_CASE("physics P1: allFinite ловит нормальный мир и пустой мир") {
    auto w = physics::PhysicsWorld::create();
    CHECK(w->allFinite());
}

namespace {
std::unique_ptr<physics::PhysicsWorld> makeWorldWithFloor(EntityId* floorOut = nullptr) {
    auto w = physics::PhysicsWorld::create();
    REQUIRE(w);
    const EntityId floor = w->addStaticBox({0, -0.5f, 0}, {50, 0.5f, 50}, SurfaceMaterial::Asphalt);
    REQUIRE(floor != contracts::kNoEntity);
    if (floorOut) *floorOut = floor;
    return w;
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

TEST_CASE("physics: прыжок поднимает персонажа и возвращает на землю; в воздухе прыгнуть нельзя") {
    auto w = makeWorldWithFloor();
    const EntityId c = w->create({}, {0, 0.05f, 0});
    CHECK_FALSE(w->jump(c, 5.f));  // ещё не стоит на земле до первого шага
    run(*w, 30);
    REQUIRE(w->jump(c, 5.f));
    float peak = 0;
    for (int i = 0; i < 20; ++i) {
        w->step(1.f / 60.f);
        peak = std::max(peak, w->state(c).position.y);
        if (i == 5) CHECK_FALSE(w->jump(c, 5.f));
    }
    CHECK(peak > 0.5f);
    run(*w, 120);
    CHECK(w->state(c).grounded);
    CHECK(w->state(c).position.y < 0.2f);
    CHECK_FALSE(w->jump(contracts::kNoEntity, 5.f));
}

TEST_CASE("physics: наклонная статическая коробка работает как рампа, поворот тела читается") {
    auto w = makeWorldWithFloor();
    // Рампа: коробка 4x0.1x2 (полуразмеры), повёрнутая на 15 градусов вокруг Z.
    const float half = 0.5f * 15.f * 3.14159265f / 180.f;
    const contracts::Quat tilt{0, 0, std::sin(half), std::cos(half)};
    w->addStaticBox({5, 0.5f, 0}, {4, 0.1f, 2}, SurfaceMaterial::Wood, tilt);
    const EntityId box = w->addDynamicBox({0, 2, 5}, {0.5f, 0.5f, 0.5f}, 1.f, SurfaceMaterial::Wood, tilt);
    w->step(1.f / 60.f);
    const contracts::Quat q = w->rotationOf(box);
    CHECK(q.z == doctest::Approx(std::sin(half)).epsilon(0.01));
    const auto hit = w->raycast({5, 5, 0}, {5, -5, 0});
    REQUIRE(hit.hit);
    CHECK(hit.material == SurfaceMaterial::Wood);
    CHECK(hit.normal.y == doctest::Approx(std::cos(2 * half)).epsilon(0.02));
    CHECK(std::fabs(hit.normal.x) > 0.2f);
}

TEST_CASE("physics: тело засыпает на полу, isAwake отражает это") {
    auto w = makeWorldWithFloor();
    const EntityId b = w->addDynamicBox({0, 1, 0}, {0.5f, 0.5f, 0.5f});
    CHECK(w->isAwake(b));
    run(*w, 600);
    CHECK_FALSE(w->isAwake(b));
    CHECK_FALSE(w->isAwake(12345));
}

TEST_CASE("physics: debugLines рисует коробки (12 рёбер), шары (3 кольца) и капсулу персонажа") {
    auto w = physics::PhysicsWorld::create();
    std::vector<physics::PhysicsWorld::DebugLine> lines;
    w->debugLines(lines);
    CHECK(lines.empty());
    w->addStaticBox({0, 0, 0}, {1, 1, 1});
    w->debugLines(lines);
    CHECK(lines.size() == 12);
    for (const auto& l : lines) CHECK(l.color == physics::PhysicsWorld::kColorStatic);
    lines.clear();
    w->addDynamicSphere({0, 5, 0}, 0.5f);
    w->debugLines(lines);
    CHECK(lines.size() == 12 + 3 * 16);
    lines.clear();
    w->create({}, {10, 0, 10});
    w->debugLines(lines);
    CHECK(lines.size() > 12 + 3 * 16);
    bool hasCharacter = false;
    for (const auto& l : lines) hasCharacter = hasCharacter || l.color == physics::PhysicsWorld::kColorCharacter;
    CHECK(hasCharacter);
}

namespace {
// Дорога для машин: асфальт из таблицы поверхностей (трение 0.8), пол 1200 x 1200 м.
std::unique_ptr<physics::PhysicsWorld> makeRoad() {
    auto w = makeWorldWithTable();
    w->addStaticBox({0, -0.5f, 0}, {600, 0.5f, 600}, SurfaceMaterial::Asphalt);
    return w;
}
}  // namespace

namespace {
using VInput = physics::PhysicsWorld::VehicleInput;
// Машина на плоской земле, нос смотрит вдоль +Z.
EntityId spawnCar(physics::PhysicsWorld& w, physics::PhysicsWorld::Drive drive = physics::PhysicsWorld::Drive::Rear) {
    physics::PhysicsWorld::VehicleDesc d;
    d.drive = drive;
    const EntityId car = w.createVehicle(d, {0, 1.0f, 0});
    REQUIRE(car != contracts::kNoEntity);
    return car;
}
float yawOf(const contracts::Quat& q) { return std::atan2(2.f * (q.w * q.y + q.x * q.z), 1.f - 2.f * (q.y * q.y + q.x * q.x)); }
}  // namespace

TEST_CASE("physics E12: машина стоит на четырёх колёсах на земле и не проваливается") {
    auto w = makeRoad();
    const EntityId car = spawnCar(*w);
    run(*w, 180);
    const auto s = w->vehicleState(car);
    REQUIRE(s.valid);
    for (int i = 0; i < 4; ++i) CHECK_MESSAGE(s.wheelContact[i], "колесо " << i << " не касается земли");
    CHECK(s.position.y > 0.3f);
    CHECK(s.position.y < 1.2f);
    CHECK(std::fabs(s.speed) < 0.1f);
}

TEST_CASE("physics E12: полный газ — машина разгоняется, переключает передачи, не переворачивается") {
    auto w = makeRoad();
    const EntityId car = spawnCar(*w);
    run(*w, 60);
    int maxGear = 0;
    float maxRpm = 0.f;
    for (int i = 0; i < 480; ++i) {  // 8 секунд
        w->setVehicleInput(car, VInput{1.f, 0.f, 0.f, 0.f});
        w->step(1.f / 60.f);
        const auto s = w->vehicleState(car);
        maxGear = std::max(maxGear, s.gear);
        maxRpm = std::max(maxRpm, s.rpm);
    }
    const auto s = w->vehicleState(car);
    CHECK(s.speed > 15.f);                 // больше 54 км/ч
    CHECK(maxGear >= 2);
    CHECK(maxRpm <= 6100.f);
    CHECK(maxRpm > 2500.f);
    CHECK(s.position.z > 60.f);            // уехала вперёд (+Z — нос)
    CHECK(std::fabs(s.position.x) < 3.f);  // прямо, без ухода в сторону
    const contracts::Quat q = s.rotation;  // не перевернулась: ось «вверх» машины смотрит вверх
    CHECK(1.f - 2.f * (q.x * q.x + q.z * q.z) > 0.95f);
    CHECK(w->allFinite());
}

TEST_CASE("physics E12: руль поворачивает машину в нужную сторону") {
    const auto turn = [](float steer) {
        auto w = makeRoad();
        const EntityId car = spawnCar(*w);
        run(*w, 60);
        for (int i = 0; i < 300; ++i) {
            w->setVehicleInput(car, VInput{i < 120 ? 1.f : 0.4f, steer, 0.f, 0.f});
            w->step(1.f / 60.f);
        }
        return yawOf(w->vehicleState(car).rotation);
    };
    const float left = turn(-1.f), right = turn(1.f), straight = turn(0.f);
    CHECK(std::fabs(straight) < 0.1f);
    CHECK(std::fabs(right - left) > 1.0f);  // заметно разные направления поворота
    CHECK(right * left < 0.f);              // противоположные знаки
}

TEST_CASE("physics E12: тормоз останавливает, задний ход едет назад, газ против движения тормозит") {
    auto w = makeRoad();
    const EntityId car = spawnCar(*w);
    run(*w, 60);
    for (int i = 0; i < 360; ++i) {
        w->setVehicleInput(car, VInput{1.f, 0.f, 0.f, 0.f});
        w->step(1.f / 60.f);
    }
    REQUIRE(w->vehicleState(car).speed > 10.f);
    for (int i = 0; i < 360; ++i) {
        w->setVehicleInput(car, VInput{0.f, 0.f, 1.f, 0.f});
        w->step(1.f / 60.f);
    }
    CHECK(std::fabs(w->vehicleState(car).speed) < 0.5f);
    for (int i = 0; i < 240; ++i) {  // задний ход с места
        w->setVehicleInput(car, VInput{-1.f, 0.f, 0.f, 0.f});
        w->step(1.f / 60.f);
    }
    CHECK(w->vehicleState(car).speed < -3.f);
    CHECK(w->vehicleState(car).gear == -1);
}

TEST_CASE("physics E12: ручник — машина резко теряет скорость") {
    auto w = makeRoad();
    const EntityId car = spawnCar(*w);
    run(*w, 60);
    for (int i = 0; i < 300; ++i) {
        w->setVehicleInput(car, VInput{1.f, 0.f, 0.f, 0.f});
        w->step(1.f / 60.f);
    }
    const float fast = w->vehicleState(car).speed;
    for (int i = 0; i < 120; ++i) {
        w->setVehicleInput(car, VInput{0.f, 0.5f, 0.f, 1.f});
        w->step(1.f / 60.f);
    }
    CHECK(w->vehicleState(car).speed < fast - 5.f);
    CHECK(w->allFinite());
}

TEST_CASE("physics E12: передний и полный привод тоже едут") {
    for (auto drive : {physics::PhysicsWorld::Drive::Front, physics::PhysicsWorld::Drive::All}) {
        auto w = makeRoad();
        const EntityId car = spawnCar(*w, drive);
        run(*w, 60);
        for (int i = 0; i < 300; ++i) {
            w->setVehicleInput(car, VInput{1.f, 0.f, 0.f, 0.f});
            w->step(1.f / 60.f);
        }
        CHECK(w->vehicleState(car).speed > 8.f);
    }
}

TEST_CASE("physics E12: удар в стену на скорости — машина останавливается, мир конечен; удаление машины") {
    auto w = makeRoad();
    w->addStaticBox({0, 2, 100}, {10, 2, 0.5f});
    const EntityId car = spawnCar(*w);
    run(*w, 60);
    for (int i = 0; i < 900; ++i) {
        w->setVehicleInput(car, VInput{1.f, 0.f, 0.f, 0.f});
        w->step(1.f / 60.f);
    }
    const auto s = w->vehicleState(car);
    CHECK(s.position.z < 100.f);
    CHECK(std::fabs(s.speed) < 6.f);
    CHECK(w->allFinite());
    const auto bodiesBefore = w->bodyCount();
    w->destroyVehicle(car);
    CHECK_FALSE(w->vehicleState(car).valid);
    CHECK(w->bodyCount() == bodiesBefore - 1);
    run(*w, 60);
    w->destroyVehicle(car);  // повторное удаление безопасно
    CHECK(w->allFinite());
}

TEST_CASE("physics E12: 20 секунд случайного вождения — без NaN, машина остаётся в мире") {
    auto w = makeRoad();
    const EntityId car = spawnCar(*w);
    unsigned seed = 12345;
    const auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.f * 2.f - 1.f;
    };
    float throttle = 0, steer = 0, brake = 0;
    for (int i = 0; i < 1200; ++i) {
        if (i % 45 == 0) {
            throttle = rnd();
            steer = rnd();
            brake = rnd() > 0.7f ? 1.f : 0.f;
        }
        w->setVehicleInput(car, VInput{throttle, steer, brake, 0.f});
        w->step(1.f / 60.f);
        if (i % 100 == 0) REQUIRE(w->allFinite());
    }
    const auto s = w->vehicleState(car);
    CHECK(std::fabs(s.position.x) < 50.f);
    CHECK(std::fabs(s.position.z) < 200.f);
    CHECK(s.position.y > -1.f);
}

#endif
