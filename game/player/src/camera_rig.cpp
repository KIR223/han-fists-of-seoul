#include "han/player/camera_rig.h"

#include <algorithm>
#include <cmath>

namespace han::player {
using contracts::Vec3;

namespace {
Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
float damp(float sharpness, float dt) { return 1.f - std::exp(-sharpness * dt); }  // независимо от частоты кадров
}  // namespace

Vec3 CameraRig::forward() const { return {std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), -std::cos(yaw_) * std::cos(pitch_)}; }

void CameraRig::look(float dx, float dy) {
    yaw_ += dx * s_.sensitivity;
    pitch_ -= dy * s_.sensitivity;  // мышь вниз — взгляд вниз
    pitch_ = std::clamp(pitch_, s_.minPitch, s_.maxPitch);
    const float twoPi = 6.2831853f;
    if (yaw_ > twoPi || yaw_ < -twoPi) yaw_ = std::fmod(yaw_, twoPi);
}

void CameraRig::setAngles(float yaw, float pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, s_.minPitch, s_.maxPitch);
}

Vec3 CameraRig::desiredPosition(Vec3 pivot) const {
    const Vec3 back = mul(forward(), -distance_);
    const Vec3 right{std::cos(yaw_), 0.f, std::sin(yaw_)};
    return add(add(pivot, back), mul(right, s_.shoulderOffset));
}

// Камера на луче от точки опоры к желаемой позиции; упёрлась в геометрию — подтягиваемся ближе с отступом.
Vec3 CameraRig::resolve(Vec3 pivot, Vec3 desired, const contracts::IPhysicsQuery* query) {
    if (!query) return desired;
    const Vec3 delta = sub(desired, pivot);
    const float len = length(delta);
    if (len < 1e-4f) return desired;
    const contracts::RayHit hit = query->raycast(pivot, desired);
    if (!hit.hit) return desired;
    const float safe = std::max(s_.minDistance, hit.distance - s_.collisionMargin);
    return add(pivot, mul(delta, std::min(safe, len) / len));
}

void CameraRig::snapTo(Vec3 feet, const contracts::IPhysicsQuery* query) {
    pivot_ = {feet.x, feet.y + s_.pivotHeight, feet.z};
    position_ = resolve(pivot_, desiredPosition(pivot_), query);
    fov_ = s_.baseFov;
    initialized_ = true;
}

void CameraRig::update(float dt, Vec3 feet, float speedFraction, const contracts::IPhysicsQuery* query) {
    const Vec3 targetPivot{feet.x, feet.y + s_.pivotHeight, feet.z};
    if (!initialized_) {
        snapTo(feet, query);
        return;
    }
    // Точка опоры плавно догоняет персонажа (ступени и прыжки не трясут камеру), поворот — без задержки.
    const float k = damp(s_.followSharpness, dt);
    pivot_ = add(pivot_, mul(sub(targetPivot, pivot_), k));
    const Vec3 target = resolve(pivot_, desiredPosition(pivot_), query);
    // Приближение к стене — мгновенное (нельзя оказаться внутри), отдаление — плавное.
    const Vec3 offsetNow = sub(position_, pivot_), offsetTarget = sub(target, pivot_);
    if (length(offsetTarget) < length(offsetNow)) position_ = target;
    else position_ = add(position_, mul(sub(target, position_), damp(s_.followSharpness * 0.6f, dt)));
    const float fovTarget = s_.baseFov + s_.sprintFovBonus * std::clamp(speedFraction, 0.f, 1.f);
    fov_ += (fovTarget - fov_) * damp(s_.fovSharpness, dt);
}
}  // namespace han::player
