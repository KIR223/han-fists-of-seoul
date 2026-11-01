#pragma once
// Каскадные карты теней для направленного света (E4, часть 34.5 дизайна: каскады 15 / 50 / 150 м).
// Чисто CPU: по камере и направлению солнца считает матрицы «мир → карта теней» для каждого каскада. Рисование — в рендерере.
#include <array>

#include "han/core/math.h"

namespace han::render {

constexpr int kShadowCascades = 3;

struct ShadowCascade {
    core::Mat4 viewProjection;  // мир → клип карты теней (ортографическая, глубина 0..1, привязана к текселям)
    float splitDistance = 0;    // до какой глубины вдоль взгляда камеры (метры) каскад используется
    float radius = 0;           // радиус охватывающей сферы среза, метры
    float texelWorldSize = 0;   // размер одного текселя карты теней в метрах
};

struct ShadowCascades {
    std::array<ShadowCascade, kShadowCascades> cascade;
};

struct ShadowSettings {
    std::array<float, kShadowCascades> distances{15.0f, 50.0f, 150.0f};  // дальняя граница каждого каскада от камеры, метры
    int mapSize = 2048;           // сторона карты теней каждого каскада, пиксели
    float casterRange = 250.0f;   // насколько далеко от среза в сторону солнца ищутся отбрасывающие тень объекты, метры
};

// viewProjection — матрица камеры (мир → клип, как у FrameParams); nearPlane/farPlane — её плоскости отсечения;
// sunDirection — единичный вектор К солнцу. Дальние границы каскадов обрезаются по farPlane.
// Гарантии: все 8 углов среза каждого каскада лежат внутри его клипа; сфера охвата не зависит от поворота камеры;
// сдвиг карты привязан к текселям, поэтому тени не «плывут» при движении камеры.
ShadowCascades computeShadowCascades(const core::Mat4& viewProjection, float nearPlane, float farPlane, core::Vec3 sunDirection,
                                     const ShadowSettings& settings);

}  // namespace han::render
// Пример: auto cascades = computeShadowCascades(camera.viewProjection(aspect), camera.nearZ, camera.farZ, sun.direction, {});
