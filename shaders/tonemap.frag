#version 450
// Тонмаппинг AgX (часть 34.4): HDR → экран. Запись идёт в sRGB-формат, оборудование само кодирует результат.
layout(set = 0, binding = 0) uniform sampler2D uHdr;
layout(set = 0, binding = 1) uniform sampler2D uBloom;  // свечение: нулевой (самый крупный) уровень bloom, половинное разрешение

layout(push_constant) uniform Push {
    float exposure;
    float contrast;    // степень кривой (1 — без изменений, больше — контрастнее)
    float saturation;  // 1 — без изменений
    float bloomIntensity;  // доля bloom, добавляемая к сцене до тонмаппинга (0 — выключено)
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// AgX (Troy Sobotka); приближение кривой контраста многочленом, матрицы входа и выхода.
const mat3 kAgxMat = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                          0.0784335999999992, 0.878468636469772, 0.0784336,
                          0.0792237451477643, 0.0791661274605434, 0.879142973793104);
const mat3 kAgxMatInv = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
const float kMinEv = -12.47393;
const float kMaxEv = 4.026069;

vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 v) {
    v = kAgxMat * v;
    v = clamp(log2(max(v, vec3(1e-10))), kMinEv, kMaxEv);
    v = (v - kMinEv) / (kMaxEv - kMinEv);
    return agxContrast(v);
}

// «Взгляд» AgX (как «Punchy» в Blender, но параметрами): контраст степенью и насыщенность от яркости.
vec3 agxLook(vec3 v) {
    float luma = dot(v, vec3(0.2126, 0.7152, 0.0722));
    v = pow(max(v, vec3(0.0)), vec3(pc.contrast));
    return luma + pc.saturation * (v - luma);
}

vec3 agxToLinear(vec3 v) {
    v = kAgxMatInv * v;
    return pow(max(v, vec3(0.0)), vec3(2.2));
}

// Шум для дизеринга (Jimenez, interleaved gradient noise): убирает полосы на плавных градиентах неба.
float gradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 hdr = (texture(uHdr, vUv).rgb + texture(uBloom, vUv).rgb * pc.bloomIntensity) * pc.exposure;
    vec3 color = agxToLinear(agxLook(agx(hdr)));
    color += (gradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
