// contracts/physics_query.h — поставщик: A, потребители: B (бой на местности, добивания у стен).
// Зачем: лучи, свипы и оверлапы по миру без знания о Jolt; материал поверхности — для звука и эффектов.
#pragma once
#include <cstdint>

#include "contracts/common.h"

namespace han::contracts {
enum class SurfaceMaterial : std::uint8_t { Unknown, Asphalt, Concrete, Wood, Metal, Glass, Grass, Dirt, Water, Tile, Carpet };

struct RayHit {
    bool hit = false;
    float distance = 0;
    Vec3 point, normal;
    EntityId entity = kNoEntity;
    SurfaceMaterial material = SurfaceMaterial::Unknown;
};

struct IPhysicsQuery {
    virtual RayHit raycast(Vec3 from, Vec3 to, std::uint32_t layerMask = ~0u) const = 0;
    virtual RayHit sweepCapsule(Vec3 from, Vec3 to, float radius, float height, std::uint32_t layerMask = ~0u) const = 0;
    // Записывает до maxOut идентификаторов в out; возвращает полное число найденных.
    virtual std::uint32_t overlapSphere(Vec3 center, float radius, EntityId* out, std::uint32_t maxOut,
                                        std::uint32_t layerMask = ~0u) const = 0;
    virtual ~IPhysicsQuery() = default;
};
}  // namespace han::contracts
// Пример: auto h = world.physics().raycast(eye, forwardPoint); if (h.hit && h.material == SurfaceMaterial::Glass) ...
