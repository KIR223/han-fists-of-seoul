// Чтение каскадных карт теней (E4). Подключается только фрагментными шейдерами сцены: ресурс set 1, binding 1 виден лишь им.
#ifndef HAN_SHADOW_GLSL
#define HAN_SHADOW_GLSL

layout(set = 1, binding = 1) uniform sampler2DArrayShadow uShadow;  // сравнивающий сэмплер: аппаратный PCF 2x2 на каждый тексель

// Номер каскада по глубине вдоль взгляда; 3 — глубже последнего каскада (теней нет).
int cascadeIndex(float viewDepth) {
    return viewDepth < frame.cascadeSplits.x ? 0 : (viewDepth < frame.cascadeSplits.y ? 1 : (viewDepth < frame.cascadeSplits.z ? 2 : 3));
}

// Доля света, дошедшая до точки: 1 — полностью освещена, 0 — в тени. normal — единичная нормаль поверхности, L — к солнцу.
float shadowVisibility(vec3 worldPos, vec3 normal, float viewDepth, vec3 L) {
    if (frame.cascadeSplits.w < 0.5) return 1.0;
    int c = cascadeIndex(viewDepth);
    if (c > 2) return 1.0;

    // Смещение вдоль нормали пропорционально размеру текселя и наклону к свету: убирает «лесенку» на скользящих поверхностях.
    float NoL = clamp(dot(normal, L), 0.0, 1.0);
    float texel = frame.cascadeTexel[c];
    vec3 p = worldPos + normal * texel * (1.0 + 2.5 * (1.0 - NoL));

    vec4 lightClip = frame.lightViewProj[c] * vec4(p, 1.0);
    vec3 uvz = vec3(lightClip.xy * 0.5 + 0.5, lightClip.z);

    // 3x3 отсчёта по сравнивающему сэмплеру (каждый — билинейное сравнение): мягкий край тени шириной около двух текселей.
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(uShadow, vec4(uvz.xy + vec2(x, y) * frame.shadowParams.x, float(c), uvz.z));
    lit /= 9.0;

    // На краю последнего каскада тень плавно сходит на нет, чтобы не было видимой границы.
    float fade = smoothstep(0.85, 1.0, viewDepth / frame.cascadeSplits.z);
    return mix(lit, 1.0, fade);
}

#endif
