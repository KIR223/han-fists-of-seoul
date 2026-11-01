#include "han/combat/sim.h"

#include <algorithm>
#include <cmath>

namespace han::combat {
namespace {
constexpr float kRadToDeg = 57.29578f;

float len(Vec2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
Vec2 norm(Vec2 v) {
    const float l = len(v);
    return l > 1e-6f ? Vec2{v.x / l, v.z / l} : Vec2{0.f, -1.f};
}
Vec2 rightOf(Vec2 dir) { return {-dir.z, dir.x}; }  // «вправо» для бойца, смотрящего вдоль dir (x — восток, z — юг)
float angleBetween(Vec2 a, Vec2 b) {
    const float d = std::clamp(a.x * b.x + a.z * b.z, -1.f, 1.f);
    return std::acos(d) * kRadToDeg;
}

int sideOf(DefenseAction a) {
    switch (a) {
        case DefenseAction::SlipLeft:
        case DefenseAction::BobLeft:
        case DefenseAction::ShoulderRollLeft:
        case DefenseAction::SidestepLeft: return -1;
        case DefenseAction::SlipRight:
        case DefenseAction::BobRight:
        case DefenseAction::ShoulderRollRight:
        case DefenseAction::SidestepRight: return +1;
        default: return 0;
    }
}

contracts::HitZone zoneOf(Height h) { return h == Height::Head ? contracts::HitZone::Head : (h == Height::Torso ? contracts::HitZone::Torso : contracts::HitZone::Legs); }
}  // namespace

const char* defenseActionName(DefenseAction a) {
    switch (a) {
        case DefenseAction::None: return "";
        case DefenseAction::Parry: return "parry";
        case DefenseAction::SlipLeft:
        case DefenseAction::SlipRight: return "slip";
        case DefenseAction::Duck: return "duck";
        case DefenseAction::BobLeft:
        case DefenseAction::BobRight: return "bob_weave";
        case DefenseAction::ShoulderRollLeft:
        case DefenseAction::ShoulderRollRight: return "shoulder_roll";
        case DefenseAction::LeanBack: return "lean_back";
        case DefenseAction::SidestepLeft:
        case DefenseAction::SidestepRight: return "sidestep";
        case DefenseAction::StepBack: return "step_back";
    }
    return "";
}

const DefenseDef* CombatSim::defenseFor(DefenseAction a) const { return rules_.defense(defenseActionName(a)); }

float CombatSim::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

int CombatSim::addFighter(Vec2 pos, const FighterStats& stats) {
    Fighter f;
    f.id = static_cast<int>(fighters_.size());
    f.pos = pos;
    f.stats = stats;
    f.stats.stars = std::clamp(f.stats.stars, 1, 3);
    f.balance = rules_.balanceMax;
    fighters_.push_back(std::move(f));
    return fighters_.back().id;
}

void CombatSim::setTarget(int fighter, int target) {
    if (fighter >= 0 && fighter < fighterCount()) fighters_[static_cast<std::size_t>(fighter)].target = (target >= 0 && target < fighterCount() && target != fighter) ? target : -1;
}

float CombatSim::distance(int a, int b) const { return len({fighter(a).pos.x - fighter(b).pos.x, fighter(a).pos.z - fighter(b).pos.z}); }

bool CombatSim::fightOver() const {
    int alive = 0;
    for (const Fighter& f : fighters_) alive += f.alive() ? 1 : 0;
    return alive <= 1 && fighters_.size() > 1;
}

Vec2 CombatSim::dirTo(const Fighter& from, const Fighter& to) const { return norm({to.pos.x - from.pos.x, to.pos.z - from.pos.z}); }

float CombatSim::staminaCost(const Fighter& f, const MoveData& m, Movement movement) const {
    float cost = m.stamina * (1.f - rules_.enduranceStaminaPerPoint * (f.stats.endurance - 5.f)) * (1.f - rules_.starStaminaDiscount * static_cast<float>(f.stats.stars - 1));
    if (movement == Movement::StepIn) cost *= rules_.stepInStamina;
    return std::max(cost, 0.f);
}

float CombatSim::expectedDamage(const Fighter& a, const Fighter& d, const MoveData& m, Movement movement) const {
    float dmg = m.damage * (1.f + rules_.strengthPerPoint * (a.stats.strength - 5.f)) * rules_.star[std::clamp(a.stats.stars, 1, 3) - 1] * rules_.zoneMultiplier(m.height);
    if (movement == Movement::StepIn) dmg *= rules_.stepInDamage;
    if (movement == Movement::Retreat) dmg *= rules_.retreatDamage;
    return dmg * (1.f - rules_.toughnessPerPoint * (d.stats.toughness - 5.f));
}

void CombatSim::faceTarget(Fighter& f) {
    if (f.target < 0 || f.down() || !f.alive()) return;
    const Fighter& t = fighters_[static_cast<std::size_t>(f.target)];
    f.facing = dirTo(f, t);
}

bool CombatSim::canStartAttack(const Fighter& f) const {
    switch (f.state) {
        case FighterState::Idle:
        case FighterState::Moving:
        case FighterState::Blocking: return true;
        case FighterState::Attacking: return f.move && f.stateFrame >= f.move->cancelFrom && f.stateFrame <= f.move->cancelTo;
        default: return false;
    }
}

void CombatSim::startAttack(Fighter& f, const MoveData& m, const FighterInput& in) {
    Movement mv = m.movement;
    if (mv == Movement::Static) {  // движение игрока в момент удара задаёт вариант (часть 22.2)
        if (in.forward > 0.5f) mv = Movement::StepIn;
        else if (in.forward < -0.5f) mv = Movement::Retreat;
        else if (std::fabs(in.right) > 0.5f) mv = Movement::Sidestep;
    }
    f.stamina -= staminaCost(f, m, mv);
    f.focus = std::max(0.f, f.focus - m.focus);
    f.move = &m;
    f.moveMovement = mv;
    f.moveResolved = false;
    f.moveSide = in.right < 0.f ? -1 : 1;
    f.state = FighterState::Attacking;
    f.stateFrame = 0;
    f.action = DefenseAction::None;
    f.buffered = nullptr;
}

void CombatSim::startDefense(Fighter& f, DefenseAction a) {
    const DefenseDef* def = defenseFor(a);
    if (!def || f.stamina < def->cost) return;
    f.stamina -= def->cost;
    f.state = FighterState::Defending;
    f.stateFrame = 0;
    f.action = a;
    f.move = nullptr;
}

void CombatSim::processInput(Fighter& f, const FighterInput& in) {
    if (!f.alive()) return;
    // Буфер ввода: запрос хранится несколько кадров и срабатывает, как только приём можно начать.
    if (in.attack) {
        f.buffered = in.attack;
        f.bufferAge = 0;
    } else if (f.buffered && ++f.bufferAge > rules_.inputBufferFrames) {
        f.buffered = nullptr;
    }
    if (f.down() || f.stunned()) return;

    if (in.defense != DefenseAction::None && (f.state == FighterState::Idle || f.state == FighterState::Moving || f.state == FighterState::Blocking)) startDefense(f, in.defense);

    if (f.buffered && canStartAttack(f) && f.stamina >= staminaCost(f, *f.buffered, f.buffered->movement) && f.focus >= f.buffered->focus) startAttack(f, *f.buffered, in);

    if (f.state == FighterState::Idle || f.state == FighterState::Moving || f.state == FighterState::Blocking) {
        if (in.blockHeld) {
            if (f.state != FighterState::Blocking) {
                f.state = FighterState::Blocking;
                f.stateFrame = 0;
                f.blockFrames = 0;
            }
        } else if (f.state == FighterState::Blocking) {
            f.state = FighterState::Idle;
        }
        if (f.state != FighterState::Blocking) {
            const Vec2 dir = f.facing, right = rightOf(dir);
            Vec2 v{dir.x * in.forward + right.x * in.right, dir.z * in.forward + right.z * in.right};
            const float l = len(v);
            if (l > 1.f) v = {v.x / l, v.z / l};
            const float speed = in.dash ? rules_.dashSpeed : rules_.walkSpeed;
            f.pos.x += v.x * speed / static_cast<float>(rules_.fps);
            f.pos.z += v.z * speed / static_cast<float>(rules_.fps);
            const bool moving = l > 0.05f;
            if (moving != (f.state == FighterState::Moving)) {
                f.state = moving ? FighterState::Moving : FighterState::Idle;
                f.stateFrame = 0;
            }
        }
    }
}

void CombatSim::advance(Fighter& f) {
    ++f.stateFrame;
    f.counterReady = std::max(0, f.counterReady - 1);
    switch (f.state) {
        case FighterState::Attacking: {
            const MoveData& m = *f.move;
            // Движение во время приёма: смещение распределено по кадрам замаха и активной фазы.
            const int moveFrames = m.startup + m.active;
            if (f.stateFrame <= moveFrames) {
                const float step = 0.4f / static_cast<float>(moveFrames);
                const Vec2 right = rightOf(f.facing);
                switch (f.moveMovement) {
                    case Movement::StepIn: f.pos.x += f.facing.x * step; f.pos.z += f.facing.z * step; break;
                    case Movement::Retreat: f.pos.x -= f.facing.x * step; f.pos.z -= f.facing.z * step; break;
                    case Movement::Sidestep:
                    case Movement::Circle:
                    case Movement::Pivot: f.pos.x += right.x * step * static_cast<float>(f.moveSide); f.pos.z += right.z * step * static_cast<float>(f.moveSide); break;
                    case Movement::Run: f.pos.x += f.facing.x * step * 2.5f; f.pos.z += f.facing.z * step * 2.5f; break;
                    case Movement::Static: break;
                }
            }
            if (f.stateFrame >= m.totalFrames()) {
                f.state = FighterState::Idle;
                f.stateFrame = 0;
                f.move = nullptr;
            }
            break;
        }
        case FighterState::Defending: {
            const DefenseDef* def = defenseFor(f.action);
            if (def) {
                if (f.stateFrame >= def->windowFrom && f.stateFrame <= def->windowTo && def->shift > 0.f) {
                    const float step = def->shift / static_cast<float>(def->windowTo - def->windowFrom + 1);
                    const Vec2 right = rightOf(f.facing);
                    const int side = sideOf(f.action);
                    if (def->sided) {
                        f.pos.x += right.x * step * static_cast<float>(side);
                        f.pos.z += right.z * step * static_cast<float>(side);
                    } else {
                        f.pos.x -= f.facing.x * step;
                        f.pos.z -= f.facing.z * step;
                    }
                }
                if (f.stateFrame >= def->total) {
                    f.state = FighterState::Idle;
                    f.stateFrame = 0;
                    f.action = DefenseAction::None;
                }
            } else {
                f.state = FighterState::Idle;
                f.action = DefenseAction::None;
            }
            break;
        }
        case FighterState::Blocking: ++f.blockFrames; break;
        case FighterState::HitStun:
        case FighterState::BlockStun:
            if (--f.stunFrames <= 0) {
                f.state = FighterState::Idle;
                f.stateFrame = 0;
            }
            break;
        case FighterState::Knockdown:
            if (f.stateFrame >= rules_.downFrames) {
                f.state = FighterState::GettingUp;
                f.stateFrame = 0;
            }
            break;
        case FighterState::GettingUp:
            if (f.stateFrame >= rules_.getupFrames) {
                f.state = FighterState::Idle;
                f.stateFrame = 0;
                f.balance = rules_.balanceMax * 0.5f;
                CombatEvent e;
                e.type = CombatEventType::GetUp;
                e.defender = f.id;
                emit(e);
            }
            break;
        default: break;
    }
}

void CombatSim::emit(const CombatEvent& e) {
    CombatEvent copy = e;
    copy.frame = frame_;
    events_.push_back(copy);
    log_.push_back(copy);
}

void CombatSim::applyKnockdown(Fighter& d, int cause) {
    d.state = FighterState::Knockdown;
    d.stateFrame = 0;
    d.move = nullptr;
    d.action = DefenseAction::None;
    d.buffered = nullptr;
    d.headHits.clear();
    ++d.knockdowns;
    CombatEvent e;
    e.type = CombatEventType::Knockdown;
    e.attacker = cause;
    e.defender = d.id;
    emit(e);
    if (official_ && d.knockdowns >= rules_.knockdownsToKo) checkKnockout(d, cause);
}

void CombatSim::checkKnockout(Fighter& d, int cause) {
    if (d.state == FighterState::KnockedOut) return;
    d.state = FighterState::KnockedOut;
    d.stateFrame = 0;
    d.move = nullptr;
    CombatEvent e;
    e.type = CombatEventType::KnockedOut;
    e.attacker = cause;
    e.defender = d.id;
    emit(e);
}

bool CombatSim::awardRhythm(int fighter, int hits) {
    if (fighter < 0 || fighter >= fighterCount() || hits <= 0) return false;
    Fighter& f = fighters_[static_cast<std::size_t>(fighter)];
    if (!f.alive() || f.flowUntil > frame_) return false;  // в «Потоке» награда не копится
    f.rhythmFrame = frame_;
    f.rhythm += hits;
    if (f.rhythm < rules_.rhythmHitsForFlow) return false;
    f.rhythm = 0;
    f.flowUntil = frame_ + rules_.flowFrames;
    return true;
}

void CombatSim::resolveHits() {
    std::vector<AttackSnapshot> pending;
    for (Fighter& a : fighters_) {
        if (a.state != FighterState::Attacking || !a.move || a.moveResolved || !a.move->inActiveFrames(a.stateFrame) || a.target < 0) continue;
        const Fighter& d = fighters_[static_cast<std::size_t>(a.target)];
        if (!d.alive()) continue;
        const Vec2 toD = dirTo(a, d);
        const float dist = len({d.pos.x - a.pos.x, d.pos.z - a.pos.z});
        const bool inRange = dist - rules_.bodyRadius <= a.move->range;
        const bool inArc = angleBetween(a.facing, toD) <= rules_.hitArcDegrees(a.move->trajectory);
        if (!inRange || !inArc) continue;  // не достал: попробует на следующем активном кадре
        pending.push_back({a.id, a.move, a.stateFrame, a.moveMovement, a.stats, a.facing, a.counterReady, a.counterPerfect});
    }
    for (const AttackSnapshot& s : pending) {
        Fighter& a = fighters_[static_cast<std::size_t>(s.attacker)];
        Fighter& d = fighters_[static_cast<std::size_t>(a.target)];
        if (!d.alive()) continue;
        resolve(a, d, s);
    }
}

void CombatSim::resolve(Fighter& a, Fighter& d, const AttackSnapshot& s) {
    const MoveData& m = *s.move;
    a.moveResolved = true;
    CombatEvent e;
    e.attacker = a.id;
    e.defender = d.id;
    e.move = &m;
    e.zone = zoneOf(m.height);
    e.direction = dirTo(a, d);
    const Vec2 toA = dirTo(d, a);
    const float defenderAngle = angleBetween(d.facing, toA);  // 0° — защитник смотрит на атакующего
    e.back = defenderAngle > 120.f && !d.down();
    e.downed = d.down();

    // 1. Что делает защитник: исход из таблицы (часть 22.6).
    Outcome out;  // по умолчанию — попадание
    if (!e.back && !d.down()) {
        if (d.state == FighterState::Defending) {
            const DefenseDef* def = defenseFor(d.action);
            if (def && d.stateFrame >= def->windowFrom && d.stateFrame <= def->windowTo && def->covered(m.height)) {
                out = def->outcome(classifyTrajectory(m.trajectory, sideOf(d.action)));
                e.defense = def->name;
                if (d.action == DefenseAction::Parry && out.kind == OutcomeKind::Perfect && d.stateFrame > def->perfectUntil) out = Outcome{OutcomeKind::Block, 1.f, false};  // поздно: просто отбил
            }
        } else if (d.state == FighterState::Blocking) {
            const char* name = m.height == Height::Head ? "high_block" : (m.height == Height::Torso ? "closed_block" : "check");
            const DefenseDef* def = rules_.defense(name);
            if (def && d.blockFrames >= def->startup && def->covered(m.height)) {
                out = def->outcome(classifyTrajectory(m.trajectory, 0));
                e.defense = def->name;
            }
        }
    }
    if (m.unblockable && (out.kind == OutcomeKind::Block || out.kind == OutcomeKind::Perfect)) out = Outcome{};
    e.outcome = out.kind;
    e.angle = out.angle;

    const int remaining = std::max(0, m.totalFrames() - s.stateFrame);
    switch (out.kind) {
        case OutcomeKind::Evade:
            e.type = CombatEventType::Evaded;
            if (d.stats.headMovement) {
                d.headCharges = std::min(rules_.headMoveMaxCharges, d.headCharges + 1);
                d.headChargeFrame = frame_;
            }
            if (out.angle) {
                d.counterReady = 30;  // «уход + угол»: окно контратаки
                d.counterPerfect = false;
            }
            emit(e);
            return;
        case OutcomeKind::Perfect:
            e.type = CombatEventType::PerfectParry;
            a.state = FighterState::BlockStun;  // атакующий «проваливается»
            a.stateFrame = 0;
            a.stunFrames = 24;
            a.move = nullptr;
            d.counterReady = 40;
            d.counterPerfect = true;
            emit(e);
            return;
        default: break;
    }

    // 2. Урон и баланс.
    const bool counterHit = d.state == FighterState::Attacking && d.move && d.stateFrame < d.move->startup;  // бьём, пока он замахивается
    float situation = 1.f;
    if (counterHit) {
        situation *= rules_.counter;
        e.counter = true;
    }
    if (s.counterReady > 0) {
        situation *= s.counterPerfect ? rules_.perfectParryCounter : rules_.counter;
        e.counter = true;
        a.counterReady = 0;
    }
    if (s.stats.headMovement && a.headCharges > 0 && out.kind != OutcomeKind::Block) {
        situation *= 1.f + rules_.headMoveBonus * static_cast<float>(a.headCharges);  // «движение головой»: до ×1,5 на следующем ударе
        e.headCharges = a.headCharges;
        a.headCharges = 0;
    }
    if (a.flowUntil > frame_ && out.kind != OutcomeKind::Block) {
        situation *= rules_.flow;
        e.flow = true;
    }
    if (e.back) situation *= rules_.back;
    if (e.downed) situation *= rules_.downed;
    const float critChance = rules_.critBase + rules_.critTechniquePerPoint * (s.stats.technique - 5.f);
    if (out.kind == OutcomeKind::Hit && rand01() < critChance) {
        situation *= rules_.crit;
        e.crit = true;
    }
    float taken = 1.f;
    if (out.kind == OutcomeKind::Block) {
        const float base = m.power ? rules_.blockTakenPower : rules_.blockTaken;
        taken = 1.f - (1.f - base) * rules_.blockFactorForAngle(defenderAngle);
    } else if (out.kind == OutcomeKind::Partial) {
        taken = rules_.partialTaken;
    }
    const float strengthBalance = 1.f + rules_.strengthBalancePerPoint * (s.stats.strength - 5.f);
    Fighter attackerView = a;  // статы и движение на момент удара (атакующего могли уже оглушить в этом кадре)
    attackerView.stats = s.stats;
    e.damage = expectedDamage(attackerView, d, m, s.movement) * situation * out.multiplier * taken;
    e.balanceDamage = m.balance * strengthBalance * out.multiplier * (out.kind == OutcomeKind::Block ? 0.5f : 1.f) * (m.power && out.kind == OutcomeKind::Block ? 1.5f : 1.f);
    e.impulse = m.impulse * (out.kind == OutcomeKind::Block ? 0.4f : 1.f);

    d.health = std::max(0.f, d.health - e.damage);
    d.balance -= e.balanceDamage;
    if (out.kind == OutcomeKind::Hit) {
        d.headCharges = 0;  // пропущенный удар сбивает ритм движения головой
        d.rhythm = 0;       // и ритм серии вместе с «Потоком»
        d.flowUntil = 0;
    }
    if (out.kind == OutcomeKind::Hit || out.kind == OutcomeKind::Partial) {  // чистое попадание растит ритм атакующего
        a.rhythmFrame = frame_;
        if (a.flowUntil <= frame_ && ++a.rhythm >= rules_.rhythmHitsForFlow) {
            a.rhythm = 0;
            a.flowUntil = frame_ + rules_.flowFrames;
            e.flowStarted = true;
        }
    }
    bool doubledOver = false;
    if (m.height == Height::Torso && out.kind != OutcomeKind::Block && !d.down()) {  // работа по корпусу: урон накапливается
        d.bodyWork += e.damage;
        if (d.bodyWork >= rules_.bodyWorkThreshold) {
            d.bodyWork = 0.f;
            doubledOver = true;
            e.doubled = true;
        }
    }
    if (out.kind == OutcomeKind::Block) d.stamina = std::max(0.f, d.stamina - m.damage * 0.5f);

    // 3. Реакция защитника: оглушение по кадровому преимуществу (отдача — остаток восстановления атакующего).
    const bool armored = d.move && d.move->armor && d.state == FighterState::Attacking;
    if (!armored && !d.down()) {
        d.state = out.kind == OutcomeKind::Block ? FighterState::BlockStun : FighterState::HitStun;
        d.stateFrame = 0;
        d.stunFrames = std::max(4, remaining + (out.kind == OutcomeKind::Block ? m.onBlockFrames : m.onHitFrames));
        if (doubledOver) d.stunFrames = std::max(d.stunFrames, rules_.bodyDoubledFrames);  // согнулся: окно для апперкота
        d.move = nullptr;
        d.action = DefenseAction::None;
        d.buffered = nullptr;
    }
    e.type = out.kind == OutcomeKind::Block ? CombatEventType::Blocked : CombatEventType::Hit;
    emit(e);

    // 4. Нокаут по здоровью, нокдаун по балансу или накопленному урону в голову.
    if (d.health <= 0.f) {
        checkKnockout(d, a.id);
        return;
    }
    if (m.height == Height::Head && out.kind != OutcomeKind::Block) {
        d.headHits.push_back({frame_, e.damage});
        const int windowFrames = static_cast<int>(rules_.headWindowSeconds * static_cast<float>(rules_.fps));
        d.headHits.erase(std::remove_if(d.headHits.begin(), d.headHits.end(), [&](const std::pair<int, float>& h) { return frame_ - h.first > windowFrames; }), d.headHits.end());
        float total = 0.f;
        for (const auto& h : d.headHits) total += h.second;
        if (total > rules_.knockdownThreshold(d.stats.toughness)) {
            applyKnockdown(d, a.id);
            return;
        }
    }
    if (d.balance <= 0.f && !d.down()) applyKnockdown(d, a.id);
}

void CombatSim::regen(Fighter& f) {
    if (!f.alive()) return;
    const float fps = static_cast<float>(rules_.fps);
    if (f.state != FighterState::Attacking && f.state != FighterState::Defending) {
        const float rate = f.state == FighterState::Blocking ? rules_.staminaRegenBlock : rules_.staminaRegen;
        f.stamina = std::min(100.f, f.stamina + rate / fps);
    }
    if (f.rhythm > 0 && frame_ - f.rhythmFrame > rules_.rhythmDecayFrames) f.rhythm = 0;
    if (f.headCharges > 0 && frame_ - f.headChargeFrame > rules_.headMoveDecayFrames) f.headCharges = 0;
    f.bodyWork = std::max(0.f, f.bodyWork - rules_.bodyWorkDecayPerSecond / fps);
    if (!f.stunned() && !f.down()) f.balance = std::min(rules_.balanceMax, f.balance + rules_.balanceRegen / fps);
}

void CombatSim::separate() {
    const float minDist = rules_.bodyRadius * 2.f;
    for (std::size_t i = 0; i < fighters_.size(); ++i)
        for (std::size_t j = i + 1; j < fighters_.size(); ++j) {
            Fighter &a = fighters_[i], &b = fighters_[j];
            if (!a.alive() || !b.alive()) continue;
            const Vec2 d{b.pos.x - a.pos.x, b.pos.z - a.pos.z};
            const float l = len(d);
            if (l >= minDist) continue;
            const Vec2 n = l > 1e-5f ? Vec2{d.x / l, d.z / l} : Vec2{1.f, 0.f};
            const float push = (minDist - l) * 0.5f;
            a.pos.x -= n.x * push;
            a.pos.z -= n.z * push;
            b.pos.x += n.x * push;
            b.pos.z += n.z * push;
        }
}

void CombatSim::step(const std::vector<FighterInput>& inputs) {
    events_.clear();
    ++frame_;
    const FighterInput empty;
    for (Fighter& f : fighters_) faceTarget(f);
    for (std::size_t i = 0; i < fighters_.size(); ++i) processInput(fighters_[i], i < inputs.size() ? inputs[i] : empty);
    for (Fighter& f : fighters_) advance(f);
    separate();
    resolveHits();
    for (Fighter& f : fighters_) regen(f);
}

contracts::HitEvent CombatSim::toContract(const CombatEvent& e, Vec2 attackerPos) {
    contracts::HitEvent h;
    h.attacker = static_cast<contracts::EntityId>(e.attacker + 1);
    h.defender = static_cast<contracts::EntityId>(e.defender + 1);
    h.zone = e.zone;
    h.damage = e.damage;
    h.impulse = e.impulse;
    h.point = {attackerPos.x + e.direction.x, 1.4f, attackerPos.z + e.direction.z};
    h.blocked = e.type == CombatEventType::Blocked;
    h.counter = e.counter;
    return h;
}
}  // namespace han::combat
