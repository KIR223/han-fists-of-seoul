// contracts/skinning.h — поставщик: A (han/render), потребители: B (han/anim).
// Зачем: анимация считает палитру костей на CPU, рендер принимает её для GPU-скиннинга.
#pragma once
#include <cstdint>

#include "contracts/common.h"

namespace han::contracts {
struct BonePalette {
    const float* matrices4x4 = nullptr;  // boneCount матриц 4x4, float32, по строкам
    std::uint32_t boneCount = 0;
};

struct ISkinning {
    // Палитра копируется при вызове, можно отдавать из рабочего потока.
    virtual void submit(EntityId skinnedMesh, const BonePalette& palette) = 0;
    virtual void setVisible(EntityId skinnedMesh, bool visible) = 0;
    virtual ~ISkinning() = default;
};
}  // namespace han::contracts
// Пример: skinning.submit(hero, {pose.data(), 64});
