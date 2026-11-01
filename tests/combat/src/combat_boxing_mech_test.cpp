// Тесты боксёрских механик (часть 19): «движение головой» (заряд за успешный уклон усиливает следующий удар) и «работа по корпусу».
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "han/combat/profile.h"
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
