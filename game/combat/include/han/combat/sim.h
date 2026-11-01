// han/combat/sim.h — ядро боя (G1): кадровая машина бойцов, буфер ввода, попадания, защита, урон, ресурсы, нокдауны.
// Чистая логика без графики и физики: мир приходит через позиции бойцов, наружу уходят события (CombatEvent и contracts::HitEvent).
// Один вызов step() — один кадр при 60 Гц. Все числа — из CombatRules и MoveData (данные в data/combat).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "contracts/combat_events.h"
#include "han/combat/move_data.h"
#include "han/combat/rules.h"

namespace han::combat {
struct Vec2 {
    float x = 0, z = 0;
};

enum class FighterState : unsigned char { Idle, Moving, Attacking, Defending, Blocking, HitStun, BlockStun, Knockdown, GettingUp, KnockedOut };
enum class DefenseAction : unsigned char { None, Parry, SlipLeft, SlipRight, Duck, BobLeft, BobRight, LeanBack, SidestepLeft, SidestepRight, StepBack, ShoulderRollLeft, ShoulderRollRight };

struct FighterStats {
    float strength = 5, endurance = 5, technique = 5, toughness = 5;
    int stars = 1;  // 1..3
    bool headMovement = false;  // боксёрская механика «движение головой»: уклоны копят заряд для следующего удара
};

// Ввод бойца на один кадр. forward/right — относительно врага (вперёд = к цели), −1..1.
struct FighterInput {
    float forward = 0, right = 0;
    bool dash = false;
    const MoveData* attack = nullptr;       // запрос приёма (попадает в буфер ввода)
    DefenseAction defense = DefenseAction::None;
    bool blockHeld = false;
};

struct Fighter {
    int id = 0;
    Vec2 pos, facing{0.f, -1.f};
    FighterStats stats;
    float health = 100, stamina = 100, balance = 100, focus = 100;
    FighterState state = FighterState::Idle;
    int stateFrame = 0;          // кадров в текущем состоянии
    int target = -1;             // индекс врага; бойцу лицом к нему
    // Атака
    const MoveData* move = nullptr;
    Movement moveMovement = Movement::Static;
    bool moveResolved = false;   // приём уже попал / был заблокирован / не достал на активных кадрах
    int moveSide = 1;            // сторона бокового шага атаки
    // Защита
    DefenseAction action = DefenseAction::None;
    int blockFrames = 0;
    // Оглушение и нокдауны
    int stunFrames = 0;
    int knockdowns = 0;
    std::vector<std::pair<int, float>> headHits;  // (кадр, урон) за последние секунды
    // Окно контратаки после ухода на угол или идеального парирования
    int counterReady = 0;
    bool counterPerfect = false;
    // Бокс: заряды движения головой (до headMoveMaxCharges) и накопленный урон по корпусу
    int headCharges = 0, headChargeFrame = 0;
    float bodyWork = 0.f;
    // Ритм и «Поток»: чистые попадания подряд; пока flowUntil больше текущего кадра, урон усилен
    int rhythm = 0, rhythmFrame = 0, flowUntil = 0;
    // Буфер ввода
    const MoveData* buffered = nullptr;
    int bufferAge = 0;

    bool alive() const { return state != FighterState::KnockedOut; }
    bool down() const { return state == FighterState::Knockdown || state == FighterState::GettingUp; }
    bool stunned() const { return state == FighterState::HitStun || state == FighterState::BlockStun; }
};

enum class CombatEventType : unsigned char { Hit, Blocked, Evaded, PerfectParry, Whiff, Knockdown, GetUp, KnockedOut };

struct CombatEvent {
    CombatEventType type = CombatEventType::Hit;
    int frame = 0;
    int attacker = -1, defender = -1;
    const MoveData* move = nullptr;
    OutcomeKind outcome = OutcomeKind::Hit;
    contracts::HitZone zone = contracts::HitZone::Torso;
    float damage = 0, balanceDamage = 0, impulse = 0;
    Vec2 direction;  // от атакующего к защитнику
    bool counter = false, crit = false, back = false, downed = false, angle = false;
    bool flowStarted = false, flow = false;  // удар нанесён в «Потоке» / этим ударом открылся «Поток»
    bool doubled = false;  // удар по корпусу согнул защитника (накопленный урон)
    int headCharges = 0;   // заряды движения головой, пошедшие в этот удар
    std::string defense;  // имя защиты, сработавшей у защитника ("" — нет)
};

class CombatSim {
public:
    explicit CombatSim(const CombatRules& rules, std::uint32_t seed = 1) : rules_(rules), rng_(seed ? seed : 1u) {}

    // Возвращает индекс бойца. Боец стоит лицом к target (если задан).
    int addFighter(Vec2 pos, const FighterStats& stats = {});
    void setTarget(int fighter, int target);
    // Нокаут по числу нокдаунов (бой «по правилам»); в уличной драке — только по здоровью.
    void setOfficialRules(bool on) { official_ = on; }
    // Награда за комбо: засчитывает бойцу ещё hits чистых попаданий в ритм (может открыть «Поток»). Возвращает true, если Поток открылся.
    bool awardRhythm(int fighter, int hits);

    // Один кадр. inputs — по одному на бойца в порядке добавления (недостающие считаются пустыми).
    void step(const std::vector<FighterInput>& inputs);

    const Fighter& fighter(int i) const { return fighters_[static_cast<std::size_t>(i)]; }
    Fighter& fighterMutable(int i) { return fighters_[static_cast<std::size_t>(i)]; }
    int fighterCount() const { return static_cast<int>(fighters_.size()); }
    int frame() const { return frame_; }
    const std::vector<CombatEvent>& events() const { return events_; }  // события последнего кадра
    const std::vector<CombatEvent>& log() const { return log_; }        // все события с начала боя
    const CombatRules& rules() const { return rules_; }

    // Стоимость запуска приёма для бойца (стамина) с учётом выносливости, звёзд и движения.
    float staminaCost(const Fighter& f, const MoveData& m, Movement movement) const;
    // Урон попадания без случайности (без крита): для подсказок в интерфейсе и тестов.
    float expectedDamage(const Fighter& attacker, const Fighter& defender, const MoveData& m, Movement movement) const;
    // Расстояние между бойцами (центры), м.
    float distance(int a, int b) const;
    bool fightOver() const;

    // Событие удара в формате контракта для звука, эффектов и толпы (EntityId = индекс + 1).
    static contracts::HitEvent toContract(const CombatEvent& e, Vec2 attackerPos);

private:
    const CombatRules& rules_;
    std::vector<Fighter> fighters_;
    std::vector<CombatEvent> events_, log_;
    std::vector<FighterInput> lastInputs_;
    int frame_ = 0;
    std::uint32_t rng_;
    bool official_ = false;

    float rand01();
    Vec2 dirTo(const Fighter& from, const Fighter& to) const;
    void faceTarget(Fighter& f);
    void processInput(Fighter& f, const FighterInput& in);
    bool canStartAttack(const Fighter& f) const;
    void startAttack(Fighter& f, const MoveData& m, const FighterInput& in);
    void startDefense(Fighter& f, DefenseAction a);
    void advance(Fighter& f);
    // Снимок атакующего на начало разбора попаданий кадра: при одновременных ударах первый попавший не отменяет замах второго.
    struct AttackSnapshot {
        int attacker = -1;
        const MoveData* move = nullptr;
        int stateFrame = 0;
        Movement movement = Movement::Static;
        FighterStats stats;
        Vec2 facing;
        int counterReady = 0;
        bool counterPerfect = false;
    };
    void resolveHits();
    void resolve(Fighter& a, Fighter& d, const AttackSnapshot& s);
    void applyKnockdown(Fighter& d, int cause);
    void checkKnockout(Fighter& d, int cause);
    void regen(Fighter& f);
    void separate();
    void emit(const CombatEvent& e);
    const DefenseDef* defenseFor(DefenseAction a) const;
};

const char* defenseActionName(DefenseAction a);
}  // namespace han::combat
// Пример: CombatSim sim(rules); int a = sim.addFighter({0,0}), b = sim.addFighter({0,-1.2f}); sim.setTarget(a,b); sim.setTarget(b,a); sim.step({{.attack=&jab}, {}});
