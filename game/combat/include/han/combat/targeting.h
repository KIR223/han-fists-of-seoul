// han/combat/targeting.h — выбор цели в групповой драке (G1): кто кому враг, ближайший живой, удержание цели, приоритет по направлению игрока.
// Ядро бьёт только по текущей цели бойца (Fighter::target), поэтому цель обновляется до CombatSim::step и не меняется посреди своего приёма.
#pragma once
#include <vector>

#include "han/combat/sim.h"

namespace han::combat {
class TargetSelector {
public:
    // switchMargin — насколько новая цель должна быть «лучше» текущей (м), чтобы боец переключился (гистерезис).
    // aimWeight — сколько метров «скидки» даёт цель точно по направлению прицела игрока.
    explicit TargetSelector(float switchMargin = 0.7f, float aimWeight = 2.0f) : margin_(switchMargin), aimWeight_(aimWeight) {}

    void setTeam(int fighter, int team);
    int team(int fighter) const;
    // Направление прицела бойца на ближайший update (мировые x, z; ввод игрока или взгляд камеры).
    void setAim(int fighter, Vec2 dir);
    // Обновляет цели всех бойцов; вызывать каждый кадр до CombatSim::step.
    void update(CombatSim& sim);
    // Сколько живых врагов в радиусе r вокруг бойца.
    int enemiesNear(const CombatSim& sim, int fighter, float r) const;
    // Все живые бойцы чужой команды побеждены.
    bool teamDefeated(const CombatSim& sim, int team) const;

private:
    struct Aim {
        int fighter;
        Vec2 dir;
    };
    float margin_, aimWeight_;
    std::vector<int> teams_;
    std::vector<Aim> aims_;
    float score(const CombatSim& sim, int self, int cand) const;
};
}  // namespace han::combat
// Пример: TargetSelector ts; ts.setTeam(0, 0); ts.setTeam(1, 1); ts.setTeam(2, 1); ts.setAim(0, {0, -1}); ts.update(sim);
