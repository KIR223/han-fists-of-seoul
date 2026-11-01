#pragma once
// Временное сглаживание (TAA, E4, часть 34.4 дизайна): подпиксельный дрожание камеры и матрицы для переотображения истории.
// Чисто CPU; сам проход разрешения — в постобработке рендерера.
#include "han/core/math.h"

namespace han::render {

constexpr int kTaaJitterPhases = 8;

// Сдвиг выборки внутри пикселя в долях пикселя, каждая компонента в (−0,5; 0,5). Последовательность Холтона (2, 3) из 8 фаз:
// ровно покрывает пиксель и повторяется с периодом 8 кадров.
struct Jitter {
    float x = 0, y = 0;
};
Jitter haltonJitter(int frameIndex);

// Матрица камеры со сдвигом на подпиксель: клип.xy += сдвиг * клип.w, то есть NDC смещается на 2·долю/размер.
core::Mat4 jitteredViewProjection(const core::Mat4& viewProjection, Jitter jitter, int width, int height);

// Матрица «клип этого кадра (без дрожания) → клип прошлого кадра (без дрожания)»: по ней пиксель находит себя в истории.
// false — матрицу текущего кадра нельзя обратить (тогда историю использовать нельзя).
bool reprojectionMatrix(const core::Mat4& currentViewProjection, const core::Mat4& previousViewProjection, core::Mat4& out);

}  // namespace han::render
// Пример: auto vp = jitteredViewProjection(camera.viewProjection(aspect), haltonJitter(frame), w, h);
