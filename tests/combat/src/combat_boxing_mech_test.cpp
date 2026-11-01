// Тесты боксёрских механик (часть 19): «движение головой» (заряд за успешный уклон усиливает следующий удар) и «работа по корпусу».
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "han/combat/profile.h"
#include "han/combat/scenario.h"
#include "han/combat/sim.h"

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
const MoveData* mv(const char* id) { return library().byId(std::string("boxing_") + id); }

struct Pair {
    CombatSim sim;
    explicit Pair(bool headMovement, std::uint32_t seed = 1) : sim(rules(), seed) {
        FighterStats s;
        s.technique = 0;  // без критов: урон сравнивается точно
        FighterStats boxer = s;
        boxer.headMovement = headMovement;
        sim.addFighter({0, 0}, boxer);
        sim.addFighter({0, -1.1f}, s);
        sim.setTarget(0, 1);
        sim.setTarget(1, 0);
    }
    void idle(int n) {
        for (int i = 0; i < n; ++i) sim.step({});
    }
    // Враг (1) бьёт приёмом, боец 0 уходит слипом через delay кадров; true, если защита сработала.
    bool slipAttempt(const char* attack, int delay) {
        FighterInput hit;
        hit.attack = mv(attack);
        const int before = static_cast<int>(sim.log().size());
        sim.step({{}, hit});
        for (int i = 0; i < 40; ++i) {
            FighterInput d;
            if (i == delay) d.defense = DefenseAction::SlipLeft;
            sim.step({d, {}});
        }
        for (std::size_t k = static_cast<std::size_t>(before); k < sim.log().size(); ++k)
            if (sim.log()[k].type == CombatEventType::Evaded && sim.log()[k].defender == 0) return true;
        return false;
    }
};
}  // namespace

TEST_CASE("head movement: успешный слип даёт заряд, следующий удар сильнее, заряд расходуется") {
    Pair p(true);
    bool slipped = false;
    for (int delay = 0; delay < 14 && !slipped; ++delay) {
        p.idle(120);
        slipped = p.slipAttempt("cross", delay);
    }
    REQUIRE(slipped);
    CHECK(p.sim.fighter(0).headCharges >= 1);

    const auto jabDamage = [&](Pair& pair) {
        pair.idle(60);
        FighterInput jab;
        jab.attack = mv("jab");
        const std::size_t before = pair.sim.log().size();
        pair.sim.step({jab, {}});
        for (int i = 0; i < 30; ++i) pair.sim.step({});
        for (std::size_t k = before; k < pair.sim.log().size(); ++k)
            if (pair.sim.log()[k].type == CombatEventType::Hit && pair.sim.log()[k].attacker == 0) return pair.sim.log()[k];
        return CombatEvent{};
    };
    Pair plain(true);
    plain.idle(200);
    const CombatEvent base = jabDamage(plain);
    const CombatEvent boosted = jabDamage(p);
    REQUIRE(base.damage > 0.f);
    REQUIRE(boosted.damage > 0.f);
    CHECK(boosted.headCharges >= 1);
    CHECK(boosted.damage > base.damage * 1.05f);
    CHECK(p.sim.fighter(0).headCharges == 0);
}

TEST_CASE("head movement: у бойца без боксёрской механики заряд не копится; заряды затухают и сбрасываются пропущенным ударом") {
    Pair plain(false);
    bool slipped = false;
    for (int delay = 0; delay < 14 && !slipped; ++delay) {
        plain.idle(120);
        slipped = plain.slipAttempt("cross", delay);
    }
    REQUIRE(slipped);
    CHECK(plain.sim.fighter(0).headCharges == 0);

    Pair p(true);
    p.sim.fighterMutable(0).headCharges = 3;
    p.sim.fighterMutable(0).headChargeFrame = p.sim.frame();
    p.idle(rules().headMoveDecayFrames + 5);
    CHECK(p.sim.fighter(0).headCharges == 0);

    Pair q(true);
    q.sim.fighterMutable(0).headCharges = 3;
    q.sim.fighterMutable(0).headChargeFrame = q.sim.frame();
    FighterInput hit;
    hit.attack = mv("jab");
    q.sim.step({{}, hit});
    for (int i = 0; i < 20; ++i) q.sim.step({});
    CHECK(q.sim.fighter(0).headCharges == 0);  // пропустил удар в голову
}

TEST_CASE("head movement: заряды ограничены потолком из правил") {
    Pair p(true);
    p.sim.fighterMutable(0).headCharges = rules().headMoveMaxCharges;
    CHECK(rules().headMoveBonus * static_cast<float>(rules().headMoveMaxCharges) <= 0.5001f);  // не больше ×1,5
}

TEST_CASE("body work: серия ударов в корпус сгибает защитника, он оглушён на окно для апперкота") {
    Pair p(false);
    FighterInput hook;
    hook.attack = mv("liver_hook");
    int hooks = 0;
    bool doubled = false;
    for (int round = 0; round < 12 && !doubled; ++round) {
        p.idle(80);
        p.sim.fighterMutable(0).stamina = 100.f;
        p.sim.step({hook, {}});
        ++hooks;
        for (int i = 0; i < 40; ++i) {
            p.sim.step({});
            for (const CombatEvent& e : p.sim.events())
                if (e.doubled) doubled = true;
            if (doubled) break;
        }
    }
    REQUIRE(doubled);
    CHECK(hooks >= 2);  // одним ударом по корпусу не сгибается
    CHECK(p.sim.fighter(1).state == FighterState::HitStun);
    CHECK(p.sim.fighter(1).stunFrames >= rules().bodyDoubledFrames);
    CHECK(p.sim.fighter(1).bodyWork == doctest::Approx(0.f));  // счётчик обнулён
}

TEST_CASE("body work: накопленный урон по корпусу со временем рассасывается; удары в голову его не копят") {
    Pair p(false);
    p.sim.fighterMutable(1).bodyWork = 20.f;
    p.idle(60 * 20);
    CHECK(p.sim.fighter(1).bodyWork == doctest::Approx(0.f));

    Pair q(false);
    FighterInput jab;
    jab.attack = mv("jab");
    for (int round = 0; round < 3; ++round) {
        q.idle(80);
        q.sim.step({jab, {}});
        for (int i = 0; i < 30; ++i) q.sim.step({});
    }
    CHECK(q.sim.fighter(1).bodyWork == doctest::Approx(0.f));
}

TEST_CASE("boxer-профиль включает движение головой, остальные архетипы — нет") {
    ProfileLibrary profiles;
    std::string error;
    REQUIRE_MESSAGE(profiles.loadDirectory(repo("data/combat/profiles"), error), error);
    const FighterProfile* boxer = profiles.byId("boxer_veteran");
    const FighterProfile* brawler = profiles.byId("street_brawler");
    REQUIRE(boxer);
    REQUIRE(brawler);
    CHECK(boxer->stats().headMovement);
    CHECK_FALSE(brawler->stats().headMovement);
}

TEST_CASE("shoulder roll: уход плечом от прямого — уклон с углом (окно контратаки), от хука той же стороны — частичный урон") {
    const auto outcomeOf = [](const char* attack, DefenseAction action) {
        for (int delay = 0; delay < 16; ++delay) {
            Pair p(false);
            p.idle(60);
            p.sim.fighterMutable(0).stamina = 100.f;
            FighterInput hit;
            hit.attack = mv(attack);
            p.sim.step({{}, hit});
            for (int i = 0; i < 40; ++i) {
                FighterInput d;
                if (i == delay) d.defense = action;
                p.sim.step({d, {}});
            }
            for (const CombatEvent& e : p.sim.log())
                if (e.defender == 0 && e.defense == "shoulder_roll") return e;
        }
        return CombatEvent{};
    };
    const CombatEvent straight = outcomeOf("cross", DefenseAction::ShoulderRollLeft);
    REQUIRE(straight.move != nullptr);
    CHECK(straight.type == CombatEventType::Evaded);
    CHECK(straight.angle);
    CHECK(defenseActionName(DefenseAction::ShoulderRollRight) == std::string("shoulder_roll"));
}

TEST_CASE("shoulder roll: понимается сценариями и схемой управления (Q + направление)") {
    DefenseAction a;
    REQUIRE(defenseFromName("shoulder_roll_left", a));
    CHECK(a == DefenseAction::ShoulderRollLeft);
    CHECK(defenseFromName("shoulder_roll_right", a));
    CHECK_FALSE(defenseFromName("shoulder_roll", a));
}

TEST_CASE("ритм: пять чистых попаданий подряд открывают «Поток» — урон выше; пропущенный удар обрывает его") {
    Pair p(false);
    FighterInput jab;
    jab.attack = mv("jab");
    float first = 0.f, last = 0.f;
    bool started = false;
    int hits = 0;
    for (int round = 0; round < 14 && !started; ++round) {
        p.idle(70);
        p.sim.fighterMutable(0).stamina = 100.f;
        p.sim.fighterMutable(1).health = 100.f;  // противник не падает на нокдаун: серия идёт непрерывно
        p.sim.fighterMutable(1).balance = 100.f;
        p.sim.step({jab, {}});
        for (int i = 0; i < 30; ++i) {
            p.sim.step({});
            for (const CombatEvent& e : p.sim.events())
                if (e.type == CombatEventType::Hit && e.attacker == 0) {
                    if (++hits == 1) first = e.damage;
                    last = e.damage;
                    if (e.flowStarted) started = true;
                }
        }
    }
    REQUIRE(started);
    CHECK(hits == rules().rhythmHitsForFlow);
    CHECK(p.sim.fighter(0).flowUntil > p.sim.frame());
    // Шестой удар уже в «Потоке»: урон ×flow
    p.idle(70);
    p.sim.fighterMutable(0).stamina = 100.f;
    p.sim.step({jab, {}});
    float flowDamage = 0.f;
    for (int i = 0; i < 30; ++i) {
        p.sim.step({});
        for (const CombatEvent& e : p.sim.events())
            if (e.type == CombatEventType::Hit && e.attacker == 0 && e.flow) flowDamage = e.damage;
    }
    REQUIRE(flowDamage > 0.f);
    CHECK(flowDamage == doctest::Approx(first * rules().flow).epsilon(0.05));
    (void)last;
    // Противник отвечает попаданием — «Поток» обрывается
    p.idle(70);
    FighterInput counter;
    counter.attack = mv("jab");
    p.sim.fighterMutable(0).state = FighterState::Idle;
    p.sim.step({{}, counter});
    for (int i = 0; i < 30; ++i) p.sim.step({});
    CHECK(p.sim.fighter(0).flowUntil == 0);
    CHECK(p.sim.fighter(0).rhythm == 0);
}

TEST_CASE("ритм: без попаданий счёт затухает") {
    Pair p(false);
    p.sim.fighterMutable(0).rhythm = 3;
    p.sim.fighterMutable(0).rhythmFrame = p.sim.frame();
    p.idle(rules().rhythmDecayFrames + 5);
    CHECK(p.sim.fighter(0).rhythm == 0);
}
