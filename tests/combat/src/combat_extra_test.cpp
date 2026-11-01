// Дополнительные тесты ядра боя (G1): переходы состояний, окна контратаки, затухание накопленного урона, согласованность данных.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>

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
const MoveData& mv(const std::string& id) {
    const MoveData* m = library().byId("boxing_" + id);
    REQUIRE_MESSAGE(m != nullptr, id);
    return *m;
}
FighterStats plain() {
    FighterStats s;
    s.technique = 0;
    return s;
}
struct Duel {
    CombatSim sim{rules()};
    int a = 0, b = 1;
    explicit Duel(float d = 1.2f) {
        a = sim.addFighter({0, 0}, plain());
        b = sim.addFighter({0, -d}, plain());
        sim.setTarget(a, b);
        sim.setTarget(b, a);
    }
    void step(const FighterInput& ia = {}, const FighterInput& ib = {}) { sim.step({ia, ib}); }
    void idle(int n) {
        for (int i = 0; i < n; ++i) step();
    }
    Fighter& A() { return sim.fighterMutable(a); }
    Fighter& B() { return sim.fighterMutable(b); }
};
FighterInput attack(const MoveData& m) {
    FighterInput in;
    in.attack = &m;
    return in;
}
FighterInput blockIn() {
    FighterInput in;
    in.blockHeld = true;
    return in;
}
}  // namespace

TEST_CASE("combat+: блок поднимается и снимается, при блоке боец не ходит") {
    Duel d(3.f);
    d.step(blockIn());
    CHECK(d.sim.fighter(d.a).state == FighterState::Blocking);
    FighterInput walk = blockIn();
    walk.forward = 1.f;
    const float z = d.sim.fighter(d.a).pos.z;
    for (int i = 0; i < 30; ++i) d.step(walk);
    CHECK(d.sim.fighter(d.a).pos.z == doctest::Approx(z));
    d.step();
    CHECK(d.sim.fighter(d.a).state == FighterState::Idle);
}

TEST_CASE("combat+: из блока можно сразу ударить и защититься") {
    Duel d(3.f);
    d.step(blockIn());
    FighterInput in = attack(mv("jab"));
    d.step(in);
    CHECK(d.sim.fighter(d.a).state == FighterState::Attacking);
    Duel e(3.f);
    e.step(blockIn());
    FighterInput def;
    def.defense = DefenseAction::Duck;
    e.step(def);
    CHECK(e.sim.fighter(e.a).state == FighterState::Defending);
}

TEST_CASE("combat+: во время защиты и атаки новую защиту начать нельзя") {
    Duel d(3.f);
    FighterInput def;
    def.defense = DefenseAction::SidestepLeft;
    d.step(def);
    const float stamina = d.sim.fighter(d.a).stamina;
    FighterInput second;
    second.defense = DefenseAction::Duck;
    d.step(second);
    CHECK(d.sim.fighter(d.a).action == DefenseAction::SidestepLeft);
    CHECK(d.sim.fighter(d.a).stamina >= stamina);
    Duel e(3.f);
    e.step(attack(mv("cross")));
    e.step(second);
    CHECK(e.sim.fighter(e.a).state == FighterState::Attacking);
}

TEST_CASE("combat+: ввод, ждавший стамины в буфере, срабатывает, когда она восстановилась (в пределах 8 кадров)") {
    Duel d(5.f);
    d.A().stamina = 2.9f;  // джебу нужно 3, восстановление 0,3 ед/кадр
    d.step(attack(mv("jab")));
    CHECK(d.sim.fighter(d.a).state == FighterState::Idle);
    d.idle(3);
    CHECK(d.sim.fighter(d.a).state == FighterState::Attacking);
}

TEST_CASE("combat+: окно контратаки после ухода на угол закрывается через 30 кадров") {
    Duel d;
    d.B().counterReady = 30;
    d.idle(31);
    CHECK(d.sim.fighter(d.b).counterReady == 0);
    const auto hit = [&](bool fresh) {
        Duel x;
        if (fresh) x.B().counterReady = 30;
        x.A().state = FighterState::Idle;
        x.step({}, attack(mv("jab")));
        for (int i = 0; i < 12; ++i) x.step();
        for (const CombatEvent& e : x.sim.log())
            if (e.type == CombatEventType::Hit) return e.damage;
        return 0.f;
    };
    CHECK(hit(true) == doctest::Approx(hit(false) * 1.5f).epsilon(0.001));
}

TEST_CASE("combat+: идеальное парирование оглушает атакующего на 24 кадра") {
    Duel d;
    d.step(attack(mv("jab")));
    for (int i = 2; i < 5; ++i) d.step();
    FighterInput parry;
    parry.defense = DefenseAction::Parry;
    d.step({}, parry);
    d.step();
    REQUIRE(d.sim.fighter(d.a).state == FighterState::BlockStun);
    CHECK(d.sim.fighter(d.a).stunFrames >= 22);
}

TEST_CASE("combat+: урон в голову затухает — редкие удары нокдауна не дают") {
    Duel d;
    for (int i = 0; i < 8; ++i) {
        d.A().stamina = 100.f;
        d.B().balance = 100.f;
        d.step(attack(mv("jab")));
        d.idle(60 * 4);  // четыре секунды между ударами: старые попадания вышли из окна в 3 с
    }
    CHECK(d.sim.fighter(d.b).knockdowns == 0);
    CHECK(d.sim.fighter(d.b).health < 100.f);
    CHECK(d.sim.fighter(d.b).headHits.size() <= 1);
}

TEST_CASE("combat+: удар по нокаутированному и бой после нокаута не создают событий") {
    Duel d;
    d.B().health = 1.f;
    d.step(attack(mv("jab")));
    d.idle(30);
    REQUIRE(d.sim.fighter(d.b).state == FighterState::KnockedOut);
    const std::size_t events = d.sim.log().size();
    d.step(attack(mv("cross")));
    d.idle(30);
    CHECK(d.sim.log().size() == events);
    CHECK(d.sim.fighter(d.b).health == 0.f);
}

TEST_CASE("combat+: бой одного бойца не заканчивается, у двоих — когда останется один живой") {
    CombatSim solo(rules());
    solo.addFighter({0, 0});
    CHECK_FALSE(solo.fightOver());
    Duel d;
    CHECK_FALSE(d.sim.fightOver());
    d.B().state = FighterState::KnockedOut;
    CHECK(d.sim.fightOver());
}

TEST_CASE("combat+: события идут в порядке кадров, номера кадров не убывают") {
    Duel d;
    for (int i = 0; i < 300; ++i) d.step(i % 25 == 0 ? attack(mv("cross")) : FighterInput{}, i % 31 == 0 ? attack(mv("jab")) : FighterInput{});
    int previous = 0;
    for (const CombatEvent& e : d.sim.log()) {
        CHECK(e.frame >= previous);
        previous = e.frame;
    }
    CHECK(d.sim.log().size() > 3);
}

TEST_CASE("combat+: контракт для заблокированного удара и контрудара") {
    Duel d;
    for (int i = 0; i < 5; ++i) d.step({}, blockIn());
    d.step(attack(mv("jab")), blockIn());
    for (int i = 0; i < 8; ++i) d.step({}, blockIn());
    const CombatEvent* blocked = nullptr;
    for (const CombatEvent& e : d.sim.log())
        if (e.type == CombatEventType::Blocked) blocked = &e;
    REQUIRE(blocked != nullptr);
    CHECK(CombatSim::toContract(*blocked, d.sim.fighter(d.a).pos).blocked);
    Duel c;
    c.step(attack(mv("jab")), attack(mv("cross")));
    for (int i = 0; i < 8; ++i) c.step();
    for (const CombatEvent& e : c.sim.log())
        if (e.type == CombatEventType::Hit && e.counter) CHECK(CombatSim::toContract(e, c.sim.fighter(c.a).pos).counter);
}

TEST_CASE("combat+: стамина восстанавливается быстрее вне блока, баланс не растёт в оглушении") {
    Duel d(5.f);
    d.A().stamina = 40.f;
    d.B().stamina = 40.f;
    for (int i = 0; i < 60; ++i) d.step({}, blockIn());
    CHECK(d.sim.fighter(d.a).stamina > d.sim.fighter(d.b).stamina);
    Duel s(5.f);
    s.A().balance = 50.f;
    s.A().state = FighterState::HitStun;
    s.A().stunFrames = 20;
    s.idle(10);
    CHECK(s.sim.fighter(s.a).balance == doctest::Approx(50.f));
    s.idle(20);
    CHECK(s.sim.fighter(s.a).balance > 50.f);
}

TEST_CASE("combat+: первый кадр боя — всё на местах, ресурсы полные") {
    Duel d;
    CHECK(d.sim.fighter(d.a).health == 100.f);
    CHECK(d.sim.fighter(d.a).stamina == 100.f);
    CHECK(d.sim.fighter(d.a).balance == doctest::Approx(rules().balanceMax));
    CHECK(d.sim.fighter(d.a).focus == 100.f);
    CHECK(d.sim.frame() == 0);
    CHECK(d.sim.log().empty());
}

// ---------------------------------------------------------------- Данные: согласованность

TEST_CASE("combat+: у каждой защиты окно внутри длительности, есть охват, есть исходы для всех траекторий") {
    for (const auto& [name, def] : rules().defenses) {
        CHECK_MESSAGE(!def.covers.empty(), name);
        if (!def.hold) {
            CHECK_MESSAGE(def.windowFrom >= 0, name);
            CHECK_MESSAGE(def.windowTo <= def.total, name);
            CHECK_MESSAGE(def.cost >= 0.f, name);
        } else {
            CHECK_MESSAGE(def.startup >= 1, name);
        }
        CHECK_MESSAGE(def.outcome(TrajClass::Straight).multiplier >= 1.f, name);
    }
}

TEST_CASE("combat+: полуугол поражения — прямые уже дуг, вращение охватывает всё") {
    const CombatRules& r = rules();
    CHECK(r.hitArcDegrees(Trajectory::Straight) < r.hitArcDegrees(Trajectory::ArcLeft));
    CHECK(r.hitArcDegrees(Trajectory::ArcLeft) == r.hitArcDegrees(Trajectory::ArcRight));
    CHECK(r.hitArcDegrees(Trajectory::Spin) == doctest::Approx(180.f));
    CHECK(r.hitArcDegrees(Trajectory::Up) > 0.f);
}

TEST_CASE("combat+: доля блока по углу не растёт с ростом угла") {
    float previous = 2.f;
    for (float deg = 0.f; deg <= 180.f; deg += 5.f) {
        const float f = rules().blockFactorForAngle(deg);
        CHECK(f <= previous);
        previous = f;
    }
}

TEST_CASE("combat+: в данных преимущество на попадании не хуже, чем на блоке; силовые и спец имеют большие импульсы") {
    for (const MoveData& m : library().all()) {
        CHECK_MESSAGE(m.onHitFrames >= m.onBlockFrames, m.id);
        if (m.type == "special") {
            CHECK_MESSAGE(m.focus > 0.f, m.id);
            CHECK_MESSAGE(m.impulse >= 300.f, m.id);
        }
        if (m.power) CHECK_MESSAGE(m.damage >= 14.f, m.id);
        CHECK_MESSAGE(m.balance >= m.damage * 0.5f, m.id);  // удар всегда сбивает баланс заметно
    }
}

TEST_CASE("combat+: самый быстрый приём бокса — джеб, самый медленный из обычных — оверхенд") {
    int fastest = 99, slowest = 0;
    std::string fastestId, slowestId;
    for (const MoveData& m : library().all()) {
        if (m.type == "special" || m.type == "counter") continue;
        if (m.startup < fastest) {
            fastest = m.startup;
            fastestId = m.id;
        }
        if (m.startup > slowest) {
            slowest = m.startup;
            slowestId = m.id;
        }
    }
    CHECK(fastestId.find("jab") != std::string::npos);
    CHECK(slowest == 16);
}

TEST_CASE("combat+: контрприёмы быстрее обычных (замах 4) и бьют сильнее джеба") {
    for (const char* id : {"slip_counter", "pull_counter"}) {
        CHECK(mv(id).startup == 4);
        CHECK(mv(id).damage > mv("jab").damage);
    }
}

TEST_CASE("combat+: приёмы без шага в данных дальше не двигают бойца; приёмы с шагом — сдвигают на 0,4 м") {
    for (const char* id : {"jab", "cross"}) {
        Duel d(5.f);
        d.step(attack(mv(id)));
        d.idle(mv(id).totalFrames());
        CHECK(d.sim.fighter(d.a).pos.z == doctest::Approx(0.f));
    }
    Duel s(5.f);
    s.step(attack(mv("double_jab_step")));
    s.idle(mv("double_jab_step").totalFrames());
    CHECK(s.sim.fighter(s.a).pos.z == doctest::Approx(-0.4f).epsilon(0.02));
}

TEST_CASE("combat+: ввод вне окна отмены у приёма с длинным замахом не прерывает его") {
    Duel d(5.f);
    d.step(attack(mv("overhand")));
    d.step(attack(mv("jab")));
    for (int i = 0; i < 5; ++i) d.step();
    CHECK(d.sim.fighter(d.a).move == &mv("overhand"));
}

TEST_CASE("combat+: боец не бьёт, если цель не задана (манекен без цели просто стоит)") {
    CombatSim sim(rules());
    const int a = sim.addFighter({0, 0});
    const int b = sim.addFighter({0, -1});
    FighterInput in = attack(mv("jab"));
    sim.step({in, {}});
    for (int i = 0; i < 12; ++i) sim.step({});
    CHECK(sim.fighter(b).health == 100.f);
    CHECK(sim.fighter(a).health == 100.f);
}
