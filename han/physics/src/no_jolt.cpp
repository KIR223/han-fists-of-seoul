// Запасная часть модуля: без Jolt (нет vcpkg-функции physics) физического мира нет, hasJolt() возвращает false.
// Код, вызывающий PhysicsWorld, оборачивают в #ifdef HAN_HAS_JOLT (макрос задаёт CMake модуля).
#include "han/physics/physics_world.h"

namespace han::physics {
bool hasJolt() { return false; }
}  // namespace han::physics
