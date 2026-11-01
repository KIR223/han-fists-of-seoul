#include "han/combat/rules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace han::combat {
namespace {
using core::Json;

bool number(const Json& j, const char* key, float& out, float lo, float hi, std::string& error) {
    if (!j.has(key)) return true;  // нет в файле — остаётся значение по умолчанию
    const double v = j[key].asNumber(std::nan(""));
    if (!(v >= lo && v <= hi)) {
        error = std::string("поле «") + key + "» должно быть числом от " + std::to_string(lo) + " до " + std::to_string(hi);
        return false;
    }
    out = static_cast<float>(v);
    return true;
}

bool integer(const Json& j, const char* key, int& out, int lo, int hi, std::string& error) {
    float v = static_cast<float>(out);
    if (!number(j, key, v, static_cast<float>(lo), static_cast<float>(hi), error)) return false;
    out = static_cast<int>(v);
    return true;
}
}  // namespace

bool DefenseDef::covered(Height h) const { return std::find(covers.begin(), covers.end(), h) != covers.end(); }

bool parseOutcome(const std::string& text, Outcome& out) {
    Outcome o;
    std::string word = text;
    const std::size_t star = text.find('*');
    if (star != std::string::npos) {
        word = text.substr(0, star);
        char* end = nullptr;
        const std::string tail = text.substr(star + 1);
        o.multiplier = std::strtof(tail.c_str(), &end);
        if (tail.empty() || end != tail.c_str() + tail.size() || !(o.multiplier > 0.f && o.multiplier <= 5.f)) return false;
    }
    if (word == "hit") o.kind = OutcomeKind::Hit;
    else if (word == "block") o.kind = OutcomeKind::Block;
    else if (word == "evade") o.kind = OutcomeKind::Evade;
    else if (word == "evade_angle") {
        o.kind = OutcomeKind::Evade;
        o.angle = true;
    } else if (word == "partial") o.kind = OutcomeKind::Partial;
    else if (word == "perfect") o.kind = OutcomeKind::Perfect;
    else return false;
    if (o.multiplier != 1.f && o.kind != OutcomeKind::Hit) return false;  // множитель имеет смысл только у попадания
    out = o;
    return true;
}

TrajClass classifyTrajectory(Trajectory t, int defenderSide) {
    switch (t) {
        case Trajectory::Straight:
        case Trajectory::Push: return TrajClass::Straight;
        case Trajectory::Up: return TrajClass::Up;
        case Trajectory::Down: return TrajClass::Down;
        case Trajectory::Low: return TrajClass::Low;
        case Trajectory::Spin: return TrajClass::ArcSame;  // вращательный блок держит, но сбивает баланс сильнее
        case Trajectory::ArcLeft:
        case Trajectory::ArcRight: {
            if (defenderSide == 0) return TrajClass::ArcSame;
            // Левый хук атакующего летит со стороны правой руки защитника (они лицом к лицу): шаг вправо — навстречу удару.
            const int attackerSide = t == Trajectory::ArcLeft ? -1 : +1;
            return defenderSide == -attackerSide ? TrajClass::ArcSame : TrajClass::ArcOther;
        }
    }
    return TrajClass::Straight;
}

bool CombatRules::parse(const Json& j, std::string& error) {
    CombatRules r;
    const auto fail = [&](const std::string& why) {
        error = "правила боя: " + why;
        return false;
    };
    if (!j.isObject()) return fail("ожидался объект");
    std::string e;
    const Json& d = j["damage"];
    if (!number(d, "strength_per_point", r.strengthPerPoint, 0, 1, e) || !number(d, "toughness_per_point", r.toughnessPerPoint, 0, 1, e) ||
        !number(d, "counter", r.counter, 0.1f, 5, e) || !number(d, "perfect_parry_counter", r.perfectParryCounter, 0.1f, 5, e) ||
        !number(d, "back", r.back, 0.1f, 5, e) || !number(d, "downed", r.downed, 0, 5, e) || !number(d, "crit", r.crit, 0.1f, 5, e) ||
        !number(d, "flow", r.flow, 0.1f, 5, e) || !number(d, "block_taken", r.blockTaken, 0, 1, e) || !number(d, "block_taken_power", r.blockTakenPower, 0, 1, e) ||
        !number(d, "partial_taken", r.partialTaken, 0, 1, e))
        return fail("damage: " + e);
    if (d.has("star")) {
        if (d["star"].size() != 3) return fail("damage.star — три числа (1, 2, 3 звезды)");
        for (int i = 0; i < 3; ++i) {
            const double v = d["star"].at(static_cast<std::size_t>(i)).asNumber(-1);
            if (!(v >= 0.5 && v <= 3)) return fail("damage.star: числа 0.5..3");
            r.star[i] = static_cast<float>(v);
        }
    }
    if (d.has("zone")) {
        if (!number(d["zone"], "head", r.zoneHead, 0, 5, e) || !number(d["zone"], "torso", r.zoneTorso, 0, 5, e) || !number(d["zone"], "legs", r.zoneLegs, 0, 5, e))
            return fail("damage.zone: " + e);
    }
    if (!number(j["crit"], "base_chance", r.critBase, 0, 1, e) || !number(j["crit"], "technique_per_point", r.critTechniquePerPoint, 0, 1, e)) return fail("crit: " + e);
    const Json& s = j["stamina"];
    if (!number(s, "endurance_per_point", r.enduranceStaminaPerPoint, 0, 1, e) || !number(s, "star_discount", r.starStaminaDiscount, 0, 1, e) ||
        !number(s, "regen_per_second", r.staminaRegen, 0, 500, e) || !number(s, "regen_block_per_second", r.staminaRegenBlock, 0, 500, e) ||
        !number(s, "retreat_regen_multiplier", r.retreatRegenMultiplier, 0, 10, e))
        return fail("stamina: " + e);
    const Json& b = j["balance"];
    if (!number(b, "max", r.balanceMax, 1, 1000, e) || !number(b, "regen_per_second", r.balanceRegen, 0, 500, e) || !number(b, "strength_per_point", r.strengthBalancePerPoint, 0, 1, e))
        return fail("balance: " + e);
    const Json& k = j["knockdown"];
    if (!number(k, "head_damage_window_seconds", r.headWindowSeconds, 0.1f, 30, e) || !number(k, "base_threshold", r.knockdownBase, 1, 1000, e) ||
        !number(k, "toughness_per_point", r.knockdownToughnessPerPoint, 0, 100, e) || !integer(k, "down_frames", r.downFrames, 1, 1000, e) ||
        !integer(k, "getup_frames", r.getupFrames, 1, 1000, e) || !integer(k, "knockdowns_to_ko", r.knockdownsToKo, 1, 20, e))
        return fail("knockdown: " + e);
    const Json& m = j["movement"];
    if (!number(m, "walk_speed", r.walkSpeed, 0.1f, 20, e) || !number(m, "dash_speed", r.dashSpeed, 0.1f, 40, e) || !number(m, "body_radius", r.bodyRadius, 0.05f, 2, e))
        return fail("movement: " + e);
    if (m.has("step_in") && (!number(m["step_in"], "range_bonus", r.stepInRange, 0, 3, e) || !number(m["step_in"], "damage", r.stepInDamage, 0.1f, 5, e) ||
                              !number(m["step_in"], "stamina", r.stepInStamina, 0.1f, 5, e)))
        return fail("movement.step_in: " + e);
    if (m.has("retreat") && !number(m["retreat"], "damage", r.retreatDamage, 0.1f, 5, e)) return fail("movement.retreat: " + e);
    if (m.has("sidestep") && !number(m["sidestep"], "block_effect", r.sidestepBlockEffect, 0, 1, e)) return fail("movement.sidestep: " + e);
    const Json& arcs = j["hit_arc_degrees"];
    if (!number(arcs, "straight", r.arcStraight, 1, 180, e) || !number(arcs, "arc", r.arcArc, 1, 180, e) || !number(arcs, "up", r.arcUp, 1, 180, e) ||
        !number(arcs, "down", r.arcDown, 1, 180, e) || !number(arcs, "spin", r.arcSpin, 1, 180, e) || !number(arcs, "low", r.arcLow, 1, 180, e) ||
        !number(arcs, "push", r.arcPush, 1, 180, e))
        return fail("hit_arc_degrees: " + e);
    if (j.has("boxing")) {
        const Json& bx = j["boxing"];
        if (bx.has("head_movement") && (!number(bx["head_movement"], "bonus_per_charge", r.headMoveBonus, 0, 1, e) || !integer(bx["head_movement"], "max_charges", r.headMoveMaxCharges, 0, 20, e) ||
                                         !integer(bx["head_movement"], "decay_frames", r.headMoveDecayFrames, 1, 3600, e)))
            return fail("boxing.head_movement: " + e);
        if (bx.has("body_work") && (!number(bx["body_work"], "threshold", r.bodyWorkThreshold, 1, 1000, e) || !number(bx["body_work"], "decay_per_second", r.bodyWorkDecayPerSecond, 0, 100, e) ||
                                     !integer(bx["body_work"], "doubled_frames", r.bodyDoubledFrames, 1, 600, e)))
            return fail("boxing.body_work: " + e);
    }
    if (j.has("rhythm") && (!integer(j["rhythm"], "hits_for_flow", r.rhythmHitsForFlow, 1, 100, e) || !integer(j["rhythm"], "flow_frames", r.flowFrames, 1, 7200, e) ||
                            !integer(j["rhythm"], "decay_frames", r.rhythmDecayFrames, 1, 7200, e)))
        return fail("rhythm: " + e);
    if (!integer(j, "input_buffer_frames", r.inputBufferFrames, 0, 60, e) || !integer(j, "fps", r.fps, 30, 240, e)) return fail(e);

    if (!j["block_by_angle"].isArray() || j["block_by_angle"].size() == 0) return fail("block_by_angle — непустой массив");
    float previous = 0.f;
    for (const Json& step : j["block_by_angle"].items()) {
        const double deg = step["max_degrees"].asNumber(-1), factor = step["factor"].asNumber(-1);
        if (!(deg > previous && deg <= 180 && factor >= 0 && factor <= 1)) return fail("block_by_angle: углы по возрастанию до 180, доля 0..1");
        r.blockByAngle.push_back({static_cast<float>(deg), static_cast<float>(factor)});
        previous = static_cast<float>(deg);
    }

    if (!j["defenses"].isObject() || j["defenses"].size() == 0) return fail("defenses — непустой объект");
    static const char* columns[6] = {"straight", "arc_same", "arc_other", "up", "down", "low"};
    for (const auto& [name, def] : j["defenses"].members()) {
        DefenseDef dd;
        dd.name = name;
        const auto dfail = [&](const std::string& why) { return fail("защита «" + name + "»: " + why); };
        if (!def["covers"].isArray() || def["covers"].size() == 0) return dfail("covers — непустой список высот");
        for (const Json& h : def["covers"].items()) {
            Height height;
            if (!heightFromName(h.asString(), height)) return dfail("неизвестная высота «" + h.asString() + "»");
            dd.covers.push_back(height);
        }
        dd.hold = def["hold"].asBool(false);
        dd.sided = def["sided"].asBool(false);
        if (!integer(def, "startup", dd.startup, 0, 60, e) || !integer(def, "total", dd.total, 0, 120, e) || !integer(def, "perfect_until", dd.perfectUntil, -1, 60, e) ||
            !number(def, "cost", dd.cost, 0, 100, e) || !number(def, "shift", dd.shift, 0, 3, e))
            return dfail(e);
        if (!dd.hold) {
            if (dd.total <= 0) return dfail("у действия должна быть total > 0");
            if (def["window"].size() != 2) return dfail("window — два числа");
            dd.windowFrom = static_cast<int>(def["window"].at(0).asNumber(-1));
            dd.windowTo = static_cast<int>(def["window"].at(1).asNumber(-1));
            if (dd.windowFrom < 0 || dd.windowTo < dd.windowFrom || dd.windowTo > dd.total) return dfail("window должно лежать внутри total");
        }
        for (int c = 0; c < 6; ++c) {
            if (!def["outcomes"].has(columns[c])) return dfail(std::string("нет исхода для «") + columns[c] + "»");
            if (!parseOutcome(def["outcomes"][columns[c]].asString(), dd.outcomes[c])) return dfail(std::string("неверный исход «") + def["outcomes"][columns[c]].asString() + "» для «" + columns[c] + "»");
        }
        r.defenses[name] = std::move(dd);
    }
    *this = std::move(r);
    return true;
}

bool CombatRules::load(const std::string& file, std::string& error) {
    Json json;
    if (!core::loadJsonFile(file, json, error)) return false;
    return parse(json, error);
}

const DefenseDef* CombatRules::defense(const std::string& name) const {
    const auto it = defenses.find(name);
    return it == defenses.end() ? nullptr : &it->second;
}

float CombatRules::blockFactorForAngle(float degrees) const {
    degrees = std::fabs(degrees);
    for (const AngleStep& s : blockByAngle)
        if (degrees <= s.maxDegrees) return s.factor;
    return blockByAngle.empty() ? 1.f : blockByAngle.back().factor;
}

float CombatRules::hitArcDegrees(Trajectory t) const {
    switch (t) {
        case Trajectory::Straight: return arcStraight;
        case Trajectory::ArcLeft:
        case Trajectory::ArcRight: return arcArc;
        case Trajectory::Up: return arcUp;
        case Trajectory::Down: return arcDown;
        case Trajectory::Spin: return arcSpin;
        case Trajectory::Low: return arcLow;
        case Trajectory::Push: return arcPush;
    }
    return arcStraight;
}
}  // namespace han::combat
