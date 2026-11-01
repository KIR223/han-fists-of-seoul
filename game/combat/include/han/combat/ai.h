// han/combat/ai.h — ИИ бойца (G3): решения на основе полезности, реакция по опыту, защита по таблице исходов (те же правила, что у игрока),
// ошибки техники по профилю, жетоны атаки в групповом бою. ИИ не читает чужие «мысли»: он видит только состояние бойцов в CombatSim.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "han/combat/profile.h"

namespace han::combat {
// Жетоны атаки: в групповой драке одновременно бьют цель не больше maxPerTarget бойцов, остальные кружат и ждут (читаемость боя 1 на 4).
class AttackTokens {
public:
    explicit AttackTokens(int maxPerTarget = 2, int holdFrames = 70) : max_(maxPerTarget), hold_(holdFrames) {}
    // true — жетон получен (или уже был у этого бойца и продлён). Жетон сам истекает через holdFrames.
    bool tryAcquire(int attacker, int target, int frame);
    void release(int attacker, int target);
    int holders(int target, int frame) const;

private:
    struct Entry {
        int attacker, target, until;
    };
    int max_, hold_;
    std::vector<Entry> entries_;
};

class FightAI {
public:
    enum class Intent : unsigned char { Idle, Approach, Retreat, Circle, Attack, Defend, Guard };

    FightAI(const FighterProfile& profile, std::vector<const MoveData*> moves, const CombatRules& rules, std::uint32_t seed = 1, AttackTokens* tokens = nullptr);

    // Ввод бойца self на текущий кадр. Вызывать до CombatSim::step.
    FighterInput decide(const CombatSim& sim, int self);

    Intent intent() const { return intent_; }
    int reactionDelayFrames() const { return lastDelay_; }
    int attacksStarted() const { return attacks_; }
    int defensesStarted() const { return defenses_; }
    const std::string& lastDefense() const { return lastDefense_; }
    const FighterProfile& profile() const { return profile_; }

private:
    float rand01();
    float flankSide(const CombatSim& sim, int self) const;
    int reactionDelay();
    bool chooseDefense(const Fighter& me, const MoveData& threat, int untilHit, FighterInput& out);
    const MoveData* chooseAttack(const CombatSim& sim, const Fighter& me, const Fighter& foe, float dist);

    FighterProfile profile_;
    std::vector<const MoveData*> moves_;
    const CombatRules& rules_;
    std::uint32_t rng_;
    AttackTokens* tokens_;
    Intent intent_ = Intent::Idle;
    // Память о текущей угрозе
    int threatStart_ = -1000;          // кадр начала чужого приёма (ключ угрозы)
    const MoveData* threatMove_ = nullptr;
    int reactionAt_ = 0;               // кадр, когда боец «осознал» угрозу
    bool defended_ = false;
    int blockUntil_ = 0, guardUntil_ = 0, nextThink_ = 0, nextAttack_ = 0, dropHandsUntil_ = 0, circleUntil_ = 0;
    float circleDir_ = 1.f;
    int lastDelay_ = 0, attacks_ = 0, defenses_ = 0;
    std::string lastDefense_;
};
}  // namespace han::combat
// Пример: FightAI ai(profile, moves, rules, seed, &tokens); sim.step({playerInput, ai.decide(sim, 1)});
