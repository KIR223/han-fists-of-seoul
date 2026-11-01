#version 450
#extension GL_GOOGLE_include_directive : require
// PBR (E4): Cook-Torrance с GGX, один направленный свет (солнце) и окружение из аналитического неба.
// Выход — линейная HDR-яркость; тонмаппинг делает отдельный проход.
#include "common.glsl"
#include "shadow.glsl"

layout(set = 0, binding = 0) uniform sampler2D uAlbedo;  // sRGB-формат: оборудование само переводит в линейный свет
layout(set = 2, binding = 0) uniform sampler2D uNormalMap;     // линейная, нормали OpenGL (+Y вверх), 0..1 → -1..1
layout(set = 3, binding = 0) uniform sampler2D uRoughMap;      // линейная, шероховатость в канале R

layout(push_constant) uniform FragmentPush {
    layout(offset = 64) vec4 material;  // x — металличность, y — шероховатость, z — есть карта нормалей, w — есть карта шероховатости
} fpc;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in vec2 vUV;
layout(location = 4) in float vViewDepth;
layout(location = 0) out vec4 outColor;

float distributionGGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float visibilitySmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 fresnelSchlick(float VoH, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Аналитическое приближение интеграла окружения (Karis, «Physically Based Shading on Mobile»).
vec3 envBrdfApprox(vec3 f0, float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// Касательный базис по производным позиции и UV (без хранимых касательных): Schüler, «Normal Mapping Without Precomputed Tangents».
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
    // В текстуре v растёт вниз, у нормалей OpenGL зелёный канал — вверх: переворачиваем B.
    return mat3(T * invmax, -B * invmax, N);
}

void main() {
    vec3 N = normalize(vNormal);
    if (fpc.material.z > 0.5) {
        vec3 tn = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
        tn.xy *= 1.0;  // сила рельефа (1 — как нарисовано)
        N = normalize(cotangentFrame(N, vWorldPos, vUV) * normalize(tn));
    }
    vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);
    vec3 L = normalize(frame.sunDir.xyz);

    vec4 albedoSample = texture(uAlbedo, vUV);
    vec3 albedo = albedoSample.rgb * vColor;
    if (frame.shadowParams.y > 0.5) {  // отладка: каскады разными цветами
        int c = cascadeIndex(vViewDepth);
        albedo = mix(albedo, c == 0 ? vec3(1.0, 0.2, 0.2) : (c == 1 ? vec3(0.2, 1.0, 0.2) : (c == 2 ? vec3(0.2, 0.4, 1.0) : vec3(0.6))), 0.5);
    }
    float wet = frame.lightInfo.y * smoothstep(0.85, 0.98, N.y);  // мокрыми считаются только горизонтальные поверхности
    albedo *= 1.0 - 0.45 * wet;
    float metallic = clamp(fpc.material.x, 0.0, 1.0);
    float roughness = fpc.material.w > 0.5 ? texture(uRoughMap, vUV).r : fpc.material.y;  // карта задаёт шероховатость как нарисована; без неё — число материала
    roughness = clamp(mix(roughness, roughness * 0.3, wet), 0.045, 1.0);  // ниже 0,045 блик вырождается в точку
    float a = roughness * roughness;

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 diffuseColor = albedo * (1.0 - metallic);

    vec3 H = normalize(V + L);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoV = abs(dot(N, V)) + 1e-5;
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);

    float D = distributionGGX(NoH, a);
    float Vis = visibilitySmithGGXCorrelated(NoV, NoL, a);
    vec3 F = fresnelSchlick(VoH, f0);
    vec3 specular = D * Vis * F;
    vec3 diffuse = diffuseColor / PI;

    vec3 sunRadiance = frame.sunColor.rgb * frame.sunColor.a;
    float visibility = shadowVisibility(vWorldPos, N, vViewDepth, L);
    vec3 direct = (diffuse * (1.0 - F) + specular) * sunRadiance * NoL * visibility;
    // Точечные источники (фонари): без теней; расстояние затухает по «окну» Карриса, блик по тому же GGX.
    int lightCount = int(frame.lightInfo.x);
    for (int i = 0; i < lightCount; ++i) {
        vec3 toLight = frame.lightPos[i].xyz - vWorldPos;
        float d2 = dot(toLight, toLight);
        float radius = frame.lightPos[i].w;
        if (d2 > radius * radius) continue;
        float d = sqrt(d2);
        vec3 Ll = toLight / max(d, 1e-4);
        float window = clamp(1.0 - pow(d / radius, 4.0), 0.0, 1.0);
        float atten = window * window / (d2 + 1.0);
        vec3 Hl = normalize(V + Ll);
        float NoLl = clamp(dot(N, Ll), 0.0, 1.0);
        float NoHl = clamp(dot(N, Hl), 0.0, 1.0);
        float VoHl = clamp(dot(V, Hl), 0.0, 1.0);
        vec3 Fl = fresnelSchlick(VoHl, f0);
        vec3 spec = distributionGGX(NoHl, a) * visibilitySmithGGXCorrelated(NoV, NoLl, a) * Fl;
        direct += (diffuse * (1.0 - Fl) + spec) * frame.lightColor[i].rgb * NoLl * atten;
    }

    // Окружение: рассеянная часть — полусфера неба по нормали, зеркальная — небо по отражённому лучу (размытое при шероховатости).
    float up = N.y * 0.5 + 0.5;
    vec3 ambientIrradiance = mix(frame.skyGround.rgb, mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, 0.6), up);
    vec3 R = reflect(-V, N);
    vec3 envSpecular = mix(skyGradient(R), ambientIrradiance, roughness * roughness);
    // Мягкое небо в тени: нижняя граница рассеянного света, чтобы теневая сторона домов не уходила в чёрный (грубая замена SSAO).
    ambientIrradiance = max(ambientIrradiance, mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, 0.5) * 0.55);
    // Прикрытие у земли: вертикальные поверхности темнеют у основания (угол стены и тротуара закрыт от неба).
    float groundAo = mix(0.55, 1.0, smoothstep(0.0, 3.5, vWorldPos.y)) * mix(1.0, 0.75 + 0.25 * N.y, 1.0);
    vec3 ambient = (diffuseColor * ambientIrradiance + envSpecular * envBrdfApprox(f0, roughness, NoV)) * groundAo;

    vec3 color = direct + ambient;
    // Ночь: окна и вывески (альфа альбедо = 0) светятся; часть окон тёмная — случайно по секции и дому (оттенок дома vColor — его «номер»).
    float night = smoothstep(0.12, -0.04, frame.sunDir.y);
    float emissiveMask = 1.0 - albedoSample.a;  // непрерывная маска: на мипах и под углом не рябит
    if (night > 0.001 && emissiveMask > 0.01) {
        vec2 cell = floor(vUV * 2.0);
        vec3 houseId = floor(vColor * 255.0 + 0.5);  // квантуем: интерполяция даёт шум ~1e-7, а хэш его усиливает
        float seed = fract(sin(dot(cell + houseId.rg * 0.37 + houseId.b * 0.11, vec2(12.9898, 78.233))) * 43758.5453);
        bool lit = seed < 0.42;
        float lum = dot(albedoSample.rgb, vec3(0.299, 0.587, 0.114));
        vec3 glow = mix(vec3(1.0, 0.78, 0.5) * 1.6, albedoSample.rgb * 3.0, smoothstep(0.2, 0.4, lum));  // яркие (вывески) светят своим цветом, стекло — тёплым
        if (lit) color += glow * night * emissiveMask;
    }
    // Воздушная дымка: дальние объекты тонут в цвете горизонта, глубина города читается (при плотности 0 — без изменений).
    float dist = length(frame.cameraPos.xyz - vWorldPos);
    float fog = 1.0 - exp(-dist * frame.params.y);
    vec3 fogColor = frame.skyHorizon.rgb * 0.9 + frame.sunColor.rgb * frame.sunColor.a * 0.02;
    color = mix(color, fogColor, clamp(fog, 0.0, 0.85));
    outColor = vec4(color, 1.0);
}
