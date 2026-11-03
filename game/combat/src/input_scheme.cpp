#include "han/combat/input_scheme.h"

#include <cmath>

namespace han::combat {
SimpleScheme::SimpleScheme(const MoveLibrary& moves, const std::string& style, const SchemeSettings& settings) : moves_(moves), style_(style), s_(settings) {}

const MoveData* SimpleScheme::find(const char* suffix) const { return moves_.byId(style_ + "_" + suffix); }

FighterInput SimpleScheme::map(const RawInput& raw, const CombatSim& sim, int self) {
    FighterInput out;
    const Fighter& me = sim.fighter(self);
    const Fighter* foe = me.target >= 0 ? &sim.fighter(me.target) : nullptr;
    const int frame = sim.frame();
    out.forward = raw.forward;
    out.right = raw.right;
    out.dash = raw.dash;
    out.blockHeld = raw.rmbHeld || raw.rmbPressed;
    lastAttack_ = nullptr;
    lastDefense_ = DefenseAction::None;

    // Ритм: пауза дольше порога — серия начинается заново.
    if (frame - lastAttackFrame_ > s_.chainResetFrames) chain_ = 0;

    // Защита: ПКМ вовремя — парирование; Space — уклон, выбранный по летящему удару.
    if (raw.rmbPressed && !raw.lmbPressed) {
        out.defense = DefenseAction::Parry;
        lastDefense_ = out.defense;
    } else if (raw.shoulderPressed) {
        out.defense = raw.right < -0.5f ? DefenseAction::ShoulderRollLeft : DefenseAction::ShoulderRollRight;
        lastDefense_ = out.defense;
    } else if (raw.duckPressed) {
        out.defense=DefenseAction::Duck;lastDefense_=out.defense;
    } else if (raw.spacePressed) {
        const bool left = raw.right < -0.5f, right = raw.right > 0.5f, back = raw.forward < -0.5f;
        const MoveData* threat = (foe && foe->state == FighterState::Attacking && foe->move && foe->stateFrame < foe->move->startup) ? foe->move : nullptr;
        DefenseAction d = DefenseAction::StepBack;  // без направления и угрозы — отскок назад
        if (threat) {
            switch (threat->trajectory) {
                case Trajectory::Straight:
                case Trajectory::Push: d = left ? DefenseAction::SlipLeft : (right ? DefenseAction::SlipRight : DefenseAction::SlipLeft); break;
                case Trajectory::ArcLeft:
                case Trajectory::ArcRight:
                case Trajectory::Spin: d = back ? DefenseAction::StepBack : DefenseAction::Duck; break;
                case Trajectory::Up: d = DefenseAction::LeanBack; break;
                case Trajectory::Down: d = right ? DefenseAction::SidestepRight : DefenseAction::SidestepLeft; break;
                case Trajectory::Low: d = DefenseAction::StepBack; break;
            }
        } else if (left) {
            d = DefenseAction::SidestepLeft;
        } else if (right) {
            d = DefenseAction::SidestepRight;
        } else if (!back) {
            d = DefenseAction::StepBack;
        }
        out.defense = d;
        lastDefense_ = d;
    }
    if(lastDefense_!=DefenseAction::None) {recentDefense_=lastDefense_;recentDefenseFrame_=frame;}

    // Удержание ЛКМ: сильный удар один раз за удержание.
    holdFrames_ = raw.lmbHeld ? holdFrames_ + 1 : 0;
    if (!raw.lmbHeld) heavyFired_ = false;
    const bool wantHeavy = raw.lmbHeld && holdFrames_ >= s_.heavyHoldFrames && !heavyFired_;

    const MoveData* choice = nullptr;
    if (raw.clinchPressed) {
        choice=find("clinch");
    } else if (raw.specialPressed) {
        choice = find(raw.forward<-.5f?"liver_knockout":raw.right>.5f?"bolo_punch":"dempsey_roll");
        if (choice && me.focus < choice->focus) choice = nullptr;
    } else if (wantHeavy) {
        choice = find("overhand");
        heavyFired_ = true;
    } else if (raw.lmbPressed) {
        // Направление меняет ветку комбо: в сторону — боковые, от врага — удар с отходом, ПКМ вместе — по корпусу.
        const bool body = raw.rmbHeld;
        if(frame-recentDefenseFrame_<=s_.chainResetFrames && recentDefense_!=DefenseAction::None) {
            if(recentDefense_==DefenseAction::Duck || recentDefense_==DefenseAction::BobLeft || recentDefense_==DefenseAction::BobRight)choice=find("duck_hook");
            else if(recentDefense_==DefenseAction::StepBack || recentDefense_==DefenseAction::LeanBack)choice=find("pull_counter");
            else choice=find("slip_counter");
            recentDefense_=DefenseAction::None;
        } else if(raw.alternate) choice=find("rear_uppercut");
        else if(raw.dash && raw.forward>.5f) choice=find("double_jab_step");
        else if(raw.dash && raw.right<-.5f) choice=find("pivot_hook");
        else if (raw.right < -0.5f) choice = find(body ? "liver_hook" : "left_hook");
        else if (raw.right > 0.5f) choice = find(body ? "body_uppercut" : "right_hook");
        else if (raw.forward < -0.5f) choice = find("jab_retreat");
        else if (body) choice = find("jab_body");
        else {
            // Серия по ритму: джеб, кросс, хук, апперкот.
            static const char* chain[] = {"jab", "cross", "left_hook", "uppercut"};
            choice = find(chain[chain_ % 4]);
            ++chain_;
        }
    }
    if (choice) {
        out.attack = choice;
        out.blockHeld = false;
        lastAttack_ = choice;
        lastAttackFrame_ = frame;
    }
    return out;
}
}  // namespace han::combat
