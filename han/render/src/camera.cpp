#include "han/render/camera.h"

#include <algorithm>
#include <cmath>

namespace han::render {

core::Vec3 Camera::forward() const {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

core::Vec3 Camera::right() const { return {std::cos(yaw), 0, std::sin(yaw)}; }

void Camera::look(float dx, float dy, float sensitivity) {
    yaw += dx * sensitivity;
    pitch = std::clamp(pitch - dy * sensitivity, -1.55f, 1.55f);
}

void Camera::move(float forwardInput, float rightInput, float upInput, float speed, float dt) {
    const core::Vec3 delta = forward() * forwardInput + right() * rightInput + core::Vec3{0, upInput, 0};
    position = position + delta * (speed * dt);
}

}  // namespace han::render
