// Тесты моста боя и мира: позиции из мира, святое место глушит атаки, отброс и доворот уходят в мир.
#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <string>

#include "contracts/stubs/combat_world_stub.h"
#include "han/combat/world_bridge.h"

using namespace han::combat;
namespace c = han::contracts;

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }

struct FakeWorld final : c::ICombatWorld {
    std::map<c::EntityId, c::Vec3> pos;
    std::map<c::EntityId, c::Vec3> impulses, facing;
    bool sacredAll = false;
    c::EntityId sacredEntity = 0;
    c::Transform transformOf(c::EntityId e) const override {
        c::Transform t;
        const auto it = pos.find(e);
        if (it != pos.end()) t.position = it->second;
        return t;
    }
    c::FighterCapsule capsuleOf(c::EntityId) const override { return {}; }
    bool lineOfSight(c::EntityId, c::EntityId) const override { return true; }
    void applyImpulse(c::EntityId e, c::Vec3 i) override {
        impulses[e].x += i.x;
        impulses[e].z += i.z;
    }
    void setFacing(c::EntityId e, c::Vec3 d) override { facing[e] = d; }
    bool isSacredAt(c::Vec3 point) const override {
        const auto it = pos.find(sacredEntity);
        return sacredAll || (it != pos.end() && it->second.x == point.x && it->second.y == point.y && it->second.z == point.z);
    }
};

struct Env {
    CombatRules rules;
    MoveLibrary lib;
    Env() {
        std::string err;
        REQUIRE_MESSAGE(rules.load(repo("data/combat/combat_rules.json"), err), err);
        REQUIRE_MESSAGE(lib.loadDirectory(repo("data/combat/moves"), err), err);
    }
};

void pair(CombatSim& sim, WorldBridge& b) {
    b.addFighter(1);
    b.addFighter(2);
    sim.setTarget(0, 1);
    sim.setTarget(1, 0);
}
}  // namespace

TEST_CASE("bridge: позиции бойцов берутся из мира, доворот лицом к цели уходит в мир") {
    Env env;
    FakeWorld w;
    w.pos[1] = {0, 0, 0};
    w.pos[2] = {0, 0, -1.2f};
    CombatSim sim(env.rules);
    WorldBridge b(sim, w);
    pair(sim, b);
    w.pos[2] = {0.5f, 0, -1.5f};
    b.step({});
    CHECK(sim.fighter(1).pos.x == doctest::Approx(0.5f));
    CHECK(sim.fighter(1).pos.z == doctest::Approx(-1.5f));
    CHECK(w.facing[1].z < 0.f);
    CHECK(w.facing[2].z > 0.f);
}

TEST_CASE("bridge: попадание даёт отброс защитнику в направлении от атакующего") {
    Env env;
    FakeWorld w;
    w.pos[1] = {0, 0, 0};
    w.pos[2] = {0, 0, -1.0f};
    CombatSim sim(env.rules);
    WorldBridge b(sim, w);
    pair(sim, b);
    FighterInput hit;
    hit.attack = env.lib.byId("boxing_jab");
    REQUIRE(hit.attack);
    for (int i = 0; i < 40 && sim.log().empty(); ++i) b.step({i == 0 ? hit : FighterInput{}, {}});
    REQUIRE_FALSE(sim.log().empty());
    CHECK(w.impulses[2].z < 0.f);
    CHECK(w.impulses.count(1) == 0);
}

TEST_CASE("bridge: в святом месте атаки отменяются, драки нет") {
    Env env;
    FakeWorld w;
    w.sacredAll = true;
    w.pos[1] = {0, 0, 0};
    w.pos[2] = {0, 0, -1.0f};
    CombatSim sim(env.rules);
    WorldBridge b(sim, w);
    pair(sim, b);
    FighterInput hit;
    hit.attack = env.lib.byId("boxing_jab");
    for (int i = 0; i < 60; ++i) b.step({hit, hit});
    CHECK(b.mutedAttacks() > 0);
    CHECK(sim.log().empty());
    CHECK(sim.fighter(0).health == doctest::Approx(100.f));
    CHECK(sim.fighter(1).health == doctest::Approx(100.f));
}

TEST_CASE("bridge: заглушка контракта подходит как мир (сборка без движка)") {
    Env env;
    c::stubs::CombatWorldStub w;
    CombatSim sim(env.rules);
    WorldBridge b(sim, w);
    CHECK(b.addFighter(1) == 0);
    sim.setTarget(0, 0);
    b.step({});
    CHECK(sim.frame() == 1);
}

TEST_CASE("bridge: вход нападающего или защитника в святую зону отменяет начатый удар без нового ввода") {
    Env env;
    for (const c::EntityId protectedEntity : {1u, 2u}) {
        FakeWorld w;
        w.pos[1] = {0, 0, 0};
        w.pos[2] = {0, 0, -1.f};
        CombatSim sim(env.rules);
        WorldBridge bridge(sim, w);
        pair(sim, bridge);
        FighterInput hit;
        hit.attack = env.lib.byId("boxing_jab");
        REQUIRE(hit.attack);
        bridge.step({hit, {}});
        REQUIRE(sim.fighter(0).state == FighterState::Attacking);
        w.sacredEntity = protectedEntity;
        bridge.step({});
        CHECK(bridge.mutedAttacks() == 1);
        CHECK(sim.fighter(0).move == nullptr);
        CHECK(sim.fighter(0).buffered == nullptr);
        CHECK(w.impulses.empty());
        w.sacredEntity = 0;  // выход из зоны не возобновляет отменённую атаку
        for (int frame = 0; frame < 40; ++frame) bridge.step({});
        CHECK(sim.fighter(1).health == doctest::Approx(100.f));
        CHECK(sim.log().empty());
        // После выхода новый явный ввод снова работает.
        bridge.step({hit, {}});
        for (int frame = 0; frame < 40; ++frame) bridge.step({});
        CHECK(sim.fighter(1).health < 100.f);
    }
}

TEST_CASE("bridge: святая зона очищает буфер оглушённого бойца, сохраняя его состояние и возможность уклоняться") {
    Env env;
    FakeWorld w;
    w.pos[1] = {0, 0, 0};
    w.pos[2] = {0, 0, -1.f};
    CombatSim sim(env.rules);
    WorldBridge bridge(sim, w);
    pair(sim, bridge);
    auto& fighter = sim.fighterMutable(0);
    fighter.state = FighterState::HitStun;
    fighter.stunFrames = 4;
    FighterInput hit;
    hit.attack = env.lib.byId("boxing_jab");
    bridge.step({hit, {}});
    REQUIRE(fighter.buffered == hit.attack);
    w.sacredAll = true;
    bridge.step({});
    CHECK(fighter.state == FighterState::HitStun);
    CHECK(fighter.buffered == nullptr);
    for (int i = 0; i < 10; ++i) bridge.step({});
    FighterInput defense;
    defense.defense = DefenseAction::SlipLeft;
    bridge.step({defense, {}});
    CHECK(fighter.state == FighterState::Defending);
    w.sacredAll = false;
    for (int i = 0; i < 40; ++i) bridge.step({});
    CHECK(sim.log().empty());
}
