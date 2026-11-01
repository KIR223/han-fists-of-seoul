// Тесты ИИ бойцов (G3): профили, жетоны атаки, реакция по опыту, выбор защиты по таблице исходов, групповой бой, дуэли, детерминизм.
// Часть проверок статистическая: десятки боёв с разными сидами, пороги выбраны с запасом.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "han/combat/ai.h"

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
const FighterProfile& profile(const char* id) {
    const FighterProfile* p = profiles().byId(id);
    REQUIRE_MESSAGE(p != nullptr, id);
    return *p;
}
std::vector<const MoveData*> basicMoves() {
    std::vector<const MoveData*> out;
    for (const char* id : {"jab", "cross", "left_hook", "right_hook", "uppercut", "jab_body", "liver_hook", "overhand", "slip_counter", "pull_counter"}) out.push_back(library().byId(std::string("boxing_") + id));
    return out;
}
const MoveData& mv(const char* id) { return *library().byId(std::string("boxing_") + id); }

// Профиль «только защита»: ничего не атакует, остальное по заданию.
FighterProfile defender(float experience, float defense, float quality = 1.f) {
    FighterProfile p = profile("boxer_veteran");
    p.experience = experience;
    p.defense = defense;
    p.techniqueQuality = quality;
    p.aggression = 0;
    p.personality = Personality::Balanced;
    p.mistakes.clear();
    p.sobriety = Sobriety::Sober;
    return p;
}

// Бой: боец 0 — скриптовый (кидает один и тот же приём каждые `period` кадров), боец 1 — ИИ без атак. Возвращает долю приёмов, дошедших до цели.
float landRate(const FighterProfile& defenderProfile, const MoveData& move, int rounds, std::uint32_t seed0, int period = 70) {
    int landed = 0, thrown = 0;
    for (int r = 0; r < rounds; ++r) {
        CombatSim sim(rules(), seed0 + static_cast<std::uint32_t>(r));
        FighterStats strong;
        strong.technique = 0;
        const int a = sim.addFighter({0, 0}, strong);
        const int b = sim.addFighter({0, -1.2f}, strong);
        sim.setTarget(a, b);
        sim.setTarget(b, a);
        FightAI ai(defenderProfile, {}, rules(), seed0 * 31u + static_cast<std::uint32_t>(r) + 1u);
        sim.fighterMutable(b).health = 100000.f;  // чтобы бой не кончался
        FighterInput hit;
        hit.attack = &move;
        // Три удара подряд за раунд.
        for (int shot = 0; shot < 3; ++shot) {
            ++thrown;
            const int logBefore = static_cast<int>(sim.log().size());
            for (int f = 0; f < period; ++f) {
                sim.fighterMutable(a).stamina = 100.f;
                sim.step({f == 0 ? hit : FighterInput{}, ai.decide(sim, b)});
                if (sim.fighter(b).state == FighterState::Knockdown) sim.fighterMutable(b).state = FighterState::Idle;
            }
            for (std::size_t i = static_cast<std::size_t>(logBefore); i < sim.log().size(); ++i)
                if (sim.log()[i].type == CombatEventType::Hit) {
                    ++landed;
                    break;
                }
        }
    }
    return static_cast<float>(landed) / static_cast<float>(thrown);
}
}  // namespace

// ---------------------------------------------------------------- Профили

TEST_CASE("profile: реальные профили читаются, значения в диапазонах") {
    CHECK(profiles().size() >= 6);
    for (const FighterProfile& p : profiles().all()) {
        CHECK_MESSAGE((p.experience >= 0.f && p.experience <= 10.f), p.id);
        CHECK_MESSAGE((p.techniqueQuality >= 0.f && p.techniqueQuality <= 1.f), p.id);
        const FighterStats s = p.stats();
        CHECK_MESSAGE((s.strength >= 1.f && s.strength <= 10.f), p.id);
        CHECK_MESSAGE((s.technique >= 0.f && s.technique <= 10.f), p.id);
        CHECK_MESSAGE((s.stars >= 1 && s.stars <= 3), p.id);
    }
    CHECK(profile("boxer_veteran").stats().stars == 3);
    CHECK(profile("nervous_rookie").stats().stars == 1);
    CHECK(profile("boxer_veteran").stats().technique > profile("tipsy_office_worker").stats().technique);
    CHECK(profile("tipsy_office_worker").hasMistake("telegraphs"));
    CHECK_FALSE(profile("boxer_veteran").hasMistake("drops_hands"));
    CHECK(profile("drunk_bruiser").impairment() > profile("tipsy_office_worker").impairment());
    CHECK(profile("boxer_veteran").impairment() == 0.f);
}

TEST_CASE("profile: ошибки данных называют поле") {
    const auto bad = [](const std::string& text, const char* expected) {
        han::core::Json j;
        std::string error;
        REQUIRE(han::core::parseJson(text, j, error));
        FighterProfile p;
        CHECK_FALSE(parseProfile(j, p, error));
        CHECK_MESSAGE(error.find(expected) != std::string::npos, text << " -> " << error);
    };
    const std::string base = R"("archetype":"boxer","experience":5,"striking":5,"defense":5,"cardio":5,"courage":5,"aggression":5,"personality":"balanced","technique_quality":0.5)";
    bad("[]", "объект");
    bad(R"({"id":"BAD",)" + base + "}", "id");
    bad(R"({"id":"ok_one","archetype":"boxer","experience":11,"striking":5,"defense":5,"cardio":5,"courage":5,"aggression":5,"personality":"balanced","technique_quality":0.5})", "experience");
    bad(R"({"id":"ok_one","archetype":"boxer","experience":5,"striking":5,"defense":5,"cardio":5,"courage":5,"aggression":5,"personality":"zen","technique_quality":0.5})", "характер");
    bad(R"({"id":"ok_one","archetype":"boxer","experience":5,"striking":5,"defense":5,"cardio":5,"courage":5,"aggression":5,"personality":"balanced","technique_quality":2})", "technique_quality");
    bad(R"({"id":"ok_one",)" + base + R"(,"sobriety":"wasted"})", "sobriety");
    bad(R"({"id":"ok_one",)" + base + R"(,"mistakes":["trips"]})", "ошибка");
    bad(R"({"id":"ok_one",)" + base + R"(,"action_weights":{"kick":0.5}})", "action_weights");
    bad(R"({"id":"ok_one",)" + base + R"(,"action_weights":{"jab":3}})", "вес");
    bad(R"({"id":"ok_one",)" + base + R"(,"bonus":1})", "лишнее");
    bad(R"({"id":"ok_one","archetype":"boxer","striking":5})", "experience");
}

TEST_CASE("profile: библиотека не принимает повтор id") {
    ProfileLibrary lib;
    std::string error;
    CHECK(lib.add(profile("boxer_veteran"), error));
    CHECK_FALSE(lib.add(profile("boxer_veteran"), error));
    CHECK(error.find("повторяющийся") != std::string::npos);
    CHECK_FALSE(lib.loadDirectory("нет_папки", error));
}

// ---------------------------------------------------------------- Жетоны

TEST_CASE("tokens: не больше N бойцов на цель, жетоны истекают и освобождаются") {
    AttackTokens t(2, 50);
    CHECK(t.tryAcquire(1, 0, 10));
    CHECK(t.tryAcquire(2, 0, 10));
    CHECK_FALSE(t.tryAcquire(3, 0, 10));
    CHECK(t.holders(0, 10) == 2);
    CHECK(t.tryAcquire(1, 0, 20));    // тот же боец — продление, не новый жетон
    CHECK(t.holders(0, 20) == 2);
    t.release(2, 0);
    CHECK(t.tryAcquire(3, 0, 21));
    CHECK_FALSE(t.tryAcquire(4, 0, 22));
    CHECK(t.tryAcquire(4, 0, 200));   // старые истекли
    CHECK(t.holders(7, 0) == 0);      // другая цель — отдельный счёт
    CHECK(t.tryAcquire(1, 7, 30));
}

// ---------------------------------------------------------------- Поведение

TEST_CASE("ai: сближается с далёкого расстояния и начинает бить") {
    CombatSim sim(rules(), 5);
    const int a = sim.addFighter({0, 0}, profile("boxer_veteran").stats());
    const int b = sim.addFighter({0, -4.5f}, FighterStats{});
    sim.setTarget(a, b);
    sim.setTarget(b, a);
    FightAI ai(profile("street_brawler"), basicMoves(), rules(), 3);
    bool approached = false;
    int attackEvents = 0;
    for (int i = 0; i < 600; ++i) {
        sim.step({ai.decide(sim, a), {}});
        if (ai.intent() == FightAI::Intent::Approach) approached = true;
        if (sim.distance(a, b) < 2.0f && i < 200) approached = approached && true;
    }
    for (const CombatEvent& e : sim.log()) attackEvents += e.attacker == a ? 1 : 0;
    CHECK(approached);
    CHECK(ai.attacksStarted() >= 3);
    CHECK(attackEvents >= 1);
    CHECK(sim.distance(a, b) < 2.2f);
}

TEST_CASE("ai: агрессивный боец наносит манекену заметно больше урона, чем осторожный") {
    const auto damageDealt = [](const char* id) {
        float total = 0.f;
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            CombatSim sim(rules(), seed);
            const int a = sim.addFighter({0, 0}, FighterStats{});
            const int b = sim.addFighter({0, -1.4f}, FighterStats{});
            sim.setTarget(a, b);
            sim.setTarget(b, a);
            sim.fighterMutable(b).health = 1e6f;
            FightAI ai(profile(id), basicMoves(), rules(), seed * 7);
            for (int i = 0; i < 600; ++i) {
                sim.fighterMutable(a).stamina = 100.f;
                sim.step({ai.decide(sim, a), {}});
                if (sim.fighter(b).state == FighterState::Knockdown) sim.fighterMutable(b).state = FighterState::Idle;
            }
            for (const CombatEvent& e : sim.log()) total += e.attacker == a ? e.damage : 0.f;
        }
        return total;
    };
    const float brawler = damageDealt("street_brawler"), rookie = damageDealt("nervous_rookie");
    CHECK(brawler > rookie * 1.4f);
    CHECK(rookie > 0.f);
}

TEST_CASE("ai: реакция по опыту — ветеран пропускает заметно меньше джебов, чем новичок") {
    const float veteran = landRate(defender(10, 10), mv("jab"), 20, 100);
    const float rookie = landRate(defender(1, 3), mv("jab"), 20, 100);
    CHECK(rookie > 0.6f);                 // новичок в основном не успевает (часть ударов закрывает защитная стойка)
    CHECK(veteran < rookie - 0.25f);      // ветеран успевает заметно чаще
}

TEST_CASE("ai: от медленного хука опытный боец защищается успешно почти всегда, новичок — нет") {
    const float veteran = landRate(defender(10, 10), mv("left_hook"), 20, 200);
    const float novice = landRate(defender(1, 2), mv("left_hook"), 20, 200);
    CHECK(veteran < 0.25f);
    CHECK(novice > veteran + 0.2f);
}

TEST_CASE("ai: от апперкота умелый боец не ныряет (это ошибка из таблицы): попадание ×1,5 встречается редко") {
    // Лучшие ответы на апперкот — отклон назад или шаг назад; нырок наказывается. Проверяем результат: ветеран уходит чаще, чем получает.
    const float veteran = landRate(defender(10, 10), mv("uppercut"), 20, 300);
    CHECK(veteran < 0.4f);
}

TEST_CASE("ai: слабая защита и опьянение ухудшают результат") {
    FighterProfile drunk = defender(10, 10);
    drunk.sobriety = Sobriety::Drunk;
    const float sober = landRate(defender(10, 10), mv("left_hook"), 20, 400);
    const float tipsy = landRate(drunk, mv("left_hook"), 20, 400);
    FighterProfile clumsy = defender(10, 1, 0.1f);
    const float poor = landRate(clumsy, mv("left_hook"), 20, 400);
    CHECK(tipsy >= sober);
    CHECK(poor > sober + 0.1f);
}

TEST_CASE("ai: защита выбирается из реальных названий, а не из воздуха") {
    CombatSim sim(rules(), 9);
    const int a = sim.addFighter({0, 0}, FighterStats{});
    const int b = sim.addFighter({0, -1.2f}, FighterStats{});
    sim.setTarget(a, b);
    sim.setTarget(b, a);
    FightAI ai(defender(10, 10), {}, rules(), 11);
    FighterInput hit;
    hit.attack = &mv("left_hook");
    for (int i = 0; i < 40; ++i) {
        sim.step({i == 0 ? hit : FighterInput{}, ai.decide(sim, b)});
        if (!ai.lastDefense().empty()) break;
    }
    REQUIRE_FALSE(ai.lastDefense().empty());
    const std::string d = ai.lastDefense();
    CHECK((d == "parry" || d == "slip" || d == "duck" || d == "lean_back" || d == "sidestep" || d == "step_back" || d == "block"));
    CHECK(ai.defensesStarted() >= 1);
}

TEST_CASE("ai: «опущенные руки» после своей атаки ухудшают защиту") {
    const auto taken = [](bool dropsHands) {
        int hits = 0, throws = 0;
        for (std::uint32_t seed = 1; seed <= 15; ++seed) {
            FighterProfile p = profile("boxer_veteran");
            p.aggression = 10;
            p.mistakes.clear();
            if (dropsHands) p.mistakes.push_back("drops_hands");
            CombatSim sim(rules(), seed);
            const int a = sim.addFighter({0, 0}, FighterStats{});
            const int b = sim.addFighter({0, -1.2f}, FighterStats{});
            sim.setTarget(a, b);
            sim.setTarget(b, a);
            sim.fighterMutable(a).health = sim.fighterMutable(b).health = 1e6f;
            FightAI ai(p, basicMoves(), rules(), seed * 13);
            FighterInput jab;
            jab.attack = &mv("jab");
            for (int f = 0; f < 900; ++f) {
                sim.fighterMutable(a).stamina = sim.fighterMutable(b).stamina = 100.f;
                if (f % 30 == 0) ++throws;
                sim.step({f % 30 == 0 ? jab : FighterInput{}, ai.decide(sim, b)});
                for (const FighterState s : {sim.fighter(a).state, sim.fighter(b).state})
                    if (s == FighterState::Knockdown) {
                        sim.fighterMutable(a).state = sim.fighter(a).state == FighterState::Knockdown ? FighterState::Idle : sim.fighter(a).state;
                        sim.fighterMutable(b).state = sim.fighter(b).state == FighterState::Knockdown ? FighterState::Idle : sim.fighter(b).state;
                    }
            }
            for (const CombatEvent& e : sim.log()) hits += (e.attacker == a && e.type == CombatEventType::Hit) ? 1 : 0;
        }
        return static_cast<float>(hits) / static_cast<float>(throws);
    };
    CHECK(taken(true) > taken(false));
}

TEST_CASE("ai: без стамины не атакует; оглушённый и нокаутированный боец ничего не вводит") {
    CombatSim sim(rules(), 3);
    const int a = sim.addFighter({0, 0}, FighterStats{});
    const int b = sim.addFighter({0, -1.2f}, FighterStats{});
    sim.setTarget(a, b);
    sim.setTarget(b, a);
    FightAI ai(profile("street_brawler"), basicMoves(), rules(), 4);
    for (int i = 0; i < 120; ++i) {
        sim.fighterMutable(a).stamina = 1.f;
        const FighterInput in = ai.decide(sim, a);
        CHECK(in.attack == nullptr);
        sim.step({in, {}});
    }
    CHECK(ai.attacksStarted() == 0);
    sim.fighterMutable(a).state = FighterState::HitStun;
    sim.fighterMutable(a).stunFrames = 10;
    const FighterInput stunned = ai.decide(sim, a);
    CHECK((stunned.attack == nullptr && !stunned.blockHeld && stunned.defense == DefenseAction::None));
    sim.fighterMutable(a).state = FighterState::KnockedOut;
    const FighterInput ko = ai.decide(sim, a);
    CHECK((ko.attack == nullptr && ko.forward == 0.f));
}

TEST_CASE("ai: слишком близко — отходит, слишком далеко — приближается; защитный характер чаще встаёт в защиту") {
    CombatSim sim(rules(), 3);
    const int a = sim.addFighter({0, 0}, FighterStats{});
    const int b = sim.addFighter({0, -0.7f}, FighterStats{});
    sim.setTarget(a, b);
    sim.setTarget(b, a);
    FightAI ai(defender(5, 5), {}, rules(), 4);
    const FighterInput close = ai.decide(sim, a);
    CHECK(close.forward < 0.f);
    CombatSim far(rules(), 3);
    const int c = far.addFighter({0, 0}, FighterStats{});
    const int d = far.addFighter({0, -6.f}, FighterStats{});
    far.setTarget(c, d);
    far.setTarget(d, c);
    FightAI ai2(defender(5, 5), {}, rules(), 4);
    CHECK(ai2.decide(far, c).forward > 0.f);
    const auto guardFrames = [](const char* id) {
        int frames = 0;
        for (std::uint32_t seed = 1; seed <= 8; ++seed) {
            CombatSim s(rules(), seed);
            const int x = s.addFighter({0, 0}, FighterStats{});
            const int y = s.addFighter({0, -1.4f}, FighterStats{});
            s.setTarget(x, y);
            s.setTarget(y, x);
            s.fighterMutable(y).health = 1e6f;
            FightAI ai3(profile(id), {}, rules(), seed);
            for (int i = 0; i < 400; ++i) {
                const FighterInput in = ai3.decide(s, x);
                frames += in.blockHeld ? 1 : 0;
                s.step({in, {}});
            }
        }
        return frames;
    };
    CHECK(guardFrames("nervous_rookie") > guardFrames("street_brawler"));
}

TEST_CASE("ai: после идеального парирования или ухода на угол контрбоец быстро отвечает ударом") {
    int answered = 0, evaded = 0;
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        CombatSim sim(rules(), seed);
        const int a = sim.addFighter({0, 0}, FighterStats{});
        const int b = sim.addFighter({0, -1.2f}, FighterStats{});
        sim.setTarget(a, b);
        sim.setTarget(b, a);
        sim.fighterMutable(a).health = 1e6f;
        FightAI ai(profile("counter_puncher"), basicMoves(), rules(), seed * 17);
        FighterInput jab;
        jab.attack = &mv("cross");  // джеб (замах 6 кадров) контрбоец с опытом 7 физически не успевает увидеть; прямой с замахом 9 — успевает
        bool sawEvade = false;
        int evadeFrame = -1;
        for (int f = 0; f < 200; ++f) {
            sim.fighterMutable(a).stamina = 100.f;
            sim.step({f == 0 ? jab : FighterInput{}, ai.decide(sim, b)});
            for (const CombatEvent& e : sim.events()) {
                if (((e.type == CombatEventType::Evaded && e.angle) || e.type == CombatEventType::PerfectParry) && e.defender == b) {
                    sawEvade = true;
                    evadeFrame = sim.frame();
                }
                if (sawEvade && e.attacker == b && (e.type == CombatEventType::Hit || e.type == CombatEventType::Blocked) && sim.frame() - evadeFrame < 60) ++answered;
            }
        }
        evaded += sawEvade ? 1 : 0;
    }
    CHECK(evaded >= 10);   // успешный «правильный» ответ случается регулярно
    CHECK(answered >= 4);  // и ответ ударом следует за ним в окне контратаки
}

// ---------------------------------------------------------------- Групповой бой и дуэли

TEST_CASE("ai: групповой бой — с жетонами одновременно бьют не больше двух, без жетонов — больше") {
    const auto maxAttackers = [](bool useTokens) {
        CombatSim sim(rules(), 2);
        FighterStats tank;
        tank.toughness = 10;
        const int victim = sim.addFighter({0, 0}, tank);
        std::vector<int> ids;
        for (int i = 0; i < 4; ++i) ids.push_back(sim.addFighter({std::cos(1.57f * static_cast<float>(i)) * 1.1f, std::sin(1.57f * static_cast<float>(i)) * 1.1f}, FighterStats{}));
        for (int id : ids) sim.setTarget(id, victim);
        sim.setTarget(victim, ids[0]);
        sim.fighterMutable(victim).health = 1e6f;
        AttackTokens tokens(2, 60);
        std::vector<FightAI> ais;
        FighterProfile eager = profile("street_brawler");
        eager.aggression = 10;
        for (std::size_t i = 0; i < ids.size(); ++i) ais.emplace_back(eager, basicMoves(), rules(), 50u + static_cast<std::uint32_t>(i), useTokens ? &tokens : nullptr);
        int most = 0;
        for (int f = 0; f < 900; ++f) {
            std::vector<FighterInput> in(5);
            for (std::size_t i = 0; i < ids.size(); ++i) {
                sim.fighterMutable(ids[i]).stamina = 100.f;
                in[static_cast<std::size_t>(ids[i])] = ais[i].decide(sim, ids[i]);
            }
            sim.step(in);
            int attacking = 0;
            for (int id : ids) attacking += (sim.fighter(id).state == FighterState::Attacking) ? 1 : 0;
            most = std::max(most, attacking);
            if (sim.fighter(victim).state == FighterState::Knockdown) sim.fighterMutable(victim).state = FighterState::Idle;
        }
        return most;
    };
    CHECK(maxAttackers(true) <= 2);
    CHECK(maxAttackers(false) >= 3);
}

TEST_CASE("ai: дуэль ветерана и новичка — ветеран выигрывает почти всегда") {
    int veteranWins = 0, decided = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        CombatSim sim(rules(), seed);
        const int a = sim.addFighter({0, 0}, profile("boxer_veteran").stats());
        const int b = sim.addFighter({0, -2.0f}, profile("nervous_rookie").stats());
        sim.setTarget(a, b);
        sim.setTarget(b, a);
        FightAI va(profile("boxer_veteran"), basicMoves(), rules(), seed * 3), vb(profile("nervous_rookie"), basicMoves(), rules(), seed * 5);
        for (int f = 0; f < 60 * 45 && !sim.fightOver(); ++f) sim.step({va.decide(sim, a), vb.decide(sim, b)});
        const float ha = sim.fighter(a).health, hb = sim.fighter(b).health;
        if (ha != hb) {
            ++decided;
            veteranWins += ha > hb ? 1 : 0;
        }
    }
    CHECK(decided >= 18);
    CHECK(veteranWins >= 15);
}

TEST_CASE("ai: два одинаковых бойца дерутся на равных — ни один не выигрывает всегда") {
    int firstWins = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        CombatSim sim(rules(), seed);
        const int a = sim.addFighter({0, 0}, profile("boxer_veteran").stats());
        const int b = sim.addFighter({0, -2.0f}, profile("boxer_veteran").stats());
        sim.setTarget(a, b);
        sim.setTarget(b, a);
        FightAI x(profile("boxer_veteran"), basicMoves(), rules(), seed * 3), y(profile("boxer_veteran"), basicMoves(), rules(), seed * 5 + 1);
        for (int f = 0; f < 60 * 45 && !sim.fightOver(); ++f) sim.step({x.decide(sim, a), y.decide(sim, b)});
        firstWins += sim.fighter(a).health > sim.fighter(b).health ? 1 : 0;
    }
    CHECK(firstWins >= 4);
    CHECK(firstWins <= 16);
}

TEST_CASE("ai: бой воспроизводим — тот же сид даёт тот же журнал, другой — другой") {
    const auto fight = [](std::uint32_t seed) {
        CombatSim sim(rules(), seed);
        const int a = sim.addFighter({0, 0}, profile("street_brawler").stats());
        const int b = sim.addFighter({0, -2.f}, profile("counter_puncher").stats());
        sim.setTarget(a, b);
        sim.setTarget(b, a);
        FightAI x(profile("street_brawler"), basicMoves(), rules(), seed * 3), y(profile("counter_puncher"), basicMoves(), rules(), seed * 5);
        for (int f = 0; f < 1200; ++f) sim.step({x.decide(sim, a), y.decide(sim, b)});
        double sum = 0;
        for (const CombatEvent& e : sim.log()) sum += e.frame * 0.37 + e.damage + static_cast<int>(e.type);
        return sum;
    };
    CHECK(fight(5) == fight(5));
    CHECK(fight(5) != fight(6));
}

TEST_CASE("ai: 5 минут боя двух ИИ — ресурсы в пределах, без NaN и зависаний") {
    CombatSim sim(rules(), 9);
    const int a = sim.addFighter({0, 0}, profile("drunk_bruiser").stats());
    const int b = sim.addFighter({0, -2.f}, profile("tipsy_office_worker").stats());
    sim.setTarget(a, b);
    sim.setTarget(b, a);
    sim.setOfficialRules(false);
    FightAI x(profile("drunk_bruiser"), basicMoves(), rules(), 1), y(profile("tipsy_office_worker"), basicMoves(), rules(), 2);
    for (int f = 0; f < 60 * 300 && !sim.fightOver(); ++f) {
        sim.step({x.decide(sim, a), y.decide(sim, b)});
        for (int i = 0; i < 2; ++i) {
            const Fighter& fi = sim.fighter(i);
            REQUIRE((fi.health >= 0.f && fi.health <= 100.f));
            REQUIRE((fi.stamina >= -0.01f && fi.stamina <= 100.f));
            REQUIRE((std::isfinite(fi.pos.x) && std::isfinite(fi.pos.z)));
        }
    }
    CHECK(sim.fightOver());  // слабые пьяные бойцы за 5 минут кого-то нокаутируют
}
