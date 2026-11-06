#include "han/render/vehicle_pose.h"

#include <algorithm>
#include <cmath>

namespace han::render {

VehiclePose solveVehiclePose(const WheelHeights& w, float wheelbase, float track, const VehiclePoseLimits& limits) {
    VehiclePose pose;
    const float base = std::max(wheelbase, 0.1f), width = std::max(track, 0.1f);
    const float front = 0.5f * (w.frontLeft + w.frontRight), rear = 0.5f * (w.rearLeft + w.rearRight);
    const float right = 0.5f * (w.frontRight + w.rearRight), left = 0.5f * (w.frontLeft + w.rearLeft);
    pose.pitch = std::clamp(std::atan2(front - rear, base), -limits.maxPitch, limits.maxPitch);
    pose.roll = std::clamp(std::atan2(right - left, width), -limits.maxRoll, limits.maxRoll);
    // Вертикальные смещения колёс от начала модели при найденных шаге и крене: колесо i на (±track/2, 0, ∓wheelbase/2) поворачивается Rx*Rz.
    const float cp = std::cos(pose.pitch), sp = std::sin(pose.pitch), sr = std::sin(pose.roll);
    const auto lift = [&](float x, float z) {
        // Rz: (x, 0, z) -> (x*cr, x*sr, z); Rx: y' = y*cp - z*sp.
        const float yAfterRoll = x * sr;
        return yAfterRoll * cp - z * sp;
    };
    const float hx = width * 0.5f, hz = base * 0.5f;
    pose.y = std::max({w.frontLeft - lift(-hx, -hz), w.frontRight - lift(hx, -hz), w.rearLeft - lift(-hx, hz), w.rearRight - lift(hx, hz)});
    return pose;
}

core::Mat4 vehicleTransform(float x, float z, float heading, const VehiclePose& pose, float lift) {
    return core::Mat4::translation({x, pose.y + lift, z}) * core::Mat4::rotationY(-heading) * core::Mat4::rotationX(pose.pitch) * core::Mat4::rotationZ(pose.roll);
}

VehiclePose VehiclePoseFilter::update(const VehiclePose& target, float dt) {
    if (!started_ || !(dt > 0.f)) {
        started_ = true;
        value_ = target;
        return value_;
    }
    const float k = tau_ > 1e-4f ? 1.f - std::exp(-dt / tau_) : 1.f;
    value_.y += (target.y - value_.y) * k;
    value_.pitch += (target.pitch - value_.pitch) * k;
    value_.roll += (target.roll - value_.roll) * k;
    return value_;
}

WheelAnimation advanceWheels(const WheelAnimation& previous, float speed, float yawRate, float dt, float wheelRadius, float wheelbase, float maxSteer) {
    WheelAnimation next = previous;
    constexpr float kTwoPi = 6.28318530718f;
    if (wheelRadius > 1e-3f && dt > 0.f) {
        next.rollAngle = std::fmod(previous.rollAngle + speed * dt / wheelRadius, kTwoPi);
        if (next.rollAngle < 0.f) next.rollAngle += kTwoPi;
    }
    // Курс города растёт к востоку (по часовой стрелке при взгляде сверху): положительная скорость курса — поворот направо.
    const float v = std::max(std::fabs(speed), 0.5f);  // у остановки поворот не растёт до бесконечности
    next.steer = std::clamp(std::atan(wheelbase * yawRate / v), -maxSteer, maxSteer);
    return next;
}

}  // namespace han::render
