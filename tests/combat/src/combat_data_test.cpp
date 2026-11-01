// Тесты данных боя (G1): разбор приёмов и правил, ошибки данных, соответствие реальных файлов data/combat часть 25 дизайна.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "han/combat/move_data.h"
#include "han/combat/rules.h"

using namespace han::combat;
using han::core::Json;

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }

Json moveJson(const std::string& extra = "") {
    Json j;
    std::string error;
    const std::string text = R"({"id":"t_jab","style":"boxing","name_key":"move.boxing.t","unlock_level":1,"type":"jab","trajectory":"straight","limb":"left_hand",
        "height":"head","frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],"range":1.3,"damage":4,"stamina":3,"balance":4)" + extra + "}";
    REQUIRE_MESSAGE(han::core::parseJson(text, j, error), error);
    return j;
}
}  // namespace

TEST_CASE("move: корректный приём разбирается полностью") {
    MoveData m;
    std::string error;
    REQUIRE_MESSAGE(parseMove(moveJson(R"(,"on_block_frames":-1,"on_hit_frames":3,"power":true,"impulse":60,"fx":{"hitstop_ms":45,"shake":0.2,"sound":"s"},"movement_variant":"step_in")"), m, error), error);
    CHECK(m.id == "t_jab");
    CHECK(m.startup == 6);
    CHECK(m.active == 2);
    CHECK(m.recovery == 10);
    CHECK(m.totalFrames() == 18);
    CHECK(m.cancelFrom == 8);
    CHECK(m.cancelTo == 13);
    CHECK(m.trajectory == Trajectory::Straight);
    CHECK(m.height == Height::Head);
    CHECK(m.movement == Movement::StepIn);
    CHECK(m.onBlockFrames == -1);
    CHECK(m.onHitFrames == 3);
    CHECK(m.power);
    CHECK(m.hitstopMs == 45);
    CHECK(m.sound == "s");
}

TEST_CASE("move: активные кадры — [startup, startup + active)") {
    MoveData m;
    std::string error;
    REQUIRE(parseMove(moveJson(), m, error));
    CHECK_FALSE(m.inActiveFrames(5));
    CHECK(m.inActiveFrames(6));
    CHECK(m.inActiveFrames(7));
    CHECK_FALSE(m.inActiveFrames(8));
}

TEST_CASE("move: ошибки данных понятны и называют поле") {
    const auto bad = [](const std::string& extraOrReplace, const char* expected) {
        Json j;
        std::string error;
        const std::string text = extraOrReplace;
        REQUIRE(han::core::parseJson(text, j, error));
        MoveData m;
        CHECK_FALSE(parseMove(j, m, error));
        CHECK_MESSAGE(error.find(expected) != std::string::npos, text << " -> " << error);
    };
    const std::string good = R"("style":"boxing","name_key":"move.b.t","unlock_level":1,"type":"jab","trajectory":"straight","limb":"left_hand","height":"head",)";
    const std::string rest = R"("range":1.3,"damage":4,"stamina":3,"balance":4)";
    bad("[]", "объект");
    bad(R"({"id":"A","style":"boxing"})", "id");
    bad(R"({"id":"ok_id",)" + good + R"("frames":{"startup":0,"active":2,"recovery":10},"cancel_window":[8,13],)" + rest + "}", "frames");
    bad(R"({"id":"ok_id",)" + good + R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[13,8],)" + rest + "}", "cancel_window");
    bad(R"({"id":"ok_id",)" + good + R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[2,8],)" + rest + "}", "внутри приёма");
    bad(R"({"id":"ok_id",)" + good + R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,99],)" + rest + "}", "внутри приёма");
    bad(R"({"id":"ok_id",)" + good + R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],"range":9,"damage":4,"stamina":3,"balance":4})", "range");
    bad(R"({"id":"ok_id",)" + good + R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],"range":1,"damage":-4,"stamina":3,"balance":4})", "damage");
    bad(R"({"id":"ok_id",)" + good + R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],)" + rest + R"(,"bonus":1})", "лишнее");
    bad(R"({"id":"ok_id","style":"boxing","name_key":"x.y","unlock_level":1,"type":"jab","trajectory":"straight","limb":"l","height":"head",)" + std::string(R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],)") + rest + "}", "move.");
    bad(R"({"id":"ok_id","style":"boxing","name_key":"move.x","unlock_level":1,"type":"jab","trajectory":"zigzag","limb":"l","height":"head",)" + std::string(R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],)") + rest + "}", "траектория");
    bad(R"({"id":"ok_id","style":"boxing","name_key":"move.x","unlock_level":1,"type":"jab","trajectory":"straight","limb":"l","height":"neck",)" + std::string(R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],)") + rest + "}", "высота");
    bad(R"({"id":"ok_id","style":"boxing","name_key":"move.x","unlock_level":11,"type":"jab","trajectory":"straight","limb":"l","height":"head",)" + std::string(R"("frames":{"startup":6,"active":2,"recovery":10},"cancel_window":[8,13],)") + rest + "}", "unlock_level");
}

TEST_CASE("move: библиотека не принимает повтор id; названия перечислений круговые") {
    MoveLibrary lib;
    MoveData m;
    std::string error;
    REQUIRE(parseMove(moveJson(), m, error));
    CHECK(lib.add(m, error));
    CHECK_FALSE(lib.add(m, error));
    CHECK(error.find("повторяющийся") != std::string::npos);
    CHECK(lib.size() == 1);
    CHECK(lib.byId("t_jab") != nullptr);
    CHECK(lib.byId("нет") == nullptr);
    for (int i = 0; i < 8; ++i) {
        Trajectory t = Trajectory::Straight;
        REQUIRE(trajectoryFromName(trajectoryName(static_cast<Trajectory>(i)), t));
        CHECK(static_cast<int>(t) == i);
    }
    for (int i = 0; i < 3; ++i) {
        Height h = Height::Head;
        REQUIRE(heightFromName(heightName(static_cast<Height>(i)), h));
        CHECK(static_cast<int>(h) == i);
    }
    for (int i = 0; i < 7; ++i) {
        Movement mv = Movement::Static;
        REQUIRE(movementFromName(movementName(static_cast<Movement>(i)), mv));
        CHECK(static_cast<int>(mv) == i);
    }
}

TEST_CASE("move: реальные приёмы бокса — 18 штук, значения по части 25 дизайна") {
    MoveLibrary lib;
    std::string error;
    REQUIRE_MESSAGE(lib.loadDirectory(repo("data/combat/moves"), error), error);
    CHECK(lib.size() >= 15);
    const MoveData* jab = lib.byId("boxing_jab");
    const MoveData* hook = lib.byId("boxing_left_hook");
    const MoveData* up = lib.byId("boxing_uppercut");
    const MoveData* power = lib.byId("boxing_overhand");
    REQUIRE((jab && hook && up && power));
    // Кадры с/а/в, урон, стамина, баланс — таблица 25.2.
    CHECK((jab->startup == 6 && jab->active == 2 && jab->recovery == 10 && jab->damage == 4.f && jab->stamina == 3.f && jab->balance == 4.f));
    CHECK((hook->startup == 11 && hook->active == 3 && hook->recovery == 16 && hook->damage == 10.f && hook->stamina == 6.f && hook->balance == 10.f));
    CHECK((up->startup == 12 && up->damage == 11.f && up->balance == 12.f));
    CHECK((power->startup == 16 && power->damage == 14.f && power->stamina == 9.f));
    CHECK(hook->trajectory == Trajectory::ArcLeft);
    CHECK(up->trajectory == Trajectory::Up);
    CHECK(power->trajectory == Trajectory::Down);
    CHECK(lib.byId("boxing_liver_hook")->height == Height::Torso);
    CHECK(lib.byId("boxing_dempsey_roll")->focus > 0.f);
}

TEST_CASE("move: у каждого реального приёма разумные кадры и порядок «лёгкий быстрее тяжёлого»") {
    MoveLibrary lib;
    std::string error;
    REQUIRE(lib.loadDirectory(repo("data/combat/moves"), error));
    std::set<std::string> ids;
    for (const MoveData& m : lib.all()) {
        CHECK(ids.insert(m.id).second);
        CHECK(m.id.rfind("boxing_", 0) == 0);
        CHECK(m.startup >= 4);
        CHECK(m.startup <= 18);
        CHECK(m.cancelFrom >= m.startup);
        CHECK(m.cancelTo <= m.totalFrames());
        CHECK(m.range > 0.5f);
        CHECK(m.range < 2.f);
        CHECK(m.impulse > 0.f);
        CHECK_MESSAGE(!m.sound.empty(), m.id);
    }
    CHECK(lib.byId("boxing_jab")->startup < lib.byId("boxing_cross")->startup);
    CHECK(lib.byId("boxing_cross")->startup < lib.byId("boxing_overhand")->startup);
    CHECK(lib.byId("boxing_jab")->damage < lib.byId("boxing_overhand")->damage);
}

TEST_CASE("move: папка с ошибками — остальные файлы грузятся, ошибки перечислены") {
    const auto dir = std::filesystem::temp_directory_path() / "han_combat_moves";
    std::filesystem::create_directories(dir);
    for (const auto& e : std::filesystem::directory_iterator(dir)) std::filesystem::remove(e.path());
    Json good = moveJson();
    {
        std::ofstream(dir / "good.json") << good.dump();
        std::ofstream(dir / "broken.json") << "{ не json";
        std::ofstream(dir / "readme.txt") << "игнорируется";
    }
    MoveLibrary lib;
    std::string error;
    CHECK_FALSE(lib.loadDirectory(dir.string(), error));
    CHECK(error.find("broken.json") != std::string::npos);
    CHECK(lib.size() == 1);
    CHECK_FALSE(lib.loadDirectory("нет_папки", error));
}

// ---------------------------------------------------------------- Правила

TEST_CASE("rules: файл правил боя читается, значения по части 25.1") {
    CombatRules r;
    std::string error;
    REQUIRE_MESSAGE(r.load(repo("data/combat/combat_rules.json"), error), error);
    CHECK(r.fps == 60);
    CHECK(r.zoneHead == doctest::Approx(1.15f));
    CHECK(r.zoneLegs == doctest::Approx(0.85f));
    CHECK(r.counter == doctest::Approx(1.5f));
    CHECK(r.perfectParryCounter == doctest::Approx(2.0f));
    CHECK(r.blockTaken == doctest::Approx(0.7f));
    CHECK(r.blockTakenPower == doctest::Approx(0.4f));
    CHECK(r.star[2] == doctest::Approx(1.2f));
    CHECK(r.staminaRegen == doctest::Approx(18.f));
    CHECK(r.balanceRegen == doctest::Approx(12.f));
    CHECK(r.knockdownThreshold(5.f) == doctest::Approx(25.f));
    CHECK(r.knockdownThreshold(8.f) == doctest::Approx(34.f));
    CHECK(r.defenses.size() >= 9);
}

TEST_CASE("rules: углы — доля блока по таблице 22.3") {
    CombatRules r;
    std::string error;
    REQUIRE(r.load(repo("data/combat/combat_rules.json"), error));
    CHECK(r.blockFactorForAngle(0.f) == doctest::Approx(1.f));
    CHECK(r.blockFactorForAngle(30.f) == doctest::Approx(1.f));
    CHECK(r.blockFactorForAngle(45.f) == doctest::Approx(0.7f));
    CHECK(r.blockFactorForAngle(90.f) == doctest::Approx(0.4f));
    CHECK(r.blockFactorForAngle(150.f) == doctest::Approx(0.f));
    CHECK(r.blockFactorForAngle(-45.f) == doctest::Approx(0.7f));
    CHECK(r.blockFactorForAngle(179.f) == doctest::Approx(0.f));
}

TEST_CASE("rules: исходы разбираются из строк") {
    Outcome o;
    REQUIRE(parseOutcome("hit", o));
    CHECK((o.kind == OutcomeKind::Hit && o.multiplier == 1.f));
    REQUIRE(parseOutcome("hit*1.3", o));
    CHECK((o.kind == OutcomeKind::Hit && o.multiplier == doctest::Approx(1.3f)));
    REQUIRE(parseOutcome("evade_angle", o));
    CHECK((o.kind == OutcomeKind::Evade && o.angle));
    REQUIRE(parseOutcome("block", o));
    CHECK(o.kind == OutcomeKind::Block);
    REQUIRE(parseOutcome("partial", o));
    CHECK(o.kind == OutcomeKind::Partial);
    REQUIRE(parseOutcome("perfect", o));
    CHECK(o.kind == OutcomeKind::Perfect);
    CHECK_FALSE(parseOutcome("explode", o));
    CHECK_FALSE(parseOutcome("hit*", o));
    CHECK_FALSE(parseOutcome("hit*abc", o));
    CHECK_FALSE(parseOutcome("hit*9", o));
    CHECK_FALSE(parseOutcome("block*1.5", o));  // множитель только у попадания
}

TEST_CASE("rules: колонка таблицы зависит от траектории и стороны шага (часть 22.1, 22.6)") {
    CHECK(classifyTrajectory(Trajectory::Straight, 0) == TrajClass::Straight);
    CHECK(classifyTrajectory(Trajectory::Push, 1) == TrajClass::Straight);
    CHECK(classifyTrajectory(Trajectory::Up, 0) == TrajClass::Up);
    CHECK(classifyTrajectory(Trajectory::Down, 0) == TrajClass::Down);
    CHECK(classifyTrajectory(Trajectory::Low, 0) == TrajClass::Low);
    CHECK(classifyTrajectory(Trajectory::Spin, 1) == TrajClass::ArcSame);
    // Левый хук атакующего летит со стороны правой руки защитника: шаг вправо — навстречу удару («с той же стороны»).
    CHECK(classifyTrajectory(Trajectory::ArcLeft, +1) == TrajClass::ArcSame);
    CHECK(classifyTrajectory(Trajectory::ArcLeft, -1) == TrajClass::ArcOther);
    CHECK(classifyTrajectory(Trajectory::ArcRight, -1) == TrajClass::ArcSame);
    CHECK(classifyTrajectory(Trajectory::ArcRight, +1) == TrajClass::ArcOther);
    CHECK(classifyTrajectory(Trajectory::ArcLeft, 0) == TrajClass::ArcSame);
}

TEST_CASE("rules: таблица исходов 22.6 из данных — все клетки основных защит") {
    CombatRules r;
    std::string error;
    REQUIRE(r.load(repo("data/combat/combat_rules.json"), error));
    struct Cell {
        const char* defense;
        TrajClass col;
        OutcomeKind kind;
        float mult;
        bool angle;
    };
    const Cell cells[] = {
        // Высокий блок: прямой, дуги, сверху — блок; снизу — частично; низовой — попадание.
        {"high_block", TrajClass::Straight, OutcomeKind::Block, 1.f, false}, {"high_block", TrajClass::ArcSame, OutcomeKind::Block, 1.f, false},
        {"high_block", TrajClass::ArcOther, OutcomeKind::Block, 1.f, false}, {"high_block", TrajClass::Up, OutcomeKind::Partial, 1.f, false},
        {"high_block", TrajClass::Down, OutcomeKind::Block, 1.f, false}, {"high_block", TrajClass::Low, OutcomeKind::Hit, 1.f, false},
        // Парирование.
        {"parry", TrajClass::Straight, OutcomeKind::Perfect, 1.f, false}, {"parry", TrajClass::ArcSame, OutcomeKind::Hit, 1.f, false},
        {"parry", TrajClass::ArcOther, OutcomeKind::Hit, 1.f, false}, {"parry", TrajClass::Up, OutcomeKind::Hit, 1.f, false},
        {"parry", TrajClass::Down, OutcomeKind::Partial, 1.f, false}, {"parry", TrajClass::Low, OutcomeKind::Hit, 1.f, false},
        // Слип наружу.
        {"slip", TrajClass::Straight, OutcomeKind::Evade, 1.f, true}, {"slip", TrajClass::ArcSame, OutcomeKind::Hit, 1.3f, false},
        {"slip", TrajClass::ArcOther, OutcomeKind::Evade, 1.f, false}, {"slip", TrajClass::Up, OutcomeKind::Evade, 1.f, false},
        {"slip", TrajClass::Down, OutcomeKind::Evade, 1.f, false}, {"slip", TrajClass::Low, OutcomeKind::Hit, 1.f, false},
        // Нырок.
        {"duck", TrajClass::Straight, OutcomeKind::Evade, 1.f, false}, {"duck", TrajClass::ArcSame, OutcomeKind::Evade, 1.f, false},
        {"duck", TrajClass::ArcOther, OutcomeKind::Evade, 1.f, false}, {"duck", TrajClass::Up, OutcomeKind::Hit, 1.5f, false},
        {"duck", TrajClass::Down, OutcomeKind::Hit, 1.f, false}, {"duck", TrajClass::Low, OutcomeKind::Hit, 1.f, false},
        // Отклон назад.
        {"lean_back", TrajClass::Straight, OutcomeKind::Evade, 1.f, false}, {"lean_back", TrajClass::Up, OutcomeKind::Evade, 1.f, false},
        {"lean_back", TrajClass::Down, OutcomeKind::Partial, 1.f, false}, {"lean_back", TrajClass::Low, OutcomeKind::Hit, 1.f, false},
        // Сайдстеп.
        {"sidestep", TrajClass::Straight, OutcomeKind::Evade, 1.f, true}, {"sidestep", TrajClass::ArcSame, OutcomeKind::Hit, 1.3f, false},
        {"sidestep", TrajClass::ArcOther, OutcomeKind::Evade, 1.f, false}, {"sidestep", TrajClass::Low, OutcomeKind::Evade, 1.f, false},
        // Шаг назад.
        {"step_back", TrajClass::Straight, OutcomeKind::Evade, 1.f, false}, {"step_back", TrajClass::Low, OutcomeKind::Partial, 1.f, false},
        // Чек: блокирует только низовой.
        {"check", TrajClass::Straight, OutcomeKind::Hit, 1.f, false}, {"check", TrajClass::Up, OutcomeKind::Hit, 1.f, false},
        {"check", TrajClass::Low, OutcomeKind::Block, 1.f, false},
    };
    for (const Cell& c : cells) {
        const DefenseDef* d = r.defense(c.defense);
        REQUIRE_MESSAGE(d != nullptr, c.defense);
        const Outcome& o = d->outcome(c.col);
        CHECK_MESSAGE(o.kind == c.kind, c.defense << " колонка " << static_cast<int>(c.col));
        CHECK_MESSAGE(o.multiplier == doctest::Approx(c.mult), c.defense << " колонка " << static_cast<int>(c.col));
        CHECK_MESSAGE(o.angle == c.angle, c.defense << " колонка " << static_cast<int>(c.col));
    }
}

TEST_CASE("rules: охват защит — кто от каких высот защищает") {
    CombatRules r;
    std::string error;
    REQUIRE(r.load(repo("data/combat/combat_rules.json"), error));
    CHECK(r.defense("high_block")->covered(Height::Head));
    CHECK_FALSE(r.defense("high_block")->covered(Height::Torso));
    CHECK(r.defense("closed_block")->covered(Height::Torso));
    CHECK_FALSE(r.defense("closed_block")->covered(Height::Head));
    CHECK(r.defense("check")->covered(Height::Legs));
    CHECK_FALSE(r.defense("check")->covered(Height::Head));
    CHECK(r.defense("sidestep")->covered(Height::Legs));
    CHECK_FALSE(r.defense("slip")->covered(Height::Legs));
    CHECK(r.defense("высокий") == nullptr);
    CHECK(r.defense("parry")->perfectUntil < r.defense("parry")->windowTo);  // идеальное окно уже общего
    CHECK(r.defense("slip")->sided);
    CHECK_FALSE(r.defense("duck")->sided);
}

TEST_CASE("rules: ошибки в файле правил называют место") {
    const auto bad = [](const char* text, const char* expected) {
        Json j;
        std::string error;
        REQUIRE(han::core::parseJson(text, j, error));
        CombatRules r;
        CHECK_FALSE(r.parse(j, error));
        CHECK_MESSAGE(error.find(expected) != std::string::npos, text << " -> " << error);
    };
    bad("[]", "объект");
    bad(R"({"damage":{"counter":99}})", "counter");
    bad(R"({"damage":{"star":[1,2]}})", "star");
    bad(R"({"block_by_angle":[]})", "block_by_angle");
    bad(R"({"block_by_angle":[{"max_degrees":90,"factor":1},{"max_degrees":60,"factor":0.5}],"defenses":{}})", "по возрастанию");
    bad(R"({"block_by_angle":[{"max_degrees":180,"factor":1}],"defenses":{}})", "defenses");
    bad(R"({"block_by_angle":[{"max_degrees":180,"factor":1}],"defenses":{"x":{"covers":["neck"]}}})", "высота");
    bad(R"({"block_by_angle":[{"max_degrees":180,"factor":1}],"defenses":{"x":{"covers":["head"],"hold":true}}})", "нет исхода");
    bad(R"({"block_by_angle":[{"max_degrees":180,"factor":1}],"defenses":{"x":{"covers":["head"],"total":0}}})", "total");
    bad(R"({"movement":{"walk_speed":-1},"block_by_angle":[{"max_degrees":180,"factor":1}]})", "walk_speed");
    CombatRules r;
    std::string error;
    CHECK_FALSE(r.load("нет_такого_файла.json", error));
}
