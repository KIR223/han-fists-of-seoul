// Заглушка character_controller.h: персонаж стоит на месте на земле, идентификаторы выдаются по порядку.
#pragma once
#include <unordered_map>

#include "contracts/character_controller.h"

namespace han::contracts::stubs {
struct CharacterControllerStub final : ICharacterController {
    EntityId create(const CharacterDesc&, Vec3 position) override {
        states_[++next_] = {position, {}, true};
        return next_;
    }
    void destroy(EntityId id) override { states_.erase(id); }
    void setDesiredVelocity(EntityId, Vec3) override {}
    void applyImpulse(EntityId, Vec3) override {}
    CharacterState state(EntityId id) const override {
        auto it = states_.find(id);
        return it == states_.end() ? CharacterState{} : it->second;
    }

private:
    EntityId next_ = 0;
    std::unordered_map<EntityId, CharacterState> states_;
};
}  // namespace han::contracts::stubs
