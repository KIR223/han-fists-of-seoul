#version 450
#extension GL_GOOGLE_include_directive : require
// PBR (E4): Cook-Torrance с GGX, один направленный свет (солнце) и окружение из аналитического неба.
// Выход — линейная HDR-яркость; тонмаппинг делает отдельный проход.
#include "common.glsl"
#include "shadow.glsl"

layout(set = 0, binding = 0) uniform sampler2D uAlbedo;  // sRGB-формат: оборудование само переводит в линейный свет

layout(push_constant) uniform FragmentPush {
    layout(offset = 64) vec4 material;  // x — металличность, y — шероховатость
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

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);
    vec3 L = normalize(frame.sunDir.xyz);

    vec3 albedo = texture(uAlbedo, vUV).rgb * vColor;
    if (frame.shadowParams.y > 0.5) {  // отладка: каскады разными цветами
        int c = cascadeIndex(vViewDepth);
        albedo = mix(albedo, c == 0 ? vec3(1.0, 0.2, 0.2) : (c == 1 ? vec3(0.2, 1.0, 0.2) : (c == 2 ? vec3(0.2, 0.4, 1.0) : vec3(0.6))), 0.5);
    }
    float metallic = clamp(fpc.material.x, 0.0, 1.0);
    float roughness = clamp(fpc.material.y, 0.045, 1.0);  // ниже 0,045 блик вырождается в точку
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

    // Окружение: рассеянная часть — полусфера неба по нормали, зеркальная — небо по отражённому лучу (размытое при шероховатости).
    float up = N.y * 0.5 + 0.5;
    vec3 ambientIrradiance = mix(frame.skyGround.rgb, mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, 0.6), up);
    vec3 R = reflect(-V, N);
    vec3 envSpecular = mix(skyGradient(R), ambientIrradiance, roughness * roughness);
    vec3 ambient = diffuseColor * ambientIrradiance + envSpecular * envBrdfApprox(f0, roughness, NoV);

    outColor = vec4(direct + ambient, 1.0);
}
