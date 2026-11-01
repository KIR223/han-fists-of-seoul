// Тесты пути приёма (G2): подсмотрел → запомнил → разучил → звёзды; дневной лимит тренировки; сохранение.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "han/learning/mastery.h"

using namespace han::learning;

namespace {
LearningConfig config() {
    std::ifstream in((std::filesystem::path(HAN_REPO_DIR) / "data/combat/learning.json").string(), std::ios::binary);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    han::core::Json j;
    std::string error;
    REQUIRE_MESSAGE(han::core::parseJson(ss.str(), j, error), error);
    LearningConfig c;
    REQUIRE_MESSAGE(parseLearningConfig(j, c, error), error);
    return c;
}
}  // namespace

TEST_CASE("learning: три подсмотра запоминают приём, до этого приём не доступен и не тренируется") {
    MoveMastery m(config());
    CHECK(m.stage("boxing_cross") == MoveStage::Unknown);
    CHECK_FALSE(m.observe("boxing_cross"));
    CHECK(m.stage("boxing_cross") == MoveStage::Seen);
    CHECK(m.train("boxing_cross", 4) == doctest::Approx(0.f));  // нельзя тренировать невыученное
    CHECK_FALSE(m.observe("boxing_cross"));
    CHECK(m.observe("boxing_cross"));
    CHECK(m.stage("boxing_cross") == MoveStage::Remembered);
    CHECK_FALSE(m.canUse("boxing_cross"));
    CHECK_FALSE(m.observe("boxing_cross"));  // повторно «запомнил» не выдаётся
}

TEST_CASE("learning: наблюдательность даёт задел опыта при запоминании") {
    MoveMastery a(config()), b(config());
    for (int i = 0; i < 3; ++i) {
        a.observe("m", 0.f);
        b.observe("m", 10.f);
    }
    CHECK(a.xp("m") == doctest::Approx(0.f));
    CHECK(b.xp("m") == doctest::Approx(20.f));
}

TEST_CASE("learning: тренировка разучивает, звёзды идут по порогам, три звезды — мастер") {
    MoveMastery m(config());
    m.grant("boxing_jab");
    CHECK(m.stage("boxing_jab") == MoveStage::Learned);
    CHECK(m.canUse("boxing_jab"));
    CHECK(m.stars("boxing_jab") == 0);
    CHECK(m.starBonus("boxing_jab") == doctest::Approx(1.f));
    float total = m.xp("boxing_jab");
    for (int day = 0; day < 8; ++day) {
        m.newDay();
        for (int k = 0; k < 6; ++k) total += m.train("boxing_jab", 4);
    }
    CHECK(m.xp("boxing_jab") == doctest::Approx(total));
    CHECK(m.stars("boxing_jab") == 3);
    CHECK(m.stage("boxing_jab") == MoveStage::Mastered);
    CHECK(m.starBonus("boxing_jab") == doctest::Approx(1.12f));
}

TEST_CASE("learning: дневной лимит тренировок не даёт «нафармить» опыт за один день") {
    MoveMastery m(config());
    m.grant("boxing_jab");
    const float before = m.xp("boxing_jab");
    float gained = 0.f;
    for (int i = 0; i < 20; ++i) gained += m.train("boxing_jab", 4);
    CHECK(gained == doctest::Approx(m.config().dailyTrainingCap));
    CHECK(m.xp("boxing_jab") == doctest::Approx(before + gained));
    m.newDay();
    CHECK(m.train("boxing_jab", 4) > 0.f);
}

TEST_CASE("learning: бой даёт опыт только выученным приёмам, попадание ценнее") {
    MoveMastery m(config());
    m.observe("m");
    m.fightUse("m", true);
    CHECK(m.xp("m") == doctest::Approx(0.f));
    m.grant("m");
    const float x = m.xp("m");
    m.fightUse("m", false);
    m.fightUse("m", true);
    CHECK(m.xp("m") == doctest::Approx(x + 1.f + 2.5f));
}

TEST_CASE("learning: сохранение и загрузка возвращают то же состояние; мусор отклоняется") {
    MoveMastery a(config());
    a.grant("boxing_jab");
    a.train("boxing_jab", 3);
    a.observe("boxing_cross");
    const std::string text = a.toJson().dump();
    han::core::Json j;
    std::string error;
    REQUIRE(han::core::parseJson(text, j, error));
    MoveMastery b(config());
    REQUIRE_MESSAGE(b.fromJson(j, error), error);
    CHECK(b.xp("boxing_jab") == doctest::Approx(a.xp("boxing_jab")));
    CHECK(b.stage("boxing_cross") == MoveStage::Seen);
    CHECK(b.train("boxing_jab", 4) == a.train("boxing_jab", 4));  // дневной счётчик тоже сохранён
    REQUIRE(han::core::parseJson(R"({"moves":{"x":{"xp":-5}}})", j, error));
    CHECK_FALSE(b.fromJson(j, error));
    CHECK(b.stage("boxing_jab") != MoveStage::Unknown);  // прежний прогресс не затёрт
}

TEST_CASE("learning: пороги конфига проверяются") {
    LearningConfig c;
    std::string error;
    han::core::Json j;
    REQUIRE(han::core::parseJson(R"({"learn_xp":40,"star_xp":[30,100,200]})", j, error));
    CHECK_FALSE(parseLearningConfig(j, c, error));
    REQUIRE(han::core::parseJson(R"({"star_xp":[100,50,200]})", j, error));
    CHECK_FALSE(parseLearningConfig(j, c, error));
}

TEST_CASE("learning: события боя — приёмы игрока дают опыт, приёмы врага подсматриваются, чужие бои не считаются") {
    using namespace han::combat;
    CombatRules rules;
    MoveLibrary moves;
    std::string error;
    REQUIRE_MESSAGE(rules.load((std::filesystem::path(HAN_REPO_DIR) / "data/combat/combat_rules.json").string(), error), error);
    REQUIRE_MESSAGE(moves.loadDirectory((std::filesystem::path(HAN_REPO_DIR) / "data/combat/moves").string(), error), error);
    const MoveData* jab = moves.byId("boxing_jab");
    const MoveData* cross = moves.byId("boxing_cross");
    REQUIRE(jab);
    REQUIRE(cross);

    MoveMastery m(config());
    m.grant("boxing_jab");
    std::vector<CombatEvent> events;
    CombatEvent hit;
    hit.type = CombatEventType::Hit;
    hit.attacker = 0;
    hit.defender = 1;
    hit.move = jab;
    events.push_back(hit);
    CombatEvent whiff = hit;
    whiff.type = CombatEventType::Whiff;
    events.push_back(whiff);
    CombatEvent foe;
    foe.type = CombatEventType::Hit;
    foe.attacker = 1;
    foe.defender = 0;
    foe.move = cross;
    events.push_back(foe);
    CombatEvent other = foe;  // драка двух других бойцов — игрок её не видел
    other.attacker = 2;
    other.defender = 1;
    events.push_back(other);
    CombatEvent ko;
    ko.type = CombatEventType::KnockedOut;
    ko.attacker = 0;
    ko.defender = 1;
    ko.move = jab;
    events.push_back(ko);

    applyCombatEvents(m, events, 0);
    CHECK(m.xp("boxing_jab") == doctest::Approx(m.config().learnXp + 2.5f + 1.f));
    CHECK(m.stage("boxing_cross") == MoveStage::Seen);
    applyCombatEvents(m, events, 0);
    applyCombatEvents(m, events, 0);
    CHECK(m.stage("boxing_cross") == MoveStage::Remembered);
}

TEST_CASE("moves: нырок и хук — контрприём с шагом вперёд, данные читаются") {
    using namespace han::combat;
    MoveLibrary moves;
    std::string error;
    REQUIRE_MESSAGE(moves.loadDirectory((std::filesystem::path(HAN_REPO_DIR) / "data/combat/moves").string(), error), error);
    const MoveData* m = moves.byId("boxing_duck_hook");
    REQUIRE(m);
    CHECK(m->damage > moves.byId("boxing_left_hook")->damage);
    CHECK(m->startup < moves.byId("boxing_left_hook")->startup);
}
