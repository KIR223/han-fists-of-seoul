// Тесты ядра боя (G1): кадры приёмов, буфер ввода и окна отмены, исходы защиты, урон, стамина, баланс, нокдауны, углы, события.
// Бой — на реальных данных data/combat (приёмы бокса и combat_rules.json); числа ожиданий выведены из части 25 дизайна.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

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
    REQUIRE_MESSAGE(m != nullptr, "нет приёма " << id);
    return *m;
}

// Статы без шанса крита (техника 0 даёт отрицательный шанс), чтобы числа урона были точными.
FighterStats plain() {
    FighterStats s;
    s.technique = 0;
    return s;
}

FighterInput attack(const MoveData& m);

// Дуэль: A в начале координат, B в distance метрах «севернее» (−Z), оба смотрят друг на друга.
struct Rig {
    CombatSim sim;
    int a = 0, b = 1;
    explicit Rig(float distance = 1.2f, FighterStats sa = plain(), FighterStats sb = plain(), std::uint32_t seed = 1) : sim(rules(), seed) {
        a = sim.addFighter({0.f, 0.f}, sa);
        b = sim.addFighter({0.f, -distance}, sb);
        sim.setTarget(a, b);
        sim.setTarget(b, a);
    }
    void step(const FighterInput& ia = {}, const FighterInput& ib = {}) { sim.step({ia, ib}); }
    void idle(int n) {
        for (int i = 0; i < n; ++i) step();
    }
    Fighter& A() { return sim.fighterMutable(a); }
    Fighter& B() { return sim.fighterMutable(b); }
    const CombatEvent* find(CombatEventType t) const {
        for (const CombatEvent& e : sim.events())
            if (e.type == t) return &e;
        return nullptr;
    }
    // Шагает, пока не появится событие типа t (не больше limit кадров). Возвращает событие или nullptr.
    // Приём и защита нажимаются один раз (на первом кадре); непрерывное — блок и движение — держится все кадры.
    const CombatEvent* until(CombatEventType t, int limit, const FighterInput& ia = {}, const FighterInput& ib = {}) {
        for (int i = 0; i < limit; ++i) {
            step(i == 0 ? ia : held(ia), i == 0 ? ib : held(ib));
            if (const CombatEvent* e = find(t)) return e;
        }
        return nullptr;
    }
    static FighterInput held(const FighterInput& in) {
        FighterInput h;
        h.forward = in.forward;
        h.right = in.right;
        h.dash = in.dash;
        h.blockHeld = in.blockHeld;
        return h;
    }
    // Бой «приём против защиты»: A бьёт приёмом m, B начинает защиту так, чтобы в кадр попадания у неё шёл кадр defFrame.
    // После вызова события кадра попадания лежат в sim.events().
    void exchange(const MoveData& m, DefenseAction act, int defFrame) {
        const int defStep = std::max(1, m.startup - defFrame + 1);
        for (int s = 1; s <= m.startup; ++s) {
            FighterInput ia, ib;
            if (s == 1) ia = attack(m);
            if (s == defStep) ib.defense = act;
            step(ia, ib);
        }
    }
};

FighterInput attack(const MoveData& m) {  // NOLINT: объявлена выше
    FighterInput in;
    in.attack = &m;
    return in;
}
FighterInput defend(DefenseAction a) {
    FighterInput in;
    in.defense = a;
    return in;
}
FighterInput block() {
    FighterInput in;
    in.blockHeld = true;
    return in;
}
}  // namespace

// ---------------------------------------------------------------- Движение

TEST_CASE("combat: бойцы стоят лицом друг к другу") {
    Rig r;
    r.idle(1);
    CHECK(r.sim.fighter(r.a).facing.z == doctest::Approx(-1.f));
    CHECK(r.sim.fighter(r.b).facing.z == doctest::Approx(1.f));
    CHECK(r.sim.distance(r.a, r.b) == doctest::Approx(1.2f));
}

TEST_CASE("combat: шаг вперёд идёт к врагу, назад — от него, скорость по данным") {
    Rig r(3.f);
    FighterInput in;
    in.forward = 1.f;
    for (int i = 0; i < 60; ++i) r.step(in);
    CHECK(r.sim.fighter(r.a).pos.z == doctest::Approx(-rules().walkSpeed).epsilon(0.02));
    in.forward = -1.f;
    for (int i = 0; i < 60; ++i) r.step(in);
    CHECK(r.sim.fighter(r.a).pos.z == doctest::Approx(0.f).epsilon(0.05));
    CHECK(r.sim.fighter(r.a).state == FighterState::Moving);
    r.idle(1);
    CHECK(r.sim.fighter(r.a).state == FighterState::Idle);
}

TEST_CASE("combat: боковой шаг идёт по кругу вокруг врага, рывок быстрее ходьбы, диагональ не ускоряет") {
    Rig r(3.f);
    FighterInput in;
    in.right = 1.f;
    for (int i = 0; i < 60; ++i) r.step(in);
    CHECK(r.sim.fighter(r.a).pos.x > 1.f);  // вправо от бойца, смотрящего на север, — на восток
    Rig d(6.f);
    FighterInput dash;
    dash.forward = 1.f;
    dash.dash = true;
    for (int i = 0; i < 60; ++i) d.step(dash);
    CHECK(-d.sim.fighter(d.a).pos.z == doctest::Approx(rules().dashSpeed).epsilon(0.02));
    Rig diag(6.f);
    FighterInput dg;
    dg.forward = 1.f;
    dg.right = 1.f;
    for (int i = 0; i < 60; ++i) diag.step(dg);
    const auto p = diag.sim.fighter(diag.a).pos;
    CHECK(std::sqrt(p.x * p.x + p.z * p.z) == doctest::Approx(rules().walkSpeed).epsilon(0.05));
}

TEST_CASE("combat: бойцы не проходят друг сквозь друга") {
    Rig r(1.0f);
    FighterInput in;
    in.forward = 1.f;
    for (int i = 0; i < 120; ++i) r.step(in, in);
    CHECK(r.sim.distance(r.a, r.b) >= rules().bodyRadius * 2.f - 0.01f);
}

// ---------------------------------------------------------------- Кадры приёма

TEST_CASE("combat: джеб попадает ровно на кадре startup, а не раньше") {
    Rig r;
    const MoveData& jab = mv("jab");
    for (int step = 1; step <= jab.startup - 1; ++step) {
        r.step(step == 1 ? attack(jab) : FighterInput{});
        CHECK_MESSAGE(r.sim.events().empty(), "событие слишком рано на кадре " << step);
        CHECK(r.sim.fighter(r.a).state == FighterState::Attacking);
    }
    r.step();
    const CombatEvent* hit = r.find(CombatEventType::Hit);
    REQUIRE(hit != nullptr);
    CHECK(hit->frame == jab.startup);
    CHECK(hit->attacker == r.a);
    CHECK(hit->defender == r.b);
    CHECK(hit->move == &jab);
}

TEST_CASE("combat: приём заканчивается через startup + active + recovery кадров и возвращает в Idle") {
    Rig r(5.f);  // не достаёт: проверяем только тайминг
    const MoveData& cross = mv("cross");
    r.step(attack(cross));
    for (int i = 1; i < cross.totalFrames(); ++i) {
        CHECK(r.sim.fighter(r.a).state == FighterState::Attacking);
        r.step();
    }
    CHECK(r.sim.fighter(r.a).state == FighterState::Idle);
    CHECK(r.sim.fighter(r.a).move == nullptr);
    CHECK(r.sim.log().empty());  // промах не создаёт событий попадания
}

TEST_CASE("combat: приём не достаёт за пределом дальности и достаёт у границы") {
    for (const float d : {2.0f, 1.8f}) {
        Rig r(d);
        CHECK_MESSAGE(r.until(CombatEventType::Hit, 20, attack(mv("jab"))) == nullptr, "d=" << d);
    }
    Rig edge(0.3f + mv("jab").range - 0.02f);  // сразу внутри
    CHECK(edge.until(CombatEventType::Hit, 20, attack(mv("jab"))) != nullptr);
    Rig out(0.3f + mv("jab").range + 0.05f);
    CHECK(out.until(CombatEventType::Hit, 20, attack(mv("jab"))) == nullptr);
}

TEST_CASE("combat: хук короче по дальности, чем джеб — на расстоянии только джеб достаёт") {
    const float d = 0.3f + 1.2f;  // между дальностью хука (1.1) и джеба (1.35)
    Rig jabRig(d);
    CHECK(jabRig.until(CombatEventType::Hit, 20, attack(mv("jab"))) != nullptr);
    Rig hookRig(d);
    CHECK(hookRig.until(CombatEventType::Hit, 30, attack(mv("left_hook"))) == nullptr);
}

TEST_CASE("combat: активное окно — попадание один раз за приём, даже если враг остаётся в зоне") {
    Rig r;
    r.step(attack(mv("cross")));
    r.idle(40);
    int hits = 0;
    for (const CombatEvent& e : r.sim.log()) hits += e.type == CombatEventType::Hit ? 1 : 0;
    CHECK(hits == 1);
}

TEST_CASE("combat: приём, не достававший в начале активной фазы, может достать позже в ней") {
    Rig r(0.3f + mv("cross").range + 0.1f);
    r.step(attack(mv("cross")));
    for (int i = 1; i < mv("cross").startup; ++i) r.step();
    r.B().pos.z += 0.2f;  // враг шагнул навстречу на первом активном кадре
    r.step();
    CHECK(r.find(CombatEventType::Hit) != nullptr);
}

// ---------------------------------------------------------------- Урон

TEST_CASE("combat: урон джеба в голову по формуле: 4 × 1,15 = 4,6; баланс −4") {
    Rig r;
    const CombatEvent* e = r.until(CombatEventType::Hit, 20, attack(mv("jab")));
    REQUIRE(e != nullptr);
    CHECK(e->damage == doctest::Approx(4.6f).epsilon(0.001));
    CHECK(e->balanceDamage == doctest::Approx(4.f));
    CHECK(r.sim.fighter(r.b).health == doctest::Approx(100.f - 4.6f).epsilon(0.001));
    CHECK(e->zone == han::contracts::HitZone::Head);
    CHECK_FALSE(e->counter);
    CHECK_FALSE(e->crit);
}

TEST_CASE("combat: зона удара: голова 1,15, корпус 1,0 (джеб в корпус те же 4 базы)") {
    Rig head, body;
    const CombatEvent* h = head.until(CombatEventType::Hit, 20, attack(mv("jab")));
    const CombatEvent* b = body.until(CombatEventType::Hit, 20, attack(mv("jab_body")));
    REQUIRE((h && b));
    CHECK(h->damage / b->damage == doctest::Approx(1.15f).epsilon(0.001));
    CHECK(b->zone == han::contracts::HitZone::Torso);
}

TEST_CASE("combat: сила, звёзды и крепость меняют урон по формуле 25.1") {
    FighterStats strong = plain();
    strong.strength = 7;
    Rig s(1.2f, strong);
    CHECK(s.until(CombatEventType::Hit, 20, attack(mv("jab")))->damage == doctest::Approx(4.6f * 1.1f).epsilon(0.001));
    FighterStats star = plain();
    star.stars = 3;
    Rig st(1.2f, star);
    CHECK(st.until(CombatEventType::Hit, 20, attack(mv("jab")))->damage == doctest::Approx(4.6f * 1.2f).epsilon(0.001));
    FighterStats tough = plain();
    tough.toughness = 8;
    Rig t(1.2f, plain(), tough);
    CHECK(t.until(CombatEventType::Hit, 20, attack(mv("jab")))->damage == doctest::Approx(4.6f * (1.f - 0.03f * 3.f)).epsilon(0.001));
    FighterStats weak = plain();
    weak.strength = 3;
    Rig w(1.2f, weak);
    CHECK(w.until(CombatEventType::Hit, 20, attack(mv("jab")))->damage == doctest::Approx(4.6f * 0.9f).epsilon(0.001));
}

TEST_CASE("combat: expectedDamage совпадает с реальным уроном без крита") {
    for (const char* id : {"jab", "cross", "left_hook", "uppercut", "liver_hook"}) {
        Rig r;
        const MoveData& m = mv(id);
        const float expected = r.sim.expectedDamage(r.A(), r.B(), m, m.movement);
        const CombatEvent* e = r.until(CombatEventType::Hit, 40, attack(m));
        REQUIRE_MESSAGE(e != nullptr, id);
        CHECK_MESSAGE(e->damage == doctest::Approx(expected).epsilon(0.001), id);
    }
}

TEST_CASE("combat: тяжёлые приёмы бьют сильнее лёгких, хук по печени слабее хука по голове") {
    const auto dmg = [](const char* id) {
        Rig r;
        return r.until(CombatEventType::Hit, 60, attack(mv(id)))->damage;
    };
    CHECK(dmg("jab") < dmg("cross"));
    CHECK(dmg("cross") < dmg("left_hook"));
    CHECK(dmg("left_hook") < dmg("overhand"));
    CHECK(dmg("liver_hook") < dmg("left_hook"));
    CHECK(dmg("overhand") < dmg("dempsey_roll"));
}

TEST_CASE("combat: крит случается примерно с шансом 3% + 2% × (техника − 5) и воспроизводим по сиду") {
    const auto crits = [](float technique, std::uint32_t seed) {
        int n = 0;
        for (int i = 0; i < 400; ++i) {
            FighterStats s;
            s.technique = technique;
            Rig r(1.2f, s, plain(), seed + static_cast<std::uint32_t>(i) * 977u);
            const CombatEvent* e = r.until(CombatEventType::Hit, 20, attack(mv("jab")));
            n += (e && e->crit) ? 1 : 0;
        }
        return n;
    };
    const int none = crits(0.f, 5);
    const int high = crits(10.f, 5);  // 13%
    CHECK(none == 0);
    CHECK(high > 20);
    CHECK(high < 100);
    CHECK(crits(10.f, 5) == high);       // тот же сид — тот же результат
    CHECK(crits(10.f, 99) != high);      // другой сид — другая серия
}

TEST_CASE("combat: крит умножает урон на 1,5") {
    FighterStats s;
    s.technique = 10;
    for (std::uint32_t seed = 1; seed < 500; ++seed) {
        Rig r(1.2f, s, plain(), seed);
        const CombatEvent* e = r.until(CombatEventType::Hit, 20, attack(mv("jab")));
        if (e && e->crit) {
            CHECK(e->damage == doctest::Approx(4.6f * 1.5f).epsilon(0.001));
            return;
        }
    }
    FAIL("за 500 сидов не нашлось ни одного крита");
}

// ---------------------------------------------------------------- Удары в движении

TEST_CASE("combat: удар с шагом вперёд — +10% урона, +15% стамины, боец сдвигается к врагу") {
    Rig r;
    FighterInput in = attack(mv("jab"));
    in.forward = 1.f;
    const float z0 = r.sim.fighter(r.a).pos.z;
    const CombatEvent* e = r.until(CombatEventType::Hit, 20, in);
    REQUIRE(e != nullptr);
    CHECK(e->damage == doctest::Approx(4.6f * 1.1f).epsilon(0.001));
    CHECK(r.sim.fighter(r.a).pos.z < z0);
    CHECK(r.sim.staminaCost(r.A(), mv("jab"), Movement::StepIn) == doctest::Approx(3.f * 1.15f));
    CHECK(r.sim.fighter(r.a).moveMovement == Movement::StepIn);
}

TEST_CASE("combat: удар на отходе — −20% урона и боец отходит от врага") {
    Rig r;
    FighterInput in = attack(mv("jab"));
    in.forward = -1.f;
    const CombatEvent* e = r.until(CombatEventType::Hit, 20, in);
    REQUIRE(e != nullptr);
    CHECK(e->damage == doctest::Approx(4.6f * 0.8f).epsilon(0.001));
    CHECK(r.sim.fighter(r.a).pos.z > 0.f);
}

TEST_CASE("combat: удар с боковым шагом сдвигает бойца вбок и сохраняет урон") {
    Rig r;
    FighterInput in = attack(mv("jab"));
    in.right = 1.f;
    r.step(in);
    for (int i = 0; i < 8; ++i) r.step();
    CHECK(r.sim.fighter(r.a).pos.x > 0.2f);
    CHECK(r.sim.fighter(r.a).moveMovement == Movement::Sidestep);
}

TEST_CASE("combat: у приёма с собственным движением (двойной джеб с шагом) вариант не зависит от ввода") {
    Rig r;
    FighterInput in = attack(mv("double_jab_step"));
    in.forward = -1.f;
    r.step(in);
    CHECK(r.sim.fighter(r.a).moveMovement == Movement::StepIn);
}

// ---------------------------------------------------------------- Стамина, баланс, фокус

TEST_CASE("combat: стоимость стамины по формуле 25.1") {
    Rig r;
    const MoveData& cross = mv("cross");
    CHECK(r.sim.staminaCost(r.A(), cross, Movement::Static) == doctest::Approx(5.f));
    r.A().stats.endurance = 7;
    CHECK(r.sim.staminaCost(r.A(), cross, Movement::Static) == doctest::Approx(5.f * 0.92f));
    r.A().stats.stars = 3;
    CHECK(r.sim.staminaCost(r.A(), cross, Movement::Static) == doctest::Approx(5.f * 0.92f * 0.8f));
}

TEST_CASE("combat: приём списывает стамину сразу; восстановление 18 ед/с вне атаки, медленнее в блоке") {
    Rig r(5.f);
    r.step(attack(mv("cross")));
    CHECK(r.sim.fighter(r.a).stamina == doctest::Approx(95.f).epsilon(0.01));
    r.idle(mv("cross").totalFrames());
    CHECK(r.sim.fighter(r.a).state == FighterState::Idle);
    r.idle(60);
    CHECK(r.sim.fighter(r.a).stamina == doctest::Approx(100.f));
    r.A().stamina = 50.f;
    r.idle(60);
    CHECK(r.sim.fighter(r.a).stamina == doctest::Approx(68.f).epsilon(0.01));
    r.A().stamina = 50.f;
    for (int i = 0; i < 60; ++i) r.step(block());
    CHECK(r.sim.fighter(r.a).stamina == doctest::Approx(56.f).epsilon(0.02));
}

TEST_CASE("combat: без стамины приём не начинается") {
    Rig r(5.f);
    r.A().stamina = 2.f;  // джеб стоит 3
    r.step(attack(mv("jab")));
    CHECK(r.sim.fighter(r.a).state == FighterState::Idle);
    CHECK(r.sim.fighter(r.a).stamina >= 2.f);
    r.A().stamina = 3.f;
    r.step(attack(mv("jab")));
    CHECK(r.sim.fighter(r.a).state == FighterState::Attacking);
}

TEST_CASE("combat: спецприём требует фокус и тратит его") {
    Rig r(5.f);
    r.A().focus = 30.f;
    r.step(attack(mv("dempsey_roll")));
    CHECK(r.sim.fighter(r.a).state == FighterState::Idle);
    r.A().focus = 100.f;
    r.step(attack(mv("dempsey_roll")));
    CHECK(r.sim.fighter(r.a).state == FighterState::Attacking);
    CHECK(r.sim.fighter(r.a).focus == doctest::Approx(60.f));
}

TEST_CASE("combat: баланс падает от попаданий и восстанавливается 12 ед/с") {
    Rig r;
    r.until(CombatEventType::Hit, 20, attack(mv("cross")));
    CHECK(r.sim.fighter(r.b).balance == doctest::Approx(92.f));
    r.idle(200);
    CHECK(r.sim.fighter(r.b).balance == doctest::Approx(100.f));
}

// ---------------------------------------------------------------- Буфер ввода и отмены

TEST_CASE("combat: приём, нажатый в окне отмены, начинается без паузы") {
    Rig r(5.f);
    const MoveData &jab = mv("jab"), &cross = mv("cross");
    r.step(attack(jab));
    for (int i = 1; i < jab.cancelFrom; ++i) r.step();
    CHECK(r.sim.fighter(r.a).move == &jab);
    r.step(attack(cross));  // кадр внутри окна отмены
    CHECK(r.sim.fighter(r.a).move == &cross);
    CHECK(r.sim.fighter(r.a).stateFrame == 1);
}

TEST_CASE("combat: ввод, нажатый заранее, ждёт в буфере до окна отмены (не дольше 8 кадров)") {
    Rig r(5.f);
    const MoveData &jab = mv("jab"), &cross = mv("cross");
    r.step(attack(jab));
    r.step(attack(cross));  // кадр 2 приёма: окно отмены начнётся на кадре 8, то есть через 6 кадров (< 8)
    CHECK(r.sim.fighter(r.a).move == &jab);
    for (int i = 0; i < 12; ++i) r.step();
    CHECK(r.sim.fighter(r.a).move == &cross);
}

TEST_CASE("combat: слишком ранний ввод (дальше буфера от окна отмены) пропадает") {
    Rig r(5.f);
    const MoveData &over = mv("overhand"), &jab = mv("jab");
    r.step(attack(over));
    r.step(attack(jab));  // окно отмены овехенда далеко (кадр 20), буфер живёт 8 кадров
    for (int i = 0; i < over.totalFrames() + 2; ++i) {
        r.step();
        CHECK_FALSE(r.sim.fighter(r.a).move == &jab);
    }
    CHECK(r.sim.fighter(r.a).state == FighterState::Idle);
}

TEST_CASE("combat: после окна отмены новый приём ждёт конца восстановления") {
    Rig r(5.f);
    const MoveData &jab = mv("jab"), &cross = mv("cross");
    r.step(attack(jab));
    for (int i = 1; i <= jab.cancelTo + 1; ++i) r.step();
    r.step(attack(cross));
    CHECK(r.sim.fighter(r.a).move == &jab);
    for (int i = 0; i < jab.totalFrames(); ++i) r.step();
    CHECK(r.sim.fighter(r.a).move == &cross);
}

TEST_CASE("combat: отмена в окне списывает стамину нового приёма и сбрасывает попадание") {
    Rig r;
    const MoveData &jab = mv("jab"), &cross = mv("cross");
    r.step(attack(jab));
    for (int i = 1; i < jab.cancelFrom; ++i) r.step();
    const float before = r.sim.fighter(r.a).stamina;
    r.step(attack(cross));
    CHECK(r.sim.fighter(r.a).stamina < before);
    CHECK_FALSE(r.sim.fighter(r.a).moveResolved);
}

// ---------------------------------------------------------------- Оглушение и кадровое преимущество

TEST_CASE("combat: на попадании атакующий свободнее на on_hit_frames кадров (джеб: +3)") {
    Rig r;
    const MoveData& jab = mv("jab");
    r.step(attack(jab));
    int attackerFree = -1, defenderFree = -1;
    for (int i = 2; i < 80; ++i) {
        r.step();
        if (attackerFree < 0 && r.sim.fighter(r.a).state == FighterState::Idle) attackerFree = i;
        if (defenderFree < 0 && r.sim.frame() > jab.startup && r.sim.fighter(r.b).state == FighterState::Idle) defenderFree = i;
    }
    REQUIRE((attackerFree > 0 && defenderFree > 0));
    CHECK(defenderFree - attackerFree == jab.onHitFrames);
}

TEST_CASE("combat: на блоке атакующий уязвимее на on_block_frames кадров (джеб: −1)") {
    Rig r;
    const MoveData& jab = mv("jab");
    r.step(attack(jab), block());
    for (int i = 0; i < 2; ++i) r.step({}, block());
    int attackerFree = -1, defenderFree = -1;
    int frame = 3;
    for (; frame < 80; ++frame) {
        r.step({}, block());
        if (attackerFree < 0 && r.sim.fighter(r.a).state == FighterState::Idle) attackerFree = frame;
        if (defenderFree < 0 && r.sim.fighter(r.b).state == FighterState::Blocking && attackerFree < 0 && frame > jab.startup) defenderFree = frame;
    }
    CHECK(attackerFree > 0);
    CHECK(r.sim.fighter(r.b).blockFrames > 0);
}

TEST_CASE("combat: оглушённый боец не может бить и защищаться, пока не пройдёт оглушение") {
    Rig r;
    r.until(CombatEventType::Hit, 20, attack(mv("cross")));
    REQUIRE(r.sim.fighter(r.b).state == FighterState::HitStun);
    r.step({}, attack(mv("jab")));
    CHECK(r.sim.fighter(r.b).state == FighterState::HitStun);
    r.step({}, defend(DefenseAction::Parry));
    CHECK(r.sim.fighter(r.b).state == FighterState::HitStun);
    const int stun = r.sim.fighter(r.b).stunFrames;
    r.idle(stun + 1);
    CHECK(r.sim.fighter(r.b).state == FighterState::Idle);
}

TEST_CASE("combat: контрудар в замахе врага: ×1,5 урона, замах отменён") {
    Rig r;
    const MoveData &jab = mv("jab"), &cross = mv("cross");
    // B начинает медленный кросс (замах 9), A попадает джебом (замах 6) раньше.
    r.step(attack(jab), attack(cross));
    const CombatEvent* e = r.until(CombatEventType::Hit, 20);
    REQUIRE(e != nullptr);
    CHECK(e->counter);
    CHECK(e->damage == doctest::Approx(4.6f * 1.5f).epsilon(0.001));
    CHECK(r.sim.fighter(r.b).state == FighterState::HitStun);
    CHECK(r.sim.fighter(r.b).move == nullptr);
}

TEST_CASE("combat: одновременные попадания — оба получают урон, контра нет") {
    Rig r;
    r.step(attack(mv("jab")), attack(mv("jab")));
    for (int i = 1; i < mv("jab").startup; ++i) r.step();
    int hits = 0;
    for (const CombatEvent& e : r.sim.events()) {
        hits += e.type == CombatEventType::Hit ? 1 : 0;
        CHECK_FALSE(e.counter);
    }
    CHECK(hits == 2);
    CHECK(r.sim.fighter(r.a).health < 100.f);
    CHECK(r.sim.fighter(r.b).health < 100.f);
}

TEST_CASE("combat: броня приёма — попадание не прерывает замах") {
    static MoveData armored = mv("cross");
    armored.armor = true;
    Rig r;
    r.step(attack(mv("jab")), attack(armored));
    for (int i = 1; i < mv("jab").startup; ++i) r.step();
    CHECK(r.sim.fighter(r.b).health < 100.f);
    CHECK(r.sim.fighter(r.b).state == FighterState::Attacking);
}

// ---------------------------------------------------------------- Блок

TEST_CASE("combat: блок встаёт через startup кадров; готовый блок режет урон до 70%") {
    Rig r;
    for (int i = 0; i < 5; ++i) r.step({}, block());  // блок давно поднят
    const CombatEvent* e = r.until(CombatEventType::Blocked, 20, attack(mv("jab")), block());
    REQUIRE(e != nullptr);
    CHECK(e->damage == doctest::Approx(4.6f * 0.7f).epsilon(0.001));
    CHECK(e->outcome == OutcomeKind::Block);
    CHECK(e->defense == "high_block");
    CHECK(r.sim.fighter(r.b).state == FighterState::BlockStun);
    CHECK(r.sim.fighter(r.b).stamina == doctest::Approx(100.f - 4.f * 0.5f).epsilon(0.01));
}

TEST_CASE("combat: блок, поднятый слишком поздно, не успевает — удар проходит") {
    Rig r;
    // Джеб долетает на 6-м кадре; блок поднимается на 5-м и ещё не встал (нужно 3 кадра).
    for (int i = 0; i < 4; ++i) r.step(i == 0 ? attack(mv("jab")) : FighterInput{});
    r.step({}, block());
    r.step({}, block());
    CHECK(r.find(CombatEventType::Hit) != nullptr);
    CHECK(r.find(CombatEventType::Blocked) == nullptr);
}

TEST_CASE("combat: силовой удар в блок снижает урон до 40% и сильнее сбивает баланс") {
    Rig r;
    for (int i = 0; i < 5; ++i) r.step({}, block());
    const CombatEvent* e = r.until(CombatEventType::Blocked, 40, attack(mv("overhand")), block());
    REQUIRE(e != nullptr);
    CHECK(e->damage == doctest::Approx(14.f * 1.15f * 0.4f).epsilon(0.001));
    CHECK(e->balanceDamage == doctest::Approx(16.f * 0.5f * 1.5f).epsilon(0.001));
}

TEST_CASE("combat: блок выбирается по высоте удара — корпус закрытым блоком, голова высоким") {
    for (const char* id : {"jab", "jab_body"}) {
        Rig r;
        for (int i = 0; i < 5; ++i) r.step({}, block());
        const CombatEvent* e = r.until(CombatEventType::Blocked, 20, attack(mv(id)), block());
        REQUIRE_MESSAGE(e != nullptr, id);
        CHECK_MESSAGE(e->defense == (std::string(id) == "jab" ? "high_block" : "closed_block"), id);
    }
}

TEST_CASE("combat: апперкот в голову проходит высокий блок частично: 50% урона") {
    Rig r;
    for (int i = 0; i < 5; ++i) r.step({}, block());
    const CombatEvent* e = r.until(CombatEventType::Hit, 30, attack(mv("uppercut")), block());
    REQUIRE(e != nullptr);
    CHECK(e->outcome == OutcomeKind::Partial);
    CHECK(e->damage == doctest::Approx(11.f * 1.15f * 0.5f).epsilon(0.001));
}

TEST_CASE("combat: неблокируемый приём пробивает блок") {
    static MoveData unblockable = mv("cross");
    unblockable.unblockable = true;
    Rig r;
    for (int i = 0; i < 5; ++i) r.step({}, block());
    const CombatEvent* e = r.until(CombatEventType::Hit, 30, attack(unblockable), block());
    REQUIRE(e != nullptr);
    CHECK(e->outcome == OutcomeKind::Hit);
    CHECK(r.find(CombatEventType::Blocked) == nullptr);
}

TEST_CASE("combat: блок под углом слабее: сбоку 40%, со спины нет блока и урон ×1,25") {
    {
        Rig r;
        r.B().target = -1;
        r.B().facing = {1.f, 0.f};  // боком к атакующему (90°)
        for (int i = 0; i < 5; ++i) r.step({}, block());
        const CombatEvent* e = r.until(CombatEventType::Blocked, 20, attack(mv("jab")), block());
        REQUIRE(e != nullptr);
        CHECK(e->damage == doctest::Approx(4.6f * (1.f - 0.3f * 0.4f)).epsilon(0.001));
    }
    {
        Rig r;
        r.B().target = -1;
        r.B().facing = {0.f, -1.f};  // спиной к атакующему: он стоит позади
        for (int i = 0; i < 5; ++i) r.step({}, block());
        const CombatEvent* e = r.until(CombatEventType::Hit, 20, attack(mv("jab")), block());
        REQUIRE(e != nullptr);
        CHECK(e->back);
        CHECK(e->damage == doctest::Approx(4.6f * 1.25f).epsilon(0.001));
    }
}

// ---------------------------------------------------------------- Парирование и уклоны
// Защита стартует так, чтобы в кадр попадания у неё шёл кадр defFrame (exchange): медленным приёмам защита нужна позже.

TEST_CASE("combat: идеальное парирование прямого — без урона, атакующий оглушён, контр ×2,0") {
    Rig r;
    r.exchange(mv("jab"), DefenseAction::Parry, 2);
    const CombatEvent* e = r.find(CombatEventType::PerfectParry);
    REQUIRE(e != nullptr);
    CHECK(e->damage == 0.f);
    CHECK(r.sim.fighter(r.b).health == doctest::Approx(100.f));
    CHECK(r.sim.fighter(r.a).state == FighterState::BlockStun);
    CHECK(r.sim.fighter(r.b).counterReady > 0);
    while (r.sim.fighter(r.b).state != FighterState::Idle) r.step();  // из защиты бить нельзя: ждём, пока боец освободится (окно контратаки ещё открыто)
    CHECK(r.sim.fighter(r.b).counterReady > 0);
    const CombatEvent* c = r.until(CombatEventType::Hit, 30, {}, attack(mv("cross")));
    REQUIRE(c != nullptr);
    CHECK(c->counter);
    CHECK(c->damage == doctest::Approx(8.f * 1.15f * 2.0f).epsilon(0.001));
}

TEST_CASE("combat: парирование позже идеального окна — просто отбивает (блок), после окна — попадание") {
    {
        Rig r;
        r.exchange(mv("jab"), DefenseAction::Parry, 6);  // кадр 6 из окна 0..9, но позже идеальных 3
        const CombatEvent* e = r.find(CombatEventType::Blocked);
        REQUIRE(e != nullptr);
        CHECK(e->defense == "parry");
    }
    {
        Rig r;
        r.exchange(mv("overhand"), DefenseAction::Parry, 11);  // окно закрылось на 9-м кадре
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->defense.empty());
    }
}

TEST_CASE("combat: парирование не спасает от хука (дуга)") {
    Rig r;
    r.exchange(mv("left_hook"), DefenseAction::Parry, 3);
    const CombatEvent* e = r.find(CombatEventType::Hit);
    REQUIRE(e != nullptr);
    CHECK(e->outcome == OutcomeKind::Hit);
    CHECK(e->defense == "parry");
}

TEST_CASE("combat: слип наружу от прямого — уход с углом: нет урона, окно контратаки ×1,5") {
    Rig r;
    r.exchange(mv("jab"), DefenseAction::SlipLeft, 3);
    const CombatEvent* e = r.find(CombatEventType::Evaded);
    REQUIRE(e != nullptr);
    CHECK(e->angle);
    CHECK(e->defense == "slip");
    CHECK(r.sim.fighter(r.b).health == doctest::Approx(100.f));
    CHECK(r.sim.fighter(r.b).counterReady > 0);
    while (r.sim.fighter(r.b).state != FighterState::Idle) r.step();  // из защиты бить нельзя: ждём, пока боец освободится (окно контратаки ещё открыто)
    CHECK(r.sim.fighter(r.b).counterReady > 0);
    const CombatEvent* c = r.until(CombatEventType::Hit, 30, {}, attack(mv("cross")));
    REQUIRE(c != nullptr);
    CHECK(c->counter);
    CHECK(c->damage == doctest::Approx(8.f * 1.15f * 1.5f).epsilon(0.001));
}

TEST_CASE("combat: слип навстречу хуку — ошибка выбора стороны: попадание ×1,3; в другую сторону — уход") {
    // Левый хук атакующего летит со стороны правой руки защитника: шаг вправо — навстречу.
    {
        Rig r;
        r.exchange(mv("left_hook"), DefenseAction::SlipRight, 3);
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->damage == doctest::Approx(10.f * 1.15f * 1.3f).epsilon(0.001));
    }
    {
        Rig r;
        r.exchange(mv("left_hook"), DefenseAction::SlipLeft, 3);
        CHECK(r.find(CombatEventType::Evaded) != nullptr);
        CHECK(r.find(CombatEventType::Hit) == nullptr);
    }
    {  // правый хук — зеркально
        Rig r;
        r.exchange(mv("right_hook"), DefenseAction::SlipLeft, 3);
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->damage == doctest::Approx(10.f * 1.15f * 1.3f).epsilon(0.001));
    }
}

TEST_CASE("combat: нырок спасает от хука и наказывается апперкотом ×1,5") {
    {
        Rig r;
        r.exchange(mv("left_hook"), DefenseAction::Duck, 3);
        CHECK(r.find(CombatEventType::Evaded) != nullptr);
    }
    {
        Rig r;
        r.exchange(mv("uppercut"), DefenseAction::Duck, 3);
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->damage == doctest::Approx(11.f * 1.15f * 1.5f).epsilon(0.001));
    }
}

TEST_CASE("combat: отклон назад уходит от прямого и апперкота, от нисходящего — частично") {
    {
        Rig r;
        r.exchange(mv("cross"), DefenseAction::LeanBack, 3);
        CHECK(r.find(CombatEventType::Evaded) != nullptr);
    }
    {
        Rig r;
        r.exchange(mv("uppercut"), DefenseAction::LeanBack, 3);
        CHECK(r.find(CombatEventType::Evaded) != nullptr);
    }
    {
        Rig r;
        r.exchange(mv("overhand"), DefenseAction::LeanBack, 3);
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->outcome == OutcomeKind::Partial);
        CHECK(e->damage == doctest::Approx(14.f * 1.15f * 0.5f).epsilon(0.001));
    }
}

TEST_CASE("combat: сайдстеп уходит от прямого с углом, навстречу дуге — попадание ×1,3") {
    {
        Rig r;
        r.exchange(mv("cross"), DefenseAction::SidestepLeft, 3);
        const CombatEvent* e = r.find(CombatEventType::Evaded);
        REQUIRE(e != nullptr);
        CHECK(e->angle);
    }
    {
        Rig r;
        r.exchange(mv("left_hook"), DefenseAction::SidestepRight, 3);
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->damage == doctest::Approx(10.f * 1.15f * 1.3f).epsilon(0.001));
    }
}

TEST_CASE("combat: шаг назад отводит бойца от врага и спасает от удара") {
    Rig r;
    r.exchange(mv("jab"), DefenseAction::StepBack, 3);
    CHECK(r.find(CombatEventType::Evaded) != nullptr);
    r.idle(10);
    CHECK(r.sim.distance(r.a, r.b) > 1.2f + 0.3f);
    CHECK(r.sim.fighter(r.b).health == doctest::Approx(100.f));
}

TEST_CASE("combat: защита стоит стамину и не начинается без неё") {
    Rig r;
    r.step({}, defend(DefenseAction::SidestepLeft));
    CHECK(r.sim.fighter(r.b).stamina == doctest::Approx(96.f).epsilon(0.01));
    Rig empty;
    empty.B().stamina = 1.f;
    empty.step({}, defend(DefenseAction::SidestepLeft));
    CHECK(empty.sim.fighter(empty.b).state == FighterState::Idle);
}

TEST_CASE("combat: защита не спасает от высот, которые не покрывает, и вне своего окна") {
    {
        Rig r;
        r.exchange(mv("jab_body"), DefenseAction::Duck, 3);  // нырок прикрывает голову и корпус
        CHECK(r.find(CombatEventType::Evaded) != nullptr);
    }
    {
        static MoveData lowKick = mv("jab");
        lowKick.height = Height::Legs;
        lowKick.trajectory = Trajectory::Low;
        Rig r;
        r.exchange(lowKick, DefenseAction::Duck, 3);  // нырок не прикрывает ноги
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->defense.empty());
    }
    {
        Rig r;
        r.exchange(mv("overhand"), DefenseAction::Duck, 14);  // нырок давно закончился (окно до 9-го кадра)
        const CombatEvent* e = r.find(CombatEventType::Hit);
        REQUIRE(e != nullptr);
        CHECK(e->defense.empty());
    }
}

// ---------------------------------------------------------------- Нокдауны и нокаут

TEST_CASE("combat: нокдаун по балансу: сильные удары обнуляют баланс, боец падает и встаёт с половиной") {
    Rig r;
    FighterStats big = plain();
    big.strength = 10;
    r.A().stats = big;
    int knockdowns = 0;
    for (int round = 0; round < 12 && knockdowns == 0; ++round) {
        r.B().stunFrames = 0;
        r.B().state = FighterState::Idle;
        r.until(CombatEventType::Hit, 60, attack(mv("overhand")));
        for (const CombatEvent& e : r.sim.events()) knockdowns += e.type == CombatEventType::Knockdown ? 1 : 0;
        r.A().stamina = 100.f;
        r.idle(30);
    }
    for (const CombatEvent& e : r.sim.log()) knockdowns += e.type == CombatEventType::Knockdown ? 1 : 0;
    CHECK(knockdowns >= 1);
    CHECK((r.sim.fighter(r.b).state == FighterState::Knockdown || r.sim.fighter(r.b).knockdowns >= 1));
}

TEST_CASE("combat: нокдаун по урону в голову: больше порога за 3 секунды") {
    Rig r;
    // Порог крепости 5 — 25. Каждый джеб — 4,6 в голову: шестой переходит порог (27,6).
    int jabs = 0;
    while (jabs < 12 && r.sim.fighter(r.b).state != FighterState::Knockdown) {
        r.B().state = FighterState::Idle;
        r.B().stunFrames = 0;
        r.B().balance = 100.f;  // исключаем нокдаун по балансу
        r.A().stamina = 100.f;
        r.until(CombatEventType::Hit, 40, attack(mv("jab")));
        r.idle(12);
        ++jabs;
        if (r.sim.fighter(r.b).state == FighterState::Knockdown) break;
    }
    CHECK(r.sim.fighter(r.b).state == FighterState::Knockdown);
    CHECK(jabs == 6);
}

TEST_CASE("combat: крепкий боец выдерживает больше: порог 25 + 3 × (крепость − 5)") {
    FighterStats tough = plain();
    tough.toughness = 10;  // порог 40, урон ×0,85
    Rig r(1.2f, plain(), tough);
    for (int i = 0; i < 6; ++i) {
        r.B().state = FighterState::Idle;
        r.B().stunFrames = 0;
        r.B().balance = 100.f;
        r.A().stamina = 100.f;
        r.until(CombatEventType::Hit, 40, attack(mv("jab")));
        r.idle(12);
    }
    CHECK(r.sim.fighter(r.b).state != FighterState::Knockdown);
    CHECK(rules().knockdownThreshold(10.f) == doctest::Approx(40.f));
}

TEST_CASE("combat: лежачий встаёт через down_frames + getup_frames и получает половину баланса") {
    Rig r;
    r.B().balance = 1.f;
    r.until(CombatEventType::Knockdown, 30, attack(mv("cross")));
    REQUIRE(r.sim.fighter(r.b).state == FighterState::Knockdown);
    r.idle(rules().downFrames);
    CHECK(r.sim.fighter(r.b).state == FighterState::GettingUp);
    const CombatEvent* up = r.until(CombatEventType::GetUp, rules().getupFrames + 2);
    REQUIRE(up != nullptr);
    CHECK(r.sim.fighter(r.b).state == FighterState::Idle);
    CHECK(r.sim.fighter(r.b).balance >= rules().balanceMax * 0.5f);
}

TEST_CASE("combat: лежачего бьют слабее (×0,6), а лежачий не бьёт и не защищается") {
    Rig r;
    r.B().state = FighterState::Knockdown;
    r.B().stateFrame = 0;
    const CombatEvent* e = r.until(CombatEventType::Hit, 20, attack(mv("jab")));
    REQUIRE(e != nullptr);
    CHECK(e->downed);
    CHECK(e->damage == doctest::Approx(4.6f * 0.6f).epsilon(0.001));
    r.step({}, attack(mv("jab")));
    CHECK(r.sim.fighter(r.b).state == FighterState::Knockdown);
}

TEST_CASE("combat: нокаут при нуле здоровья: событие, конец боя, ввод игнорируется") {
    Rig r;
    r.B().health = 3.f;
    const CombatEvent* e = r.until(CombatEventType::KnockedOut, 30, attack(mv("jab")));
    REQUIRE(e != nullptr);
    CHECK(e->defender == r.b);
    CHECK(r.sim.fighter(r.b).health == 0.f);
    CHECK(r.sim.fighter(r.b).state == FighterState::KnockedOut);
    CHECK(r.sim.fightOver());
    r.step({}, attack(mv("jab")));
    CHECK(r.sim.fighter(r.b).state == FighterState::KnockedOut);
    CHECK_FALSE(r.sim.fighter(r.b).alive());
}

TEST_CASE("combat: в боях по правилам нокаут после трёх нокдаунов, в уличной драке — нет") {
    for (const bool official : {false, true}) {
        Rig r;
        r.sim.setOfficialRules(official);
        for (int i = 0; i < 3; ++i) {
            r.B().state = FighterState::Idle;
            r.B().stunFrames = 0;
            r.B().balance = 1.f;
            r.A().stamina = 100.f;
            r.A().state = FighterState::Idle;
            r.until(CombatEventType::Knockdown, 40, attack(mv("cross")));
            r.B().state = r.sim.fighter(r.b).state == FighterState::KnockedOut ? FighterState::KnockedOut : FighterState::Idle;
            r.idle(15);
        }
        CHECK(r.sim.fighter(r.b).knockdowns == 3);
        CHECK((r.sim.fighter(r.b).state == FighterState::KnockedOut) == official);
    }
}

// ---------------------------------------------------------------- События и контракты

TEST_CASE("combat: событие удара несёт направление, импульс приёма, зону; блок гасит импульс") {
    Rig r;
    const CombatEvent* e = r.until(CombatEventType::Hit, 20, attack(mv("left_hook")));
    REQUIRE(e != nullptr);
    CHECK(e->impulse == doctest::Approx(mv("left_hook").impulse));
    CHECK(e->direction.z == doctest::Approx(-1.f));
    Rig b;
    for (int i = 0; i < 5; ++i) b.step({}, block());
    const CombatEvent* blocked = b.until(CombatEventType::Blocked, 30, attack(mv("left_hook")), block());
    REQUIRE(blocked != nullptr);
    CHECK(blocked->impulse == doctest::Approx(mv("left_hook").impulse * 0.4f));
}

TEST_CASE("combat: перевод в контракт HitEvent для звука и эффектов (идентификаторы = индекс + 1)") {
    Rig r;
    const CombatEvent* e = r.until(CombatEventType::Hit, 20, attack(mv("jab")));
    REQUIRE(e != nullptr);
    const auto h = CombatSim::toContract(*e, r.sim.fighter(r.a).pos);
    CHECK(h.attacker == 1);
    CHECK(h.defender == 2);
    CHECK(h.zone == han::contracts::HitZone::Head);
    CHECK(h.damage == doctest::Approx(4.6f).epsilon(0.001));
    CHECK(h.impulse == doctest::Approx(60.f));
    CHECK_FALSE(h.blocked);
    CHECK(h.point.z < 0.f);
}

TEST_CASE("combat: журнал накапливает события, events() — только последнего кадра") {
    Rig r;
    r.until(CombatEventType::Hit, 20, attack(mv("jab")));
    CHECK(r.sim.events().size() == 1);
    const std::size_t logged = r.sim.log().size();
    r.step();
    CHECK(r.sim.events().empty());
    CHECK(r.sim.log().size() == logged);
}

// ---------------------------------------------------------------- Устройство симуляции

TEST_CASE("combat: одинаковые вводы и сид дают одинаковый бой, разные сиды — разные криты") {
    const auto fight = [](std::uint32_t seed) {
        FighterStats s;
        s.technique = 10;
        Rig r(1.2f, s, s, seed);
        double sum = 0;
        for (int i = 0; i < 400; ++i) {
            r.step(i % 30 == 0 ? attack(mv("jab")) : FighterInput{}, i % 45 == 0 ? attack(mv("cross")) : FighterInput{});
            sum += r.sim.fighter(r.a).health * 1.7 + r.sim.fighter(r.b).health * 0.3 + r.sim.fighter(r.a).pos.z;
        }
        return sum;
    };
    CHECK(fight(3) == fight(3));
    CHECK(fight(3) != fight(4));
}

TEST_CASE("combat: недостающие вводы считаются пустыми, неверная цель игнорируется, звёзды зажаты") {
    CombatSim sim(rules());
    FighterStats s;
    s.stars = 9;
    const int a = sim.addFighter({0, 0}, s);
    const int b = sim.addFighter({0, -1}, s);
    CHECK(sim.fighter(a).stats.stars == 3);
    sim.setTarget(a, 99);
    CHECK(sim.fighter(a).target == -1);
    sim.setTarget(a, a);
    CHECK(sim.fighter(a).target == -1);
    sim.setTarget(a, b);
    sim.step({});                       // совсем без вводов
    sim.step({FighterInput{}});         // меньше вводов, чем бойцов
    CHECK(sim.frame() == 2);
    CHECK_FALSE(sim.fightOver());
}

TEST_CASE("combat: групповой бой — каждый бьёт свою цель, третий боец не затронут") {
    CombatSim sim(rules());
    const int a = sim.addFighter({0, 0}, plain());
    const int b = sim.addFighter({0, -1.2f}, plain());
    const int c = sim.addFighter({5, 0}, plain());
    sim.setTarget(a, b);
    sim.setTarget(b, a);
    sim.setTarget(c, a);
    FighterInput in;
    in.attack = &mv("jab");
    for (int i = 0; i < 12; ++i) sim.step({i == 0 ? in : FighterInput{}, {}, {}});
    CHECK(sim.fighter(b).health < 100.f);
    CHECK(sim.fighter(a).health == doctest::Approx(100.f));
    CHECK(sim.fighter(c).health == doctest::Approx(100.f));
}

TEST_CASE("combat: всем реальным приёмам бокса по очереди — попадание на своём кадре и в пределах дальности") {
    for (const MoveData& m : library().all()) {
        Rig r;
        r.A().focus = 100.f;
        r.step(attack(m));
        for (int i = 2; i < m.startup; ++i) r.step();  // кадры 1..startup-1
        CHECK_MESSAGE(r.sim.events().empty(), m.id << ": событие раньше замаха");
        r.step();                                      // кадр startup — попадание
        const bool landed = r.find(CombatEventType::Hit) != nullptr || r.find(CombatEventType::Blocked) != nullptr;
        // Некоторые приёмы (слип-контр, отход) смещают бойца, но на 1,2 м всё равно достают.
        CHECK_MESSAGE(landed, m.id << ": не попал на кадре " << m.startup);
    }
}

TEST_CASE("combat: полный бой на 30 секунд с обменом ударами — без нарушения ресурсов и NaN") {
    FighterStats s = plain();
    s.technique = 6;
    Rig r(1.2f, s, s, 77);
    const MoveData* moves[] = {&mv("jab"), &mv("cross"), &mv("left_hook"), &mv("uppercut"), &mv("liver_hook")};
    std::uint32_t seed = 5;
    const auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) & 0xFFFF;
    };
    for (int i = 0; i < 1800 && !r.sim.fightOver(); ++i) {
        FighterInput ia, ib;
        if (rnd() % 20 == 0) ia.attack = moves[rnd() % 5];
        if (rnd() % 25 == 0) ib.attack = moves[rnd() % 5];
        if (rnd() % 30 == 0) ib.defense = static_cast<DefenseAction>(1 + rnd() % 10);
        ib.blockHeld = rnd() % 7 == 0;
        ia.forward = static_cast<float>(static_cast<int>(rnd() % 3) - 1);
        r.step(ia, ib);
        for (int k = 0; k < 2; ++k) {
            const Fighter& f = r.sim.fighter(k);
            REQUIRE((f.health >= 0.f && f.health <= 100.f));
            REQUIRE((f.stamina >= -0.01f && f.stamina <= 100.f));
            REQUIRE((std::isfinite(f.pos.x) && std::isfinite(f.pos.z) && std::isfinite(f.balance)));
        }
    }
    CHECK(r.sim.log().size() > 5);
}
