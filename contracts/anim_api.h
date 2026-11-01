// contracts/anim_api.h — поставщик: B (han/anim), потребители: A (скиннинг, толпа, редактор).
// Зачем: проиграть клип, задать параметры смешивания, цели motion warping и LOD для героя, бойцов и толпы.
#pragma once
#include <cstdint>

#include "contracts/common.h"

namespace han::contracts {
using ClipId = std::uint32_t;

struct AnimEvent {
    EntityId entity = kNoEntity;
    std::uint32_t nameHash = 0;
    float time = 0;
};

struct IAnimation {
    virtual void play(EntityId e, ClipId clip, float blendSeconds = 0.1f) = 0;
    virtual void setParam(EntityId e, std::uint32_t paramHash, float value) = 0;  // скорость, направление, наклон
    virtual void setWarpTarget(EntityId e, Transform target) = 0;                 // подгон удара или добивания под цель
    virtual void setLod(EntityId e, std::uint8_t lod) = 0;                        // 0 — полная, 3 — упрощённая (толпа)
    virtual ~IAnimation() = default;
};
}  // namespace han::contracts
// Пример: anim.play(npc, kClipWalk); anim.setParam(npc, speedHash, 1.4f);
