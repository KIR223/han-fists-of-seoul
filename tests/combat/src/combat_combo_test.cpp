// Тесты фирменных комбо: данные, распознавание серии, обрыв паузой и пропущенным ударом.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "han/combat/combos.h"

using namespace han::combat;

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }

const MoveLibrary& moves() {
    static MoveLibrary l = [] {
        MoveLibrary x;
        std::string error;
        REQUIRE_MESSAGE(x.loadDirectory(repo("data/combat/moves"), error), error);
        return x;
    }();
    return l;
}
const ComboLibrary& combos() {
    static ComboLibrary l = [] {
        ComboLibrary x;
        std::string error;
        REQUIRE_MESSAGE(x.loadDirectory(repo("data/combat/combos"), error), error);
        return x;
    }();
    return l;
}
CombatEvent land(int attacker, const char* move, int frame, CombatEventType type = CombatEventType::Hit) {
    CombatEvent e;
    e.type = type;
    e.attacker = attacker;
    e.defender = 1 - attacker;
    e.move = moves().byId(std::string("boxing_") + move);
    REQUIRE(e.move);
    e.frame = frame;
    return e;
}
}  // namespace

TEST_CASE("combo: данные читаются, все приёмы комбо существуют") {
    CHECK(combos().all().size() == 4);
    for (const ComboDef& c : combos().all())
        for (const std::string& m : c.moves) CHECK_MESSAGE(moves().byId(m) != nullptr, c.id << ": нет приёма " << m);
}

TEST_CASE("combo: раз-два-три распознаётся на третьем ударе, награда из данных") {
    ComboTracker t(combos());
    CHECK(t.update({land(0, "jab", 10)}).empty());
    CHECK(t.update({land(0, "cross", 40)}).empty());
    CHECK(t.streak(0) == 2);
    const auto done = t.update({land(0, "left_hook", 70)});
    REQUIRE(done.size() == 1);
    CHECK(done[0].combo->id == "boxing_one_two_three");
    CHECK(done[0].fighter == 0);
    CHECK(done[0].combo->streamPoints == 20);
}

TEST_CASE("combo: блокированный удар тоже в серии; длинная пауза обрывает; чужое попадание обрывает") {
    ComboTracker t(combos());
    t.update({land(0, "jab", 10)});
    t.update({land(0, "cross", 40, CombatEventType::Blocked)});
    CHECK(t.update({land(0, "left_hook", 70)}).size() == 1);

    ComboTracker pause(combos());
    pause.update({land(0, "jab", 10)});
    pause.update({land(0, "cross", 40)});
    CHECK(pause.update({land(0, "left_hook", 300)}).empty());  // пауза дольше окна
    CHECK(pause.streak(0) == 1);

    ComboTracker interrupted(combos());
    interrupted.update({land(0, "jab", 10)});
    interrupted.update({land(0, "cross", 40)});
    interrupted.update({land(1, "jab", 55)});  // боец 1 попал по бойцу 0
    CHECK(interrupted.streak(0) == 0);
    CHECK(interrupted.update({land(0, "left_hook", 70)}).empty());
}

TEST_CASE("combo: промах и уклон серию не строят; у каждого бойца своя серия") {
    ComboTracker t(combos());
    t.update({land(0, "jab", 10)});
    t.update({land(0, "cross", 40, CombatEventType::Evaded)});
    t.update({land(1, "jab", 45, CombatEventType::Whiff)});
    CHECK(t.streak(0) == 1);
    CHECK(t.streak(1) == 0);
    t.update({land(1, "liver_hook", 100)});
    // удар бойца 1 попал по бойцу 0 и оборвал его серию
    CHECK(t.streak(0) == 0);
    const auto done = t.update({land(1, "left_hook", 130)});
    REQUIRE(done.size() == 1);
    CHECK(done[0].combo->id == "boxing_liver_head");
    CHECK(done[0].fighter == 1);
}

TEST_CASE("combo: плохие данные отклоняются") {
    ComboLibrary lib;
    std::string error;
    han::core::Json j;
    REQUIRE(han::core::parseJson(R"({"combos":[{"id":"a","name_key":"k","moves":["x"]}]})", j, error));
    CHECK_FALSE(lib.parse(j, error));
    CHECK(error.find("минимум два") != std::string::npos);
    REQUIRE(han::core::parseJson(R"({"combos":[{"id":"a","name_key":"k","moves":["x","y"],"max_gap_frames":1}]})", j, error));
    CHECK_FALSE(lib.parse(j, error));
    REQUIRE(han::core::parseJson(R"({"combos":[{"id":"a","name_key":"k","moves":["x","y"]},{"id":"a","name_key":"k","moves":["x","y"]}]})", j, error));
    CHECK_FALSE(lib.parse(j, error));
    CHECK(error.find("повторяющийся") != std::string::npos);
}

TEST_CASE("combo: награда в ритм — комбо добавляет попадания и может открыть «Поток»; в потоке и у нокаутированного не копится") {
    CombatRules rules;
    std::string error;
    REQUIRE_MESSAGE(rules.load(repo("data/combat/combat_rules.json"), error), error);
    CombatSim sim(rules);
    sim.addFighter({0, 0});
    sim.addFighter({0, -1.1f});
    CHECK_FALSE(sim.awardRhythm(0, 2));
    CHECK(sim.fighter(0).rhythm == 2);
    CHECK_FALSE(sim.awardRhythm(0, 0));
    CHECK(sim.awardRhythm(0, rules.rhythmHitsForFlow));  // 2 + 5 >= порога
    CHECK(sim.fighter(0).flowUntil > sim.frame());
    CHECK(sim.fighter(0).rhythm == 0);
    CHECK_FALSE(sim.awardRhythm(0, 10));  // уже в «Потоке»
    CHECK(sim.fighter(0).rhythm == 0);
    CHECK_FALSE(sim.awardRhythm(5, 3));   // нет такого бойца
    sim.fighterMutable(1).state = FighterState::KnockedOut;
    CHECK_FALSE(sim.awardRhythm(1, 10));
}
