#include "han/combat/ai.h"

#include <algorithm>
#include <cmath>

namespace han::combat {
namespace {
struct Candidate {
    DefenseAction action = DefenseAction::None;
    const char* name = "";
    int side = 0;
    bool hold = false;
};
}  // namespace

bool AttackTokens::tryAcquire(int attacker, int target, int frame) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.until <= frame; }), entries_.end());
    for (Entry& e : entries_)
        if (e.attacker == attacker && e.target == target) {
            e.until = frame + hold_;
            return true;
        }
    if (holders(target, frame) >= max_) return false;
    entries_.push_back({attacker, target, frame + hold_});
    return true;
}

void AttackTokens::release(int attacker, int target) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.attacker == attacker && e.target == target; }), entries_.end());
}

int AttackTokens::holders(int target, int frame) const {
    int n = 0;
    for (const Entry& e : entries_) n += (e.target == target && e.until > frame) ? 1 : 0;
    return n;
}

FightAI::FightAI(const FighterProfile& profile, std::vector<const MoveData*> moves, const CombatRules& rules, std::uint32_t seed, AttackTokens* tokens)
    : profile_(profile), moves_(std::move(moves)), rules_(rules), rng_(seed ? seed : 1u), tokens_(tokens) {}

float FightAI::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

int FightAI::reactionDelay() {
    // Опытный боец замечает удар за 2–4 кадра, новичок — за 14–18; опьянение и паника добавляют.
    float delay = 14.f - 12.f * (profile_.experience / 10.f);
    delay *= 1.f + profile_.impairment();
    if (profile_.personality == Personality::Panic) delay += 3.f * rand01();
    delay += 3.f * rand01();
    return std::max(1, static_cast<int>(delay + 0.5f));
}

bool FightAI::chooseDefense(const Fighter& me, const MoveData& threat, int untilHit, FighterInput& out) {
    static const Candidate all[] = {{DefenseAction::Parry, "parry", 0, false},       {DefenseAction::SlipLeft, "slip", -1, false},  {DefenseAction::SlipRight, "slip", +1, false},
                                    {DefenseAction::Duck, "duck", 0, false},         {DefenseAction::LeanBack, "lean_back", 0, false}, {DefenseAction::SidestepLeft, "sidestep", -1, false},
                                    {DefenseAction::SidestepRight, "sidestep", +1, false}, {DefenseAction::StepBack, "step_back", 0, false}, {DefenseAction::None, "", 0, true}};
    struct Scored {
        Candidate c;
        float score;
    };
    std::vector<Scored> feasible;
    for (const Candidate& c : all) {
        const char* name = c.name;
        if (c.hold) name = threat.height == Height::Head ? "high_block" : (threat.height == Height::Torso ? "closed_block" : "check");
        const DefenseDef* def = rules_.defense(name);
        if (!def) continue;
        Outcome o;
        int frameAtHit = untilHit;
        if (c.hold) {
            if (untilHit < def->startup) continue;  // блок не успеет подняться
        } else {
            if (frameAtHit < def->windowFrom || frameAtHit > def->windowTo || me.stamina < def->cost) continue;
        }
        o = def->covered(threat.height) ? def->outcome(classifyTrajectory(threat.trajectory, c.side)) : Outcome{};
        if (threat.unblockable && (o.kind == OutcomeKind::Block || o.kind == OutcomeKind::Perfect)) o = Outcome{};
        float score = 1.f;
        switch (o.kind) {
            case OutcomeKind::Hit: score = o.multiplier; break;
            case OutcomeKind::Block: score = threat.power ? rules_.blockTakenPower : rules_.blockTaken; break;
            case OutcomeKind::Partial: score = rules_.partialTaken; break;
            case OutcomeKind::Evade: score = o.angle ? -0.2f : 0.f; break;
            case OutcomeKind::Perfect: score = (c.action == DefenseAction::Parry && frameAtHit <= def->perfectUntil) ? -0.5f : rules_.blockTaken; break;
        }
        score += def->cost * 0.004f;
        if (c.hold) score += 0.05f;  // блок утомляет и оставляет без контратаки
        if (profile_.personality == Personality::Counter && o.angle) score -= 0.15f;
        if (profile_.personality == Personality::Defensive && c.hold) score -= 0.12f;
        if (profile_.personality == Personality::Aggressor && !c.hold && o.kind == OutcomeKind::Evade) score -= 0.05f;
        feasible.push_back({c, score});
    }
    if (feasible.empty()) return false;
    std::sort(feasible.begin(), feasible.end(), [](const Scored& a, const Scored& b) { return a.score < b.score; });
    if (feasible.front().score >= 1.f) return false;  // лучшая защита не спасает — не тратим силы
    // Умение защиты решает, найдёт ли боец лучший вариант или выберет что-то случайное (в том числе плохое).
    const float skill = std::clamp(0.35f + 0.065f * profile_.defense - profile_.impairment() * 0.4f, 0.1f, 1.f);
    std::size_t pick = 0;
    if (rand01() > skill) pick = static_cast<std::size_t>(rand01() * static_cast<float>(feasible.size())) % feasible.size();
    if (rand01() < (1.f - profile_.techniqueQuality) * 0.15f) return false;  // растерялся
    const Candidate& c = feasible[pick].c;
    if (c.hold) {
        out.blockHeld = true;
        blockUntil_ = threat.startup + 10 + static_cast<int>(rand01() * 8.f);  // относительное; пересчитаем в decide
        lastDefense_ = "block";
    } else {
        out.defense = c.action;
        lastDefense_ = c.name;
    }
    return true;
}

const MoveData* FightAI::chooseAttack(const CombatSim& sim, const Fighter& me, const Fighter& foe, float dist) {
    std::vector<std::pair<const MoveData*, float>> options;
    float total = 0.f;
    const float quality = profile_.techniqueQuality;
    for (const MoveData* m : moves_) {
        if (!m) continue;
        // Небрежный боец хуже оценивает дальность (и бьёт с неудобной дистанции).
        const float estimated = m->range * (0.85f + 0.15f * quality) + 0.0f;
        if (dist - rules_.bodyRadius > estimated - 0.05f) continue;
        if (me.stamina < sim.staminaCost(me, *m, m->movement) || me.focus < m->focus) continue;
        float w = 0.15f;
        const auto it = profile_.actionWeights.find(m->type);
        if (it != profile_.actionWeights.end()) w = it->second;
        else if (!profile_.actionWeights.empty()) w = 0.02f;  // не в его репертуаре
        if (me.counterReady > 0 && m->type == "counter") w *= 3.f;
        if (foe.stunned() || foe.down()) w *= (m->power ? 1.8f : 1.f);
        if (m->type == "special") w *= (foe.balance < 40.f ? 2.f : 0.2f);
        w *= 0.5f + 0.5f * (static_cast<float>(m->unlockLevel) <= profile_.experience + 2.f ? 1.f : 0.f);  // сложные приёмы — опытным
        if (w <= 0.f) continue;
        options.push_back({m, w});
        total += w;
    }
    if (options.empty()) return nullptr;
    float r = rand01() * total;
    for (const auto& [m, w] : options) {
        r -= w;
        if (r <= 0.f) return m;
    }
    return options.back().first;
}

// Сторона обхода при групповой драке: среди живых бойцов, бьющих ту же цель, ранг 0 идёт прямо, нечётные — вправо, чётные — влево.
float FightAI::flankSide(const CombatSim& sim, int self) const {
    const int target = sim.fighter(self).target;
    if (target < 0) return 0.f;
    int rank = 0, others = 0;
    for (int i = 0; i < sim.fighterCount(); ++i) {
        if (i == self || i == target || !sim.fighter(i).alive() || sim.fighter(i).target != target) continue;
        ++others;
        if (i < self) ++rank;
    }
    if (others == 0 || rank == 0) return 0.f;
    return rank % 2 == 1 ? 1.f : -1.f;
}

FighterInput FightAI::decide(const CombatSim& sim, int self) {
    FighterInput out;
    const Fighter& me = sim.fighter(self);
    intent_ = Intent::Idle;
    if (me.target < 0 || !me.alive() || me.down() || me.stunned()) {
        defended_ = false;
        return out;
    }
    const Fighter& foe = sim.fighter(me.target);
    if (!foe.alive()) return out;
    const int frame = sim.frame();
    const float dist = sim.distance(self, me.target);

    // 1. Угроза: чужой приём ещё не дошёл до активных кадров и достаёт до нас.
    int untilHit = -1;
    const MoveData* threat = nullptr;
    if (foe.state == FighterState::Attacking && foe.move && !foe.moveResolved && foe.stateFrame < foe.move->startup && dist - rules_.bodyRadius <= foe.move->range + 0.4f) {
        threat = foe.move;
        untilHit = foe.move->startup - foe.stateFrame;
        const int start = frame - foe.stateFrame;
        if (threatMove_ != threat || start != threatStart_) {  // новая угроза
            threatMove_ = threat;
            threatStart_ = start;
            lastDelay_ = reactionDelay();
            reactionAt_ = frame + lastDelay_;
            defended_ = false;
        }
    }

    // 2. Защита: сработала реакция, ещё есть время, защита ещё не выбрана; после своей атаки с ошибкой «опущенные руки» защиты нет.
    const bool handsDown = profile_.hasMistake("drops_hands") && frame < dropHandsUntil_;
    if (threat && !defended_ && frame >= reactionAt_ && !handsDown && (me.state == FighterState::Idle || me.state == FighterState::Moving || me.state == FighterState::Blocking)) {
        if (chooseDefense(me, *threat, untilHit, out)) {
            defended_ = true;
            ++defenses_;
            intent_ = Intent::Defend;
            if (out.blockHeld) blockUntil_ = frame + untilHit + 8 + static_cast<int>(rand01() * 6.f);
        }
    }
    if (frame < blockUntil_) {
        out.blockHeld = true;
        if (intent_ == Intent::Idle) intent_ = Intent::Guard;
    }
    if (intent_ == Intent::Defend) return out;

    // 3. Раз в несколько кадров — «подумать»: встать в защитную стойку или нет.
    if (frame >= nextThink_) {
        nextThink_ = frame + 10 + static_cast<int>(rand01() * 8.f);
        float guardProb = 0.15f;
        switch (profile_.personality) {
            case Personality::Defensive: guardProb = 0.5f; break;
            case Personality::Counter: guardProb = 0.3f; break;
            case Personality::Panic: guardProb = 0.35f; break;
            case Personality::Aggressor: guardProb = 0.05f; break;
            case Personality::Balanced: break;
        }
        if (me.health < 30.f && profile_.courage < 5.f) guardProb += 0.3f;
        if (me.stamina < 25.f) guardProb += 0.3f;
        if (dist < 2.6f && rand01() < guardProb && !handsDown) guardUntil_ = frame + 18 + static_cast<int>(rand01() * 22.f);
        if (rand01() < 0.25f) {
            circleDir_ = rand01() < 0.5f ? -1.f : 1.f;
            circleUntil_ = frame + 20 + static_cast<int>(rand01() * 30.f);
        }
    }
    if (frame < guardUntil_ && !(me.state == FighterState::Attacking)) {
        out.blockHeld = true;
        intent_ = Intent::Guard;
    }

    // 4. Атака: когда в дальности, есть жетон и хватает ресурсов.
    const bool canStart = me.state == FighterState::Idle || me.state == FighterState::Moving || me.state == FighterState::Blocking;
    if (canStart && frame >= nextAttack_ && !(threat && !defended_ && frame < reactionAt_ + 1 && profile_.personality == Personality::Defensive)) {
        float p = 0.02f + 0.014f * profile_.aggression;
        if (foe.stunned() || foe.down()) p *= 2.2f;
        if (me.counterReady > 0) p = std::max(p, 0.5f);
        if (me.stamina < 35.f) p *= 0.5f;
        if (profile_.personality == Personality::Counter && me.counterReady <= 0 && !foe.stunned()) p *= 0.5f;
        if (profile_.personality == Personality::Panic && rand01() < 0.05f) p = 0.6f;
        if (profile_.sobriety != Sobriety::Sober) p *= 1.2f;
        if (rand01() < p) {
            const MoveData* m = chooseAttack(sim, me, foe, dist);
            const bool ok = m && (!tokens_ || tokens_->tryAcquire(self, me.target, frame));
            if (ok) {
                out.attack = m;
                out.blockHeld = false;
                ++attacks_;
                intent_ = Intent::Attack;
                // Время до следующего решения об атаке: быстрее у агрессивных, медленнее у осторожных.
                nextAttack_ = frame + m->totalFrames() + 6 + static_cast<int>(rand01() * (14.f - profile_.aggression));
                if (profile_.hasMistake("drops_hands")) dropHandsUntil_ = frame + m->totalFrames() + 18;
                if (profile_.hasMistake("leans_forward") && rand01() < 0.5f) out.forward = 1.f;  // тянется вперёд на ударе (шаг навстречу)
                return out;
            }
        }
    }

    // 5. Движение: держать дистанцию по характеру; при усталости и страхе — отходить.
    if (me.state == FighterState::Idle || me.state == FighterState::Moving) {
        float preferred = 1.2f;
        if (!moves_.empty()) {
            float sum = 0.f;
            for (const MoveData* m : moves_) sum += m ? m->range : 0.f;
            preferred = sum / static_cast<float>(moves_.size());
        }
        // Бойцы сближаются до дистанции, с которой их приёмы реально достают: агрессор — ближе, контрбоец — на самой границе джеба.
        const float idealGap = preferred * (profile_.personality == Personality::Aggressor ? 0.78f : (profile_.personality == Personality::Counter || profile_.personality == Personality::Defensive ? 0.95f : 0.86f));
        const float ideal = rules_.bodyRadius + idealGap;
        const bool afraid = (me.health < 30.f && profile_.courage < 5.f) || me.stamina < 20.f;
        if (afraid) {
            out.forward = -1.f;
            intent_ = Intent::Retreat;
        } else if (dist > ideal) {
            out.forward = 1.f;
            intent_ = Intent::Approach;
            // Группа на одну цель: нечётные заходят слева, чётные справа, первый — прямо (окружение вместо колонны).
            if (dist > ideal + 0.5f) out.right = flankSide(sim, self) * 0.8f;
        } else if (dist < ideal - 0.35f) {
            out.forward = -1.f;
            intent_ = Intent::Retreat;
        } else if (frame < circleUntil_) {
            const float side = flankSide(sim, self);
            out.right = side != 0.f ? side : circleDir_;
            intent_ = Intent::Circle;
        }
    }
    return out;
}
}  // namespace han::combat
