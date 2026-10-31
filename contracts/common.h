// contracts/common.h — общие типы контрактов. Поставщик: A, потребители: все.
// Зачем: контракты не тянут glm/Jolt/Vulkan; обмен идёт простыми структурами.
#pragma once
#include <cstdint>

namespace han::contracts {
using EntityId = std::uint32_t;  // 0 — «никто»
constexpr EntityId kNoEntity = 0;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};
struct Transform {
    Vec3 position;
    Quat rotation;
};
}  // namespace han::contracts
// Пример: contracts::Vec3 p{1, 0, 2};
