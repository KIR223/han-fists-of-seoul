#include "han/render/shadow.h"

#include <algorithm>
#include <cmath>

namespace han::render {
namespace {

core::Vec3 unproject(const core::Mat4& inverse, float x, float y, float z) {
    const core::Vec4 p = core::transform(inverse, {x, y, z});
    return {p.x / p.w, p.y / p.w, p.z / p.w};
}

}  // namespace

ShadowCascades computeShadowCascades(const core::Mat4& viewProjection, float nearPlane, float farPlane, core::Vec3 sunDirection,
                                     const ShadowSettings& settings) {
    ShadowCascades result;
    core::Mat4 inverse;
    if (!core::invert(viewProjection, inverse) || !(farPlane > nearPlane) || settings.mapSize <= 0) return result;

    // Углы среза на глубине d вдоль взгляда. Разворачиваем их прямо на NDC-глубине этого среза: углы дальней плоскости (2000 м при
    // ближней 0,1 м) из обратной матрицы получаются с ошибкой в метры из-за точности float, а интерполяция потом её увеличивает.
    // NDC-глубина для нашей перспективной проекции (Mat4::perspective): z = far·(d − near) / (d·(far − near)).
    const float xs[4] = {-1, 1, -1, 1}, ys[4] = {-1, -1, 1, 1};
    const auto cornersAt = [&](float d, core::Vec3* out) {
        const float ndcZ = farPlane * (d - nearPlane) / (d * (farPlane - nearPlane));
        for (int k = 0; k < 4; ++k) out[k] = unproject(inverse, xs[k], ys[k], ndcZ);
    };

    // Свет смотрит от солнца к сцене. Если он почти вертикален, опорная ось «вверх» заменяется, иначе базис вырождается.
    const core::Vec3 lightForward = core::normalize(core::Vec3{-sunDirection.x, -sunDirection.y, -sunDirection.z});
    const core::Vec3 up = std::fabs(lightForward.y) > 0.99f ? core::Vec3{0, 0, 1} : core::Vec3{0, 1, 0};
    const core::Mat4 lightRotation = core::Mat4::lookDirection({0, 0, 0}, lightForward, up);

    float previous = nearPlane;
    for (int i = 0; i < kShadowCascades; ++i) {
        const float split = std::clamp(settings.distances[static_cast<std::size_t>(i)], previous + 0.01f, farPlane);
        core::Vec3 corners[8];
        core::Vec3 center{};
        cornersAt(previous, corners);
        cornersAt(split, corners + 4);
        for (const core::Vec3& c : corners) center = center + c;
        center = center * (1.0f / 8.0f);

        float radius = 0;
        for (const core::Vec3& c : corners) radius = std::max(radius, core::length(c - center));
        // Радиус округляется (стабильность при повороте) и увеличивается на один тексель: сдвиг к сетке текселей не должен
        // выводить углы среза за край карты.
        radius = std::ceil(radius * 16.0f) / 16.0f;
        radius *= 1.0f + 2.0f / static_cast<float>(settings.mapSize);
        const float texel = 2.0f * radius / static_cast<float>(settings.mapSize);

        // Центр среза в системе света (начало — мировое начало): привязываем x, y к сетке текселей.
        const core::Vec4 centerLight = core::transform(lightRotation, center);
        const float snappedX = std::round(centerLight.x / texel) * texel;
        const float snappedY = std::round(centerLight.y / texel) * texel;

        // Глубина вдоль света: срез занимает [z - r, z + r] (вид смотрит вдоль -Z); тень отбрасывают и объекты ближе к солнцу.
        const float nearDepth = -(centerLight.z + radius) - settings.casterRange;
        const float farDepth = -(centerLight.z - radius);
        const core::Mat4 projection =
            core::Mat4::orthographic(snappedX - radius, snappedX + radius, snappedY - radius, snappedY + radius, nearDepth, farDepth);

        ShadowCascade& out = result.cascade[static_cast<std::size_t>(i)];
        out.viewProjection = projection * lightRotation;
        out.splitDistance = split;
        out.radius = radius;
        out.texelWorldSize = texel;
        previous = split;
    }
    return result;
}

}  // namespace han::render
