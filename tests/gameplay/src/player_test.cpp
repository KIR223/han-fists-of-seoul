// Тесты управления персонажем и паркура v1 (E6) на настоящей физике (Jolt): разгон, спринт, прыжок, койот-время, буфер, ступени, перепрыгивание, запрыгивание.
#include <doctest/doctest.h>

#include <cmath>
#include <memory>

#include "han/player/player_controller.h"

#ifdef HAN_HAS_JOLT
#include "han/physics/physics_world.h"

using namespace han::player;
using han::contracts::EntityId;
using han::contracts::Vec3;
using han::physics::PhysicsWorld;

namespace {
constexpr float kDt = 1.f / 60.f;

struct World {
    std::unique_ptr<PhysicsWorld> physics = PhysicsWorld::create();
    EntityId hero = 0;
    std::unique_ptr<PlayerController> player;
    PlayerSettings settings;
    World() {
        REQUIRE(physics);
        physics->addStaticBox({0, -0.5f, 0}, {200, 0.5f, 200});
    }
    void spawn(Vec3 at = {0, 0.05f, 0}) {
        hero = physics->create({}, at);
        REQUIRE(hero != 0);
        player = std::make_unique<PlayerController>(hero, *physics, *physics, settings);
        for (int i = 0; i < 30; ++i) physics->step(kDt);  // осесть
    }
    // Кадр: ввод, потом шаг физики. Камера смотрит вдоль −Z (yaw 0), если не задано иное.
    void frame(const PlayerInput& in, float yaw = 0.f) {
        player->update(kDt, in, yaw);
        physics->step(kDt);
    }
    void run(int frames, const PlayerInput& in, float yaw = 0.f) {
        for (int i = 0; i < frames; ++i) frame(in, yaw);
    }
    han::contracts::CharacterState st() const { return physics->state(hero); }
};

PlayerInput forward(bool sprint = false) {
    PlayerInput in;
    in.moveZ = 1.f;
    in.sprint = sprint;
    return in;
}
PlayerInput jump() {
    PlayerInput in;
    in.jumpPressed = true;
    in.jumpHeld = true;
    return in;
}
}  // namespace

TEST_CASE("player: стоит на месте без ввода") {
    World w;
    w.spawn();
    w.run(120, {});
    CHECK(w.st().grounded);
    CHECK(std::fabs(w.st().position.x) < 0.01f);
    CHECK(std::fabs(w.st().position.z) < 0.01f);
    CHECK(w.player->action() == PlayerAction::Idle);
    CHECK(w.player->horizontalSpeed() < 0.05f);
}

TEST_CASE("player: вперёд — по взгляду камеры (−Z), разгон до бега за долю секунды") {
    World w;
    w.spawn();
    w.run(8, forward());
    CHECK(w.player->horizontalSpeed() < w.settings.runSpeed);  // ещё разгоняется
    w.run(60, forward());
    CHECK(w.player->horizontalSpeed() == doctest::Approx(w.settings.runSpeed).epsilon(0.05));
    CHECK(w.st().position.z < -3.f);
    CHECK(std::fabs(w.st().position.x) < 0.05f);
    CHECK(w.player->action() == PlayerAction::Running);
}

TEST_CASE("player: направление зависит от поворота камеры — yaw 90° вперёд значит на +X") {
    World w;
    w.spawn();
    w.run(90, forward(), 1.5707963f);
    CHECK(w.st().position.x > 3.f);
    CHECK(std::fabs(w.st().position.z) < 0.1f);
    World back;
    back.spawn();
    PlayerInput in;
    in.moveZ = -1.f;
    back.run(90, in, 0.f);
    CHECK(back.st().position.z > 3.f);
    World strafe;
    strafe.spawn();
    PlayerInput right;
    right.moveX = 1.f;
    strafe.run(90, right, 0.f);
    CHECK(strafe.st().position.x > 3.f);
}

TEST_CASE("player: спринт быстрее бега, диагональ не быстрее прямой") {
    World run, sprint, diag;
    run.spawn();
    sprint.spawn();
    diag.spawn();
    run.run(120, forward());
    sprint.run(120, forward(true));
    PlayerInput d;
    d.moveX = 1.f;
    d.moveZ = 1.f;
    diag.run(120, d);
    CHECK(sprint.player->horizontalSpeed() > run.player->horizontalSpeed() * 1.3f);
    CHECK(sprint.player->action() == PlayerAction::Sprinting);
    CHECK(diag.player->horizontalSpeed() == doctest::Approx(run.player->horizontalSpeed()).epsilon(0.05));
    CHECK(sprint.player->speedFraction() > run.player->speedFraction());
    CHECK(sprint.player->speedFraction() <= 1.f);
}

TEST_CASE("player: лёгкий ввод — шаг, а не бег; остановка быстрая (торможение)") {
    World w;
    w.spawn();
    PlayerInput slow;
    slow.moveZ = 0.3f;
    w.run(90, slow);
    CHECK(w.player->horizontalSpeed() < w.settings.walkSpeed + 0.05f);
    CHECK(w.player->horizontalSpeed() > 0.5f);
    w.run(60, forward());
    REQUIRE(w.player->horizontalSpeed() > 4.f);
    w.run(20, {});  // 1/3 секунды
    CHECK(w.player->horizontalSpeed() < 0.3f);
}

TEST_CASE("player: тело поворачивается по ходу движения") {
    World w;
    w.spawn();
    w.run(60, forward(), 0.f);
    CHECK(std::fabs(w.player->facingYaw()) < 0.1f);
    PlayerInput right;
    right.moveX = 1.f;
    w.run(90, right, 0.f);
    CHECK(w.player->facingYaw() == doctest::Approx(1.5707963f).epsilon(0.1));
}

TEST_CASE("player: прыжок поднимает на ожидаемую высоту и возвращает на землю; в воздухе повторный прыжок невозможен") {
    World w;
    w.spawn();
    w.frame(jump());
    float peak = 0.f;
    bool secondJumpWorked = false;
    for (int i = 0; i < 90; ++i) {
        w.frame(i == 10 ? jump() : PlayerInput{});  // нажатие в воздухе
        peak = std::max(peak, w.st().position.y);
        if (i > 40 && w.st().velocity.y > 1.f) secondJumpWorked = true;
    }
    const float expected = w.settings.jumpSpeed * w.settings.jumpSpeed / (2.f * w.settings.gravity);
    CHECK(peak > expected * 0.8f);
    CHECK(peak < expected * 1.25f);
    CHECK_FALSE(secondJumpWorked);
    CHECK(w.st().grounded);
    CHECK(w.st().position.y < 0.2f);
}

TEST_CASE("player: койот-время — прыжок сразу после схода с края работает, позже — нет") {
    const auto attempt = [](int framesAfterEdge) {
        World w;
        w.physics->addStaticBox({0, 0.5f, -2.5f}, {3, 0.5f, 1.5f});  // площадка высотой 1 м, край на z = -1 (со стороны старта)
        w.spawn({0, 1.05f, -2.5f});
        // Идём к краю (+Z) и сходим.
        PlayerInput back;
        back.moveZ = -1.f;
        const float yaw = 3.1415927f;  // «вперёд» = +Z
        PlayerInput walk;
        walk.moveZ = 1.f;
        int guard = 0;
        while (w.st().grounded && guard++ < 400) w.frame(walk, yaw);
        REQUIRE(!w.st().grounded);
        for (int i = 0; i < framesAfterEdge; ++i) w.frame({}, yaw);
        const float vyBefore = w.st().velocity.y;
        w.frame(jump(), yaw);
        w.frame({}, yaw);
        return w.st().velocity.y > vyBefore + 2.f;
    };
    CHECK(attempt(2));
    CHECK_FALSE(attempt(15));  // койот 0,12 с = 7 кадров; падение с 1 м длится ~27 кадров: ещё в воздухе, но поздно
}

TEST_CASE("player: буфер прыжка — нажатие чуть раньше приземления срабатывает при касании земли") {
    World w;
    w.spawn();
    w.frame(jump());
    // Ждём, пока падение близко к земле, и нажимаем заранее.
    bool pressed = false, rebounded = false;
    for (int i = 0; i < 120; ++i) {
        PlayerInput in;
        if (!pressed && w.st().velocity.y < -1.f && w.st().position.y < 0.3f) {
            in.jumpPressed = true;
            pressed = true;
        }
        w.frame(in);
        if (pressed && w.st().velocity.y > 2.f) rebounded = true;
    }
    CHECK(pressed);
    CHECK(rebounded);
}

TEST_CASE("player: ступени ниже шага проходятся без прыжка") {
    World w;
    for (int i = 0; i < 6; ++i) w.physics->addStaticBox({0, 0.075f * static_cast<float>(i + 1), -3.f - 0.6f * static_cast<float>(i)}, {2, 0.075f * static_cast<float>(i + 1), 0.3f});
    w.physics->addStaticBox({0, 0.45f, -11.3f}, {2, 0.45f, 5.f});  // площадка на верху лестницы (0,9 м)
    w.spawn();
    w.run(150, forward());  // 2,5 с: поднялся по лестнице и вышел на площадку (она кончается на z = -16,3)
    w.run(30, {});
    CHECK(w.st().position.y == doctest::Approx(0.9f).epsilon(0.05));
    CHECK(w.st().position.z < -6.f);
}

// ---------------------------------------------------------------- Паркур

TEST_CASE("parkour: поиск препятствия — высота и вид (перепрыгивание, запрыгивание, слишком высоко, ступенька)") {
    World w;
    PlayerSettings s;
    w.physics->addStaticBox({0, 0.5f, -2.f}, {2, 0.5f, 0.3f});    // высота 1.0
    w.physics->addStaticBox({10, 0.9f, -2.f}, {2, 0.9f, 0.3f});   // 1.8
    w.physics->addStaticBox({20, 1.5f, -2.f}, {2, 1.5f, 0.3f});   // 3.0: слишком высоко
    w.physics->addStaticBox({30, 0.15f, -2.f}, {2, 0.15f, 0.3f}); // 0.3: ступенька
    w.physics->step(kDt);
    const Vec3 dir{0, 0, -1};
    const auto vault = PlayerController::probe(*w.physics, {0, 0, -0.8f}, dir, s);
    CHECK(vault.kind == ParkourKind::Vault);
    CHECK(vault.topHeight == doctest::Approx(1.0f).epsilon(0.05));
    const auto mantle = PlayerController::probe(*w.physics, {10, 0, -0.8f}, dir, s);
    CHECK(mantle.kind == ParkourKind::Mantle);
    CHECK(mantle.topHeight == doctest::Approx(1.8f).epsilon(0.05));
    CHECK(PlayerController::probe(*w.physics, {20, 0, -0.8f}, dir, s).kind == ParkourKind::None);
    CHECK(PlayerController::probe(*w.physics, {30, 0, -0.8f}, dir, s).kind == ParkourKind::None);
    CHECK(PlayerController::probe(*w.physics, {0, 0, 5.f}, dir, s).kind == ParkourKind::None);  // впереди пусто
    CHECK(PlayerController::probe(*w.physics, {0, 0, -0.8f}, {0, 0, 0}, s).kind == ParkourKind::None);  // нет направления
}

TEST_CASE("parkour: пологий склон не считается препятствием") {
    World w;
    PlayerSettings s;
    const float h = 0.5f * 20.f * 3.14159265f / 180.f;
    w.physics->addStaticBox({0, 1.0f, -4.f}, {3, 0.1f, 4.f}, han::contracts::SurfaceMaterial::Concrete, {std::sin(h), 0, 0, std::cos(h)});  // рампа 20°
    w.physics->step(kDt);
    CHECK(PlayerController::probe(*w.physics, {0, 0, -0.2f}, {0, 0, -1}, s).kind == ParkourKind::None);
}

TEST_CASE("parkour: над уступом нет места встать — запрыгивать некуда") {
    World w;
    PlayerSettings s;
    w.physics->addStaticBox({0, 0.9f, -2.f}, {2, 0.9f, 0.6f});   // уступ 1.8
    w.physics->addStaticBox({0, 3.0f, -2.f}, {2, 0.3f, 1.0f});   // потолок на 2.7..3.3 — над краем всего 0.9 м
    w.physics->step(kDt);
    CHECK(PlayerController::probe(*w.physics, {0, 0, -0.2f}, {0, 0, -1}, s).kind == ParkourKind::None);
}

TEST_CASE("parkour: на бегу прыжок перед низким препятствием перепрыгивает его") {
    World w;
    w.physics->addStaticBox({0, 0.5f, -8.f}, {3, 0.5f, 0.4f});  // ящик высотой 1 м и толщиной 0,8 м
    w.spawn();
    bool vaulted = false;
    for (int i = 0; i < 400; ++i) {
        PlayerInput in = forward();
        const auto pos = w.st().position;
        if (pos.z < -6.8f && pos.z > -7.3f) in.jumpPressed = true;  // у самого препятствия: лицо ящика на z = -7,6
        w.frame(in);
        vaulted = vaulted || w.player->action() == PlayerAction::Vaulting;
        if (pos.z < -11.f) break;
    }
    CHECK(vaulted);
    CHECK(w.st().position.z < -8.5f);  // оказался за ящиком
}

TEST_CASE("parkour: без паркура (выключен) тот же прыжок упирается в препятствие") {
    World w;
    w.settings.parkour = false;
    w.physics->addStaticBox({0, 0.5f, -8.f}, {3, 0.5f, 0.4f});
    w.spawn();
    for (int i = 0; i < 300; ++i) {
        PlayerInput in = forward();
        const auto pos = w.st().position;
        if (pos.z < -6.8f && pos.z > -7.3f) in.jumpPressed = true;
        w.frame(in);
    }
    // Обычный прыжок высотой около 1,4 м тоже перелетает метровый ящик на разбеге, но не гарантированно: главное — паркур не включался.
    CHECK(w.player->action() != PlayerAction::Vaulting);
}

TEST_CASE("parkour: уступ 1,8 м — персонаж запрыгивает и остаётся стоять наверху") {
    World w;
    w.physics->addStaticBox({0, 0.9f, -6.f}, {3, 0.9f, 2.f});  // широкая платформа высотой 1.8 м
    w.spawn();
    bool mantled = false;
    for (int i = 0; i < 400; ++i) {
        PlayerInput in = forward();
        if (w.st().position.z < -3.4f && w.st().position.y < 0.2f) in.jumpPressed = true;
        if (w.st().grounded && w.st().position.y > 1.7f && w.st().position.z < -4.6f) in = {};  // забрался и встал: платформа широкая, но не бесконечная
        w.frame(in);
        mantled = mantled || w.player->action() == PlayerAction::Mantling;
    }
    CHECK(mantled);
    w.run(60, {});
    CHECK(w.st().position.y == doctest::Approx(1.8f).epsilon(0.05));
    CHECK(w.st().grounded);
    CHECK(w.st().position.z < -4.2f);  // на платформе, а не перед ней
}

TEST_CASE("parkour: стена 3 м — паркур не включается, персонаж просто прыгает на месте") {
    World w;
    w.physics->addStaticBox({0, 1.5f, -6.f}, {5, 1.5f, 0.5f});
    w.spawn();
    for (int i = 0; i < 300; ++i) {
        PlayerInput in = forward();
        if (w.st().position.z < -4.8f && i % 40 == 0) in.jumpPressed = true;
        w.frame(in);
        CHECK(w.player->action() != PlayerAction::Mantling);
    }
    CHECK(w.st().position.z > -5.6f);
    CHECK(w.st().position.y < 1.6f);
}

TEST_CASE("parkour: перепрыгивание — только на ходу, с места перед препятствием 1 м — обычный прыжок") {
    World w;
    w.physics->addStaticBox({0, 0.5f, -2.f}, {3, 0.5f, 0.4f});
    w.spawn({0, 0.05f, -0.9f});
    PlayerInput in = jump();
    in.moveZ = 0.f;
    w.frame(in);
    CHECK(w.player->action() != PlayerAction::Vaulting);
}

TEST_CASE("parkour: после паркура есть пауза, повторное срабатывание не мгновенное") {
    World w;
    w.physics->addStaticBox({0, 0.5f, -8.f}, {3, 0.5f, 0.4f});
    w.spawn();
    int vaultStarts = 0;
    PlayerAction previous = PlayerAction::Idle;
    for (int i = 0; i < 300; ++i) {
        PlayerInput in = forward();
        in.jumpPressed = (i % 5 == 0) && w.st().position.z < -6.8f;  // долбим прыжок у самого ящика
        w.frame(in);
        if (w.player->action() == PlayerAction::Vaulting && previous != PlayerAction::Vaulting) ++vaultStarts;
        previous = w.player->action();
    }
    CHECK(vaultStarts >= 1);
    CHECK(vaultStarts <= 2);
}

TEST_CASE("player: 20 секунд случайного управления — персонаж остаётся в мире, без NaN") {
    World w;
    w.physics->addStaticBox({5, 0.5f, -5.f}, {2, 0.5f, 0.4f});
    w.physics->addStaticBox({-5, 0.9f, -8.f}, {2, 0.9f, 2.f});
    w.spawn();
    unsigned seed = 77;
    const auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 8) & 0xFFFF);
    };
    PlayerInput in;
    float yaw = 0.f;
    for (int i = 0; i < 1200; ++i) {
        if (i % 30 == 0) {
            in.moveX = static_cast<float>(rnd() % 3) - 1.f;
            in.moveZ = static_cast<float>(rnd() % 3) - 1.f;
            in.sprint = rnd() % 4 == 0;
            yaw += (static_cast<float>(rnd() % 100) - 50.f) * 0.02f;
        }
        in.jumpPressed = rnd() % 25 == 0;
        w.frame(in, yaw);
        REQUIRE(std::isfinite(w.st().position.x));
        REQUIRE(std::isfinite(w.st().position.y));
    }
    CHECK(w.st().position.y > -1.f);
    CHECK(w.physics->allFinite());
}
#else
TEST_CASE("player: без Jolt тесты управления пропущены") { CHECK(true); }
#endif
