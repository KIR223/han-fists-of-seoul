// Общее для шейдеров сцены (E4): данные кадра и аналитическое небо. Подключается через #include (glslc -I shaders).
#ifndef HAN_COMMON_GLSL
#define HAN_COMMON_GLSL

const float PI = 3.14159265358979;

// Данные кадра: один буфер на кадр, набор дескрипторов 1. Структура зеркалит FrameUbo в han/render/vulkan/renderer.cpp.
layout(set = 1, binding = 0, std140) uniform Frame {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 cameraPos;   // xyz — позиция камеры
    vec4 sunDir;      // xyz — единичный вектор К солнцу
    vec4 sunColor;    // rgb — цвет света, a — освещённость
    vec4 skyZenith;   // rgb — небо над головой
    vec4 skyHorizon;  // rgb — небо у горизонта
    vec4 skyGround;   // rgb — «земля» под горизонтом
    vec4 params;      // x — экспозиция, y — плотность воздушной дымки
    mat4 lightViewProj[3];  // каскады теней: мир → клип карты теней
    vec4 cascadeSplits;     // xyz — дальние границы каскадов по глубине вида, метры; w — 1, если тени включены
    vec4 cascadeTexel;      // xyz — размер текселя карты каждого каскада, метры
    vec4 shadowParams;      // x — 1 / размер карты (шаг по uv), y — 1: подкрасить каскады (отладка)
} frame;

// Небо без солнца: градиент от горизонта к зениту, под горизонтом — цвет земли.
vec3 skyGradient(vec3 dir) {
    float h = dir.y;
    vec3 up = mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, pow(clamp(h, 0.0, 1.0), 0.45));
    vec3 down = mix(frame.skyHorizon.rgb, frame.skyGround.rgb, clamp(-h * 4.0, 0.0, 1.0));
    return h >= 0.0 ? up : down;
}

#endif
