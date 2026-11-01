// Тесты сценариев боя (G1): консольный симулятор на реальных сценариях data/combat/scenarios и ошибки сценария.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "han/combat/scenario.h"

using namespace han::combat;

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }

struct World {
    CombatRules rules;
    MoveLibrary moves;
    World() {
        std::string error;
        REQUIRE_MESSAGE(rules.load(repo("data/combat/combat_rules.json"), error), error);
        REQUIRE_MESSAGE(moves.loadDirectory(repo("data/combat/moves"), error), error);
    }
    bool run(const std::string& text, ScenarioResult& out, std::string& error) const {
        han::core::Json j;
        REQUIRE_MESSAGE(han::core::parseJson(text, j, error), error);
        return runScenario(j, rules, moves, out, error);
    }
    bool runFile(const char* name, ScenarioResult& out, std::string& error) const {
        han::core::Json j;
        REQUIRE_MESSAGE(han::core::loadJsonFile(repo((std::string("data/combat/scenarios/") + name).c_str()), j, error), error);
        return runScenario(j, rules, moves, out, error);
    }
};
}  // namespace

TEST_CASE("scenario: джеб против слипа — уклонение на кадре замаха и выход на угол") {
    World w;
    ScenarioResult r;
    std::string error;
    REQUIRE_MESSAGE(w.runFile("jab_vs_slip.json", r, error), error);
    CHECK(r.evades == 1);
    CHECK(r.hits >= 1);
    REQUIRE_FALSE(r.log.empty());
    CHECK(r.log[0].find("кадр 6:") == 0);
    CHECK(r.log[0].find("уклонение") != std::string::npos);
    CHECK(r.log[0].find("выход на угол") != std::string::npos);
    CHECK(r.health[0] <= 100.f);
}

TEST_CASE("scenario: серия против блока — четыре блока, затем две попадания, защитник теряет здоровье") {
    World w;
    ScenarioResult r;
    std::string error;
    REQUIRE_MESSAGE(w.runFile("combo_vs_block.json", r, error), error);
    CHECK(r.blocks == 4);
    CHECK(r.hits == 2);
    CHECK(r.health[1] < 100.f);
    CHECK(r.health[0] == doctest::Approx(100.f));
    CHECK(r.stamina[0] < 100.f);
}

TEST_CASE("scenario: нокаут — сильный боец побеждает, события по порядку, бой останавливается") {
    World w;
    ScenarioResult r;
    std::string error;
    REQUIRE_MESSAGE(w.runFile("knockout.json", r, error), error);
    CHECK(r.knockouts == 1);
    CHECK(r.knockdowns >= 1);
    CHECK(r.winner == 0);
    CHECK(r.frames < 600);  // бой остановился сразу после нокаута
    CHECK(r.health[1] == 0.f);
    CHECK(r.log.back().find("НОКАУТ") != std::string::npos);
}

TEST_CASE("scenario: одинаковый сценарий даёт одинаковый лог") {
    World w;
    ScenarioResult a, b;
    std::string error;
    REQUIRE(w.runFile("knockout.json", a, error));
    REQUIRE(w.runFile("knockout.json", b, error));
    CHECK(a.log == b.log);
    CHECK(a.health == b.health);
}

TEST_CASE("scenario: ошибки сценария названы") {
    World w;
    ScenarioResult r;
    std::string error;
    const std::string two = R"("fighters":[{"x":0,"z":0},{"x":0,"z":-1.2}])";
    CHECK_FALSE(w.run(R"({"fighters":[{"x":0}]})", r, error));
    CHECK(error.find("fighters") != std::string::npos);
    CHECK_FALSE(w.run(R"({"frames":0,)" + two + "}", r, error));
    CHECK(error.find("frames") != std::string::npos);
    CHECK_FALSE(w.run("{" + two + R"(,"inputs":[{"frame":1,"fighter":0,"attack":"boxing_nope"}]})", r, error));
    CHECK(error.find("неизвестный приём") != std::string::npos);
    CHECK_FALSE(w.run("{" + two + R"(,"inputs":[{"frame":1,"fighter":0,"defense":"teleport"}]})", r, error));
    CHECK(error.find("неизвестная защита") != std::string::npos);
    CHECK_FALSE(w.run("{" + two + R"(,"inputs":[{"frame":1,"fighter":5,"attack":"boxing_jab"}]})", r, error));
    CHECK(error.find("номер бойца") != std::string::npos);
    CHECK_FALSE(w.run("{" + two + R"(,"frames":10,"inputs":[{"frame":99,"fighter":0}]})", r, error));
    CHECK(error.find("frame") != std::string::npos);
    CHECK_FALSE(w.run("[]", r, error));
    ScenarioResult ok;
    CHECK(w.run("{" + two + "}", ok, error));  // пустой бой — тоже сценарий
    CHECK(ok.hits == 0);
    CHECK(ok.frames == 120);
}

TEST_CASE("scenario: названия защит разбираются") {
    DefenseAction a = DefenseAction::None;
    for (const char* n : {"parry", "slip_left", "slip_right", "duck", "bob_left", "bob_right", "lean_back", "sidestep_left", "sidestep_right", "step_back"}) {
        CHECK_MESSAGE(defenseFromName(n, a), n);
        CHECK_MESSAGE(a != DefenseAction::None, n);
    }
    CHECK_FALSE(defenseFromName("block", a));
}

TEST_CASE("scenario: блок держится заданное число кадров, цель можно задать явно (бой втроём)") {
    World w;
    ScenarioResult r;
    std::string error;
    REQUIRE_MESSAGE(w.run(R"({"frames":60,"fighters":[{"x":0,"z":0},{"x":0,"z":-1.2},{"x":1.2,"z":0,"target":0}],
        "inputs":[{"frame":1,"fighter":2,"attack":"boxing_jab"}]})", r, error), error);
    CHECK(r.hits == 1);
    CHECK(r.health[0] < 100.f);   // третий бьёт первого
    CHECK(r.health[1] == doctest::Approx(100.f));
}
