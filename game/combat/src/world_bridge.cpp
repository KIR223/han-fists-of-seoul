#include "han/combat/world_bridge.h"

namespace han::combat {
int WorldBridge::addFighter(contracts::EntityId entity, const FighterStats& stats) {
    const contracts::Transform t = world_.transformOf(entity);
    const int i = sim_.addFighter({t.position.x, t.position.z}, stats);
    entities_.push_back(entity);
    return i;
}

void WorldBridge::pullPositions() {
    for (int i = 0; i < sim_.fighterCount(); ++i) {
        const contracts::Transform t = world_.transformOf(entities_[static_cast<std::size_t>(i)]);
        sim_.fighterMutable(i).pos = {t.position.x, t.position.z};
    }
}

bool WorldBridge::sacred(int fighter) const {
    const contracts::Transform t = world_.transformOf(entities_[static_cast<std::size_t>(fighter)]);
    return world_.isSacredAt(t.position);
}

void WorldBridge::step(std::vector<FighterInput> inputs) {
    pullPositions();
    inputs.resize(entities_.size());
    // Проверяем и текущий приём, и буфер даже при пустом вводе: границу можно пересечь посреди замаха.
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        Fighter& f = sim_.fighterMutable(static_cast<int>(i));
        const int target = f.target;
        if (sacred(static_cast<int>(i)) || (target >= 0 && sacred(target))) {
            if (inputs[i].attack || f.buffered || f.state == FighterState::Attacking) ++muted_;
            inputs[i].attack = nullptr;
            f.buffered = nullptr;
            f.bufferAge = 0;
            if (f.state == FighterState::Attacking) {
                f.move = nullptr;
                f.moveResolved = false;
                f.state = FighterState::Idle;
                f.stateFrame = 0;
            }
        }
    }
    sim_.step(inputs);
    for (const CombatEvent& e : sim_.events()) {
        if (e.defender < 0 || e.impulse <= 0.f) continue;
        if (e.type != CombatEventType::Hit && e.type != CombatEventType::Blocked && e.type != CombatEventType::Knockdown) continue;
        const float k = e.impulse * impulseScale_;
        world_.applyImpulse(entities_[static_cast<std::size_t>(e.defender)], {e.direction.x * k, 0.f, e.direction.z * k});
    }
    for (int i = 0; i < sim_.fighterCount(); ++i) {
        const Fighter& f = sim_.fighter(i);
        world_.setFacing(entities_[static_cast<std::size_t>(i)], {f.facing.x, 0.f, f.facing.z});
    }
}
}  // namespace han::combat
