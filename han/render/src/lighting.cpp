#include "han/render/lighting.h"

#include <algorithm>
#include <cmath>

namespace han::render {
namespace {

constexpr float kPi = 3.14159265358979f;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float smoothstep(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
core::Vec3 lerp(core::Vec3 a, core::Vec3 b, float t) { return a + (b - a) * t; }

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

}  // namespace

core::Vec3 colorFromTemperature(float kelvin) {
    // Приближение чёрного тела (Tanner Helland), точность достаточна для света и неба.
    const float t = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    float r = t <= 66.0f ? 255.0f : 329.698727446f * std::pow(t - 60.0f, -0.1332047592f);
    float g = t <= 66.0f ? 99.4708025861f * std::log(t) - 161.1195681661f : 288.1221695283f * std::pow(t - 60.0f, -0.0755148492f);
    float b = t >= 66.0f ? 255.0f : (t <= 19.0f ? 0.0f : 138.5177312231f * std::log(t - 10.0f) - 305.0447927307f);
    core::Vec3 c{srgbToLinear(std::clamp(r, 0.0f, 255.0f) / 255.0f), srgbToLinear(std::clamp(g, 0.0f, 255.0f) / 255.0f),
                 srgbToLinear(std::clamp(b, 0.0f, 255.0f) / 255.0f)};
    const float peak = std::max({c.x, c.y, c.z, 1e-6f});
    return c * (1.0f / peak);
}

SunLight makeSun(float elevationDegrees, float azimuthDegrees, float kelvin, float noonIlluminance) {
    const float el = elevationDegrees * kPi / 180.0f, az = azimuthDegrees * kPi / 180.0f;
    SunLight sun;
    sun.direction = core::normalize({std::cos(el) * std::sin(az), std::sin(el), -std::cos(el) * std::cos(az)});
    sun.color = colorFromTemperature(kelvin);
    // Ниже горизонта прямого света нет; у горизонта он слабее из-за толщи атмосферы.
    sun.illuminance = noonIlluminance * smoothstep(-4.0f, 12.0f, elevationDegrees);
    return sun;
}

SkyColors skyFromSun(const SunLight& sun) {
    const float elevation = std::asin(std::clamp(sun.direction.y, -1.0f, 1.0f)) * 180.0f / kPi;

    const float day = smoothstep(-6.0f, 25.0f, elevation);
    const float twilight = smoothstep(-18.0f, -3.0f, elevation) * (1.0f - day);
    const float night = std::max(0.0f, 1.0f - day - twilight);

    const core::Vec3 zenithDay{0.10f, 0.26f, 0.62f}, horizonDay{0.52f, 0.68f, 0.88f};
    // Синий час: небо холодное; тёплое свечение у горизонта добавляется ниже отдельным членом «золотого часа».
    const core::Vec3 zenithTwilight{0.02f, 0.035f, 0.12f}, horizonTwilight{0.17f, 0.18f, 0.32f};
    const core::Vec3 zenithNight{0.002f, 0.004f, 0.012f}, horizonNight{0.010f, 0.012f, 0.030f};

    SkyColors sky;
    sky.zenith = zenithDay * day + zenithTwilight * twilight + zenithNight * night;
    sky.horizon = horizonDay * day + horizonTwilight * twilight + horizonNight * night;

    // Золотой час: у горизонта небо принимает цвет низкого солнца.
    const float warm = smoothstep(-6.0f, 2.0f, elevation) * (1.0f - smoothstep(2.0f, 22.0f, elevation));
    sky.horizon = lerp(sky.horizon, sun.color * 0.75f, warm * 0.6f);

    sky.ground = core::Vec3{0.12f, 0.11f, 0.10f} * (0.04f + 0.96f * day) + core::Vec3{0.03f, 0.02f, 0.02f} * twilight;
    return sky;
}

}  // namespace han::render
