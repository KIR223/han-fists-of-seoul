// Тесты простой схемы управления боем (часть 17 дизайна): ритм серии, ветки по направлению, сильный удар, блок, парирование, уклон, спецприём.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "han/combat/input_scheme.h"

using namespace han::combat;

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }

const CombatRules& rules() {
    static CombatRules r = [] {
        CombatRules x;
        std::string error;
        REQUIRE_MESSAGE(x.load(repo("data/combat/combat_rules.json"), error), error);
        return x;
    }();
    return r;
}
const MoveLibrary& library() {
    static MoveLibrary l = [] {
        MoveLibrary x;
        std::string error;
        REQUIRE_MESSAGE(x.loadDirectory(repo("data/combat/moves"), error), error);
        return x;
    }();
    return l;
}
const MoveData& mv(const char* id) { return *library().byId(std::string("boxing_") + id); }

struct Rig {
    CombatSim sim{rules()};
    SimpleScheme scheme{library()};
    Rig() {
        FighterStats s;
        s.technique = 0;
        sim.addFighter({0, 0}, s);
        sim.addFighter({0, -3.f}, s);
        sim.setTarget(0, 1);
        sim.setTarget(1, 0);
    }
    FighterInput press(RawInput raw) {
        const FighterInput in = scheme.map(raw, sim, 0);
        sim.step({in, {}});
        return in;
    }
    void idle(int n) {
        for (int i = 0; i < n; ++i) press({});
    }
    const MoveData* tap(RawInput raw = {}) {
        raw.lmbPressed = true;
        press(raw);
        const MoveData* m = scheme.lastAttack();
        // пауза между нажатиями в ритме серии, но короче порога сброса
        for (int i = 0; i < 22; ++i) press({});
        return m;
    }
};
}  // namespace

TEST_CASE("scheme: ритм нажатий ЛКМ строит серию джеб, кросс, хук, апперкот и замыкается по кругу") {
    Rig r;
    CHECK(r.tap() == &mv("jab"));
    CHECK(r.tap() == &mv("cross"));
    CHECK(r.tap() == &mv("left_hook"));
    CHECK(r.tap() == &mv("uppercut"));
    CHECK(r.tap() == &mv("jab"));
}

TEST_CASE("scheme: длинная пауза начинает серию заново") {
    Rig r;
    CHECK(r.tap() == &mv("jab"));
    CHECK(r.tap() == &mv("cross"));
    r.idle(80);
    CHECK(r.tap() == &mv("jab"));
    CHECK(r.scheme.chainIndex() == 1);
}

TEST_CASE("scheme: направление выбирает ветку — в сторону хуки, назад удар с отходом") {
    {
        Rig r;
        RawInput left;
        left.right = -1.f;
        CHECK(r.tap(left) == &mv("left_hook"));
    }
    {
        Rig r;
        RawInput right;
        right.right = 1.f;
        CHECK(r.tap(right) == &mv("right_hook"));
    }
    {
        Rig r;
        RawInput back;
        back.forward = -1.f;
        CHECK(r.tap(back) == &mv("jab_retreat"));
    }
    {
        Rig r;
        RawInput body;
        body.rmbHeld = true;
        CHECK(r.tap(body) == &mv("jab_body"));
        RawInput bodyLeft;
        bodyLeft.rmbHeld = true;
        bodyLeft.right = -1.f;
        Rig r2;
        CHECK(r2.tap(bodyLeft) == &mv("liver_hook"));
    }
}

TEST_CASE("scheme: направление не сбивает ритм серии при боковых ударах") {
    Rig r;
    RawInput left;
    left.right = -1.f;
    r.tap(left);
    CHECK(r.scheme.chainIndex() == 0);  // боковой удар — отдельная ветка, не часть серии
    CHECK(r.tap() == &mv("jab"));
}

TEST_CASE("scheme: удержание ЛКМ — сильный удар один раз за удержание") {
    Rig r;
    int heavy = 0;
    for (int i = 0; i < 60; ++i) {
        RawInput raw;
        raw.lmbHeld = true;
        raw.lmbPressed = i == 0;
        const FighterInput in = r.scheme.map(raw, r.sim, 0);
        if (in.attack == &mv("overhand")) ++heavy;
        r.sim.step({in, {}});
    }
    CHECK(heavy == 1);
    RawInput release;
    r.press(release);
    RawInput again;
    again.lmbHeld = true;
    again.lmbPressed = true;
    r.press(again);
    int second = 0;
    for (int i = 0; i < 40; ++i) {
        RawInput raw;
        raw.lmbHeld = true;
        const FighterInput in = r.scheme.map(raw, r.sim, 0);
        second += in.attack == &mv("overhand") ? 1 : 0;
        r.sim.step({in, {}});
    }
    CHECK(second == 1);  // новое удержание — новый сильный удар
}

TEST_CASE("scheme: ПКМ — блок, нажатие — парирование; движение и рывок передаются как есть") {
    Rig r;
    RawInput hold;
    hold.rmbHeld = true;
    CHECK(r.scheme.map(hold, r.sim, 0).blockHeld);
    RawInput tap;
    tap.rmbPressed = true;
    const FighterInput in = r.scheme.map(tap, r.sim, 0);
    CHECK(in.defense == DefenseAction::Parry);
    CHECK(in.blockHeld);
    RawInput walk;
    walk.forward = 0.7f;
    walk.right = -0.4f;
    walk.dash = true;
    const FighterInput w = r.scheme.map(walk, r.sim, 0);
    CHECK(w.forward == doctest::Approx(0.7f));
    CHECK(w.right == doctest::Approx(-0.4f));
    CHECK(w.dash);
    CHECK_FALSE(w.blockHeld);
}

TEST_CASE("scheme: удар отменяет удержание блока в том же кадре") {
    Rig r;
    RawInput raw;
    raw.rmbHeld = true;
    raw.lmbPressed = true;
    raw.right = 0.f;
    const FighterInput in = r.scheme.map(raw, r.sim, 0);
    CHECK(in.attack != nullptr);
    CHECK_FALSE(in.blockHeld);
}

TEST_CASE("scheme: Space без угрозы — отскок назад или шаг в сторону по направлению") {
    Rig r;
    RawInput space;
    space.spacePressed = true;
    CHECK(r.scheme.map(space, r.sim, 0).defense == DefenseAction::StepBack);
    space.right = -1.f;
    CHECK(r.scheme.map(space, r.sim, 0).defense == DefenseAction::SidestepLeft);
    space.right = 1.f;
    CHECK(r.scheme.map(space, r.sim, 0).defense == DefenseAction::SidestepRight);
    space.right = 0.f;
    space.forward = -1.f;
    CHECK(r.scheme.map(space, r.sim, 0).defense == DefenseAction::StepBack);
}

TEST_CASE("scheme: Space выбирает уклон по летящему удару — слип от прямого, нырок от хука, отклон от апперкота, сайдстеп от сверху") {
    const auto defenseAgainst = [](const char* move, float right) {
        Rig r;
        FighterInput hit;
        hit.attack = &mv(move);
        r.sim.fighterMutable(1).stamina = 100.f;
        r.sim.step({{}, hit});
        r.sim.step({});
        RawInput space;
        space.spacePressed = true;
        space.right = right;
        return r.scheme.map(space, r.sim, 0).defense;
    };
    CHECK(defenseAgainst("cross", 0.f) == DefenseAction::SlipLeft);
    CHECK(defenseAgainst("cross", 1.f) == DefenseAction::SlipRight);
    CHECK(defenseAgainst("left_hook", 0.f) == DefenseAction::Duck);
    CHECK(defenseAgainst("uppercut", 0.f) == DefenseAction::LeanBack);
    CHECK(defenseAgainst("overhand", 0.f) == DefenseAction::SidestepLeft);
    CHECK(defenseAgainst("overhand", 1.f) == DefenseAction::SidestepRight);
}

TEST_CASE("scheme: R — спецприём, только если хватает фокуса") {
    Rig r;
    RawInput special;
    special.specialPressed = true;
    r.sim.fighterMutable(0).focus = 10.f;
    CHECK(r.scheme.map(special, r.sim, 0).attack == nullptr);
    r.sim.fighterMutable(0).focus = 100.f;
    CHECK(r.scheme.map(special, r.sim, 0).attack == &mv("dempsey_roll"));
}

TEST_CASE("scheme: игрок на схеме побеждает неподвижного бойца серией ударов (интеграция со всем боем)") {
    Rig r;
    r.sim.fighterMutable(1).health = 40.f;
    RawInput walk;
    walk.forward = 1.f;
    for (int i = 0; i < 120 && r.sim.distance(0, 1) > 1.5f; ++i) r.press(walk);
    for (int round = 0; round < 40 && !r.sim.fightOver(); ++round) {
        r.sim.fighterMutable(0).stamina = 100.f;
        r.tap();
    }
    CHECK(r.sim.fighter(1).health < 40.f);
    CHECK(r.sim.fightOver());
}
