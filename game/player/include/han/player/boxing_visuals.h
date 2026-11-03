#pragma once
#include <map>
#include <string>
#include "han/combat/sim.h"

namespace han::player {
struct VisualSample {
    std::string clip;
    float normalizedTime = 0;
    float fadeSeconds = .08f;
    bool loop = false;
    std::string lowerBodyClip;
    float lowerBodyPhase = 0, lowerBodyWeight = 0;
    bool aimHand = false, leftHand = false;
    float aimWeight = 0, targetHeight = 0, contactRadius = 0;
};
struct BoxingMotion {
    combat::Vec2 previousPosition;
    float gaitPhase = 0, forward = 0, right = 0, speed = 0;
    float downSeconds = 0;
    float locomotionWeight = 0;
    bool initialized = false, wasDown = false;
    std::string reaction = "Boxing_HitHeadFront", fallSide = "Back";
};
// Presentation-only: не меняет CombatSim, хитбоксы, кадры и урон.
class BoxingVisuals {
public:
    bool load(const std::string& path, std::string& error);
    void advance(BoxingMotion& motion, const combat::Fighter& fighter, const combat::CombatRules& rules,
                 const std::vector<combat::CombatEvent>& events) const;
    VisualSample choose(const combat::Fighter& fighter, const combat::CombatRules& rules, float locomotionSeconds, bool moving,
                        const BoxingMotion* motion = nullptr) const;
private:
    std::map<std::string,std::string> moves_, defenses_;
    std::map<std::string,std::vector<float>> contacts_;
    float contact_ = .4f, activeEnd_ = .46f, fade_ = .08f, attackFade_ = .025f;
    float cycleDistance_ = .6f, speedDeadzone_ = .08f, fallSeconds_ = .85f, staggerDamage_ = 14.f, guardSeconds_ = 2.8f;
    float headContact_ = 1.52f, bodyContact_ = 1.06f, contactRadius_ = .24f, aimWeight_ = .85f;
};
}  // namespace han::player
