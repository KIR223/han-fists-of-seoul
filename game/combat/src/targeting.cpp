#include "han/combat/targeting.h"

#include <cmath>

namespace han::combat {
void TargetSelector::setTeam(int fighter, int team) {
    if (fighter < 0) return;
    if (static_cast<std::size_t>(fighter) >= teams_.size()) teams_.resize(static_cast<std::size_t>(fighter) + 1, 0);
    teams_[static_cast<std::size_t>(fighter)] = team;
}

int TargetSelector::team(int fighter) const {
    return fighter >= 0 && static_cast<std::size_t>(fighter) < teams_.size() ? teams_[static_cast<std::size_t>(fighter)] : 0;
}

void TargetSelector::setAim(int fighter, Vec2 dir) { aims_.push_back({fighter, dir}); }

float TargetSelector::score(const CombatSim& sim, int self, int cand) const {
    const Fighter& a = sim.fighter(self);
    const Fighter& b = sim.fighter(cand);
    const float dx = b.pos.x - a.pos.x, dz = b.pos.z - a.pos.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    float s = dist;
    for (const Aim& aim : aims_) {
        if (aim.fighter != self) continue;
        const float al = std::sqrt(aim.dir.x * aim.dir.x + aim.dir.z * aim.dir.z);
        if (al < 1e-4f || dist < 1e-4f) break;
        const float cosA = (aim.dir.x * dx + aim.dir.z * dz) / (al * dist);
        s -= aimWeight_ * cosA;  // цель по прицелу — ближе «на вид», цель за спиной — дальше
        break;
    }
    return s;
}

void TargetSelector::update(CombatSim& sim) {
    for (int i = 0; i < sim.fighterCount(); ++i) {
        const Fighter& f = sim.fighter(i);
        if (!f.alive()) continue;
        // Посреди своего приёма или лёжа цель не меняется: удар уже летит в выбранного.
        if (f.state == FighterState::Attacking || f.down()) continue;
        int best = -1;
        float bestScore = 0.f;
        for (int j = 0; j < sim.fighterCount(); ++j) {
            if (j == i || !sim.fighter(j).alive() || team(j) == team(i)) continue;
            const float s = score(sim, i, j);
            if (best < 0 || s < bestScore) {
                best = j;
                bestScore = s;
            }
        }
        const int cur = f.target;
        const bool curValid = cur >= 0 && sim.fighter(cur).alive() && team(cur) != team(i);
        if (best < 0) {
            sim.setTarget(i, -1);
        } else if (!curValid) {
            sim.setTarget(i, best);
        } else if (best != cur && bestScore + margin_ < score(sim, i, cur)) {
            sim.setTarget(i, best);
        }
    }
    aims_.clear();
}

int TargetSelector::enemiesNear(const CombatSim& sim, int fighter, float r) const {
    int n = 0;
    for (int j = 0; j < sim.fighterCount(); ++j) {
        if (j == fighter || !sim.fighter(j).alive() || team(j) == team(fighter)) continue;
        if (sim.distance(fighter, j) <= r) ++n;
    }
    return n;
}

bool TargetSelector::teamDefeated(const CombatSim& sim, int t) const {
    for (int i = 0; i < sim.fighterCount(); ++i)
        if (team(i) == t && sim.fighter(i).alive()) return false;
    return true;
}
}  // namespace han::combat
