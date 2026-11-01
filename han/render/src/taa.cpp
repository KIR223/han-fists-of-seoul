#include "han/render/taa.h"

namespace han::render {
namespace {

float halton(int index, int base) {
    float fraction = 1.0f, result = 0.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

}  // namespace

Jitter haltonJitter(int frameIndex) {
    const int phase = (frameIndex % kTaaJitterPhases + kTaaJitterPhases) % kTaaJitterPhases + 1;  // с 1: нулевой индекс даёт (0, 0)
    return {halton(phase, 2) - 0.5f, halton(phase, 3) - 0.5f};
}

core::Mat4 jitteredViewProjection(const core::Mat4& viewProjection, Jitter jitter, int width, int height) {
    if (width <= 0 || height <= 0) return viewProjection;
    const core::Vec3 ndcShift{2.0f * jitter.x / static_cast<float>(width), 2.0f * jitter.y / static_cast<float>(height), 0.0f};
    return core::Mat4::translation(ndcShift) * viewProjection;  // translation на однородных координатах клипа прибавляет сдвиг·w
}

bool reprojectionMatrix(const core::Mat4& currentViewProjection, const core::Mat4& previousViewProjection, core::Mat4& out) {
    core::Mat4 inverse;
    if (!core::invert(currentViewProjection, inverse)) return false;
    out = previousViewProjection * inverse;
    return true;
}

}  // namespace han::render
