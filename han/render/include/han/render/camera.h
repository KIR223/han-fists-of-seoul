#pragma once
// Свободная камера (E2): позиция, рыскание и тангаж. Не зависит от Vulkan.
#include "han/core/math.h"

namespace han::render {

class Camera {
public:
    core::Vec3 position{0, 2, 6};
    float yaw = 0;    // радианы; 0 — смотрит вдоль -Z, положительный — поворот вправо
    float pitch = 0;  // радианы; положительный — вверх, ограничен ±~89°
    float fovY = 1.0f, nearZ = 0.1f, farZ = 2000.0f;

    core::Vec3 forward() const;
    core::Vec3 right() const;

    // Мышь: dx вправо — поворот вправо, dy вниз — взгляд вниз. sensitivity — радиан на пиксель.
    void look(float dx, float dy, float sensitivity = 0.0025f);
    // Движение в осях камеры: вперёд, вправо, вверх (мировая ось Y), каждый −1..1; speed в м/с.
    void move(float forwardInput, float rightInput, float upInput, float speed, float dt);

    core::Mat4 view() const { return core::Mat4::lookDirection(position, forward()); }
    core::Mat4 projection(float aspect) const { return core::Mat4::perspective(fovY, aspect, nearZ, farZ); }
    core::Mat4 viewProjection(float aspect) const { return projection(aspect) * view(); }
};

}  // namespace han::render
// Пример: camera.look(ev.mouseDx, ev.mouseDy); camera.move(w - s, d - a, 0, 5.f, dt); auto vp = camera.viewProjection(aspect);
