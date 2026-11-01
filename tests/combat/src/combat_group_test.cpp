// Тесты групповой драки (G1): выбор цели, команды, удержание цели, бой один против нескольких с жетонами атаки.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "han/combat/ai.h"
#include "han/combat/targeting.h"

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
const ProfileLibrary& profiles() {
    static ProfileLibrary l = [] {
        ProfileLibrary x;
        std::string error;
        REQUIRE_MESSAGE(x.loadDirectory(repo("data/combat/profiles"), error), error);
        return x;
    }();
    return l;
}
std::vector<const MoveData*> basicMoves() {
    std::vector<const MoveData*> out;
    for (const char* id : {"jab", "cross", "left_hook", "right_hook", "uppercut", "jab_body", "liver_hook", "overhand"}) out.push_back(library().byId(std::string("boxing_") + id));
    return out;
}
}  // namespace

TEST_CASE("group: боец выбирает ближайшего врага и не выбирает своих") {
    CombatSim sim(rules());
    const int p = sim.addFighter({0, 0});
    const int near = sim.addFighter({0, -2.f});
    const int far = sim.addFighter({4.f, 0});
    const int ally = sim.addFighter({0.5f, 0.5f});
    TargetSelector ts;
    ts.setTeam(p, 0);
    ts.setTeam(ally, 0);
    ts.setTeam(near, 1);
    ts.setTeam(far, 1);
    ts.update(sim);
    CHECK(sim.fighter(p).target == near);
    CHECK(sim.fighter(near).target == p);  // для near врагами служат p и ally (одна команда); p ближе
    CHECK(sim.fighter(far).target != far);
}

TEST_CASE("group: прицел игрока перетягивает выбор на цель в направлении взгляда") {
    CombatSim sim(rules());
    const int p = sim.addFighter({0, 0});
    const int a = sim.addFighter({-1.8f, 0});  // чуть ближе, но слева
    const int b = sim.addFighter({0, -2.4f});  // дальше, но прямо по прицелу
    TargetSelector ts;
    ts.setTeam(a, 1);
    ts.setTeam(b, 1);
    ts.update(sim);
    CHECK(sim.fighter(p).target == a);
    ts.setAim(p, {0, -1});
    ts.update(sim);
    CHECK(sim.fighter(p).target == b);
}

TEST_CASE("group: гистерезис — цель не дёргается при почти равных расстояниях") {
    CombatSim sim(rules());
    const int p = sim.addFighter({0, 0});
    const int a = sim.addFighter({-2.0f, 0});
    const int b = sim.addFighter({2.1f, 0});
    TargetSelector ts;
    ts.setTeam(a, 1);
    ts.setTeam(b, 1);
    ts.update(sim);
    CHECK(sim.fighter(p).target == a);
    sim.fighterMutable(b).pos = {1.9f, 0};  // b чуть ближе, но разница меньше порога
    ts.update(sim);
    CHECK(sim.fighter(p).target == a);
    sim.fighterMutable(b).pos = {0.9f, 0};  // заметно ближе — переключение
    ts.update(sim);
    CHECK(sim.fighter(p).target == b);
}

TEST_CASE("group: посреди своего приёма цель не меняется; побеждённый враг сменяется следующим") {
    CombatSim sim(rules());
    const int p = sim.addFighter({0, 0});
    const int a = sim.addFighter({0, -1.1f});
    const int b = sim.addFighter({0, 1.5f});
    TargetSelector ts;
    ts.setTeam(a, 1);
    ts.setTeam(b, 1);
    ts.update(sim);
    REQUIRE(sim.fighter(p).target == a);
    FighterInput hit;
    hit.attack = library().byId("boxing_cross");
    sim.step({hit, {}, {}});
    REQUIRE(sim.fighter(p).state == FighterState::Attacking);
    sim.fighterMutable(b).pos = {0, -0.3f};
    ts.update(sim);
    CHECK(sim.fighter(p).target == a);
    // враг a выбывает — цель сразу меняется на b
    sim.fighterMutable(p).state = FighterState::Idle;
    sim.fighterMutable(a).state = FighterState::KnockedOut;
    ts.update(sim);
    CHECK(sim.fighter(p).target == b);
    sim.fighterMutable(b).state = FighterState::KnockedOut;
    ts.update(sim);
    CHECK(sim.fighter(p).target == -1);
    CHECK(ts.teamDefeated(sim, 1));
    CHECK_FALSE(ts.teamDefeated(sim, 0));
}

TEST_CASE("group: один против троих — жетоны ограничивают одновременные атаки, бой доходит до конца без сбоев") {
    int totalSimultaneousMax = 0;
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        CombatSim sim(rules(), seed);
        TargetSelector ts;
        AttackTokens tokens(2, 70);
        const FighterProfile& vet = *profiles().byId("boxer_veteran");
        const FighterProfile& brawl = *profiles().byId("street_brawler");
        const int p = sim.addFighter({0, 0}, vet.stats());
        std::vector<int> foes;
        const Vec2 spots[3] = {{-2.5f, -2.5f}, {2.5f, -2.5f}, {0.f, 3.f}};
        std::vector<FightAI> ais;
        ais.emplace_back(vet, basicMoves(), rules(), seed, &tokens);
        for (int k = 0; k < 3; ++k) {
            const int f = sim.addFighter(spots[k], brawl.stats());
            foes.push_back(f);
            ts.setTeam(f, 1);
            ais.emplace_back(brawl, basicMoves(), rules(), seed * 17u + static_cast<std::uint32_t>(k), &tokens);
        }
        for (int frame = 0; frame < 3600 && !ts.teamDefeated(sim, 0) && !ts.teamDefeated(sim, 1); ++frame) {
            ts.update(sim);
            std::vector<FighterInput> in;
            for (int i = 0; i < sim.fighterCount(); ++i) in.push_back(ais[static_cast<std::size_t>(i)].decide(sim, i));
            sim.step(in);
            int attackingP = 0;
            for (int f : foes)
                if (sim.fighter(f).target == p && sim.fighter(f).state == FighterState::Attacking) ++attackingP;
            if (attackingP > totalSimultaneousMax) totalSimultaneousMax = attackingP;
        }
        for (int i = 0; i < sim.fighterCount(); ++i) {
            CHECK(sim.fighter(i).health <= 100.f);
            CHECK(sim.fighter(i).stamina >= 0.f);
        }
        // За 60 с кто-то обязан пострадать: бой реально шёл.
        float lost = 0.f;
        for (int i = 0; i < sim.fighterCount(); ++i) lost += 100.f - sim.fighter(i).health;
        CHECK(lost > 20.f);
    }
    // Жетонов два на цель: трое не бьют игрока одновременно.
    CHECK(totalSimultaneousMax <= 2);
}
