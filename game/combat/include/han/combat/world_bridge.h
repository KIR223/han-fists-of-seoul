// han/combat/world_bridge.h — мост боевого ядра и мира через contracts::ICombatWorld (G1).
// Ядро (CombatSim) не знает ни о физике, ни о святых местах: мост читает позиции бойцов из мира, глушит атаки в святых местах,
// передаёт миру отброс от ударов и доворот лицом к цели. Реализация мира — от A (в тестах и без движка — заглушка).
#pragma once
#include <vector>

#include "contracts/combat_world.h"
#include "han/combat/sim.h"

namespace han::combat {
class WorldBridge {
public:
    WorldBridge(CombatSim& sim, contracts::ICombatWorld& world) : sim_(sim), world_(world) {}

    // Привязывает бойца ядра к сущности мира; возвращает индекс бойца.
    int addFighter(contracts::EntityId entity, const FighterStats& stats = {});
    contracts::EntityId entityOf(int fighter) const { return entities_[static_cast<std::size_t>(fighter)]; }

    // Позиции бойцов берутся из мира (после шага физики).
    void pullPositions();
    // Один кадр: позиции из мира, глушение атак в святых местах, шаг ядра, отброс и доворот в мир.
    void step(std::vector<FighterInput> inputs);

    // Боец сейчас в святом месте (атака ему недоступна, по нему не бьют).
    bool sacred(int fighter) const;
    int mutedAttacks() const { return muted_; }  // сколько атак отменено из-за святого места

    // Множитель отброса: физический импульс = направление × impulse × scale.
    void setImpulseScale(float s) { impulseScale_ = s; }

private:
    CombatSim& sim_;
    contracts::ICombatWorld& world_;
    std::vector<contracts::EntityId> entities_;
    float impulseScale_ = 0.9f;
    int muted_ = 0;
};
}  // namespace han::combat
// Пример: WorldBridge bridge(sim, world); int a = bridge.addFighter(1); bridge.step({input});
