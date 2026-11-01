#version 450
// Bloom, спуск по мипам (E4, часть 34.4): 13 отсчётов (Jimenez, «Next Generation Post Processing in Call of Duty: Advanced Warfare»).
// Первый уровень дополнительно отсекает яркость по порогу (мягкое колено) и гасит одиночные «светляки» весами Каариса.
layout(set = 0, binding = 0) uniform sampler2D uSrc;

layout(push_constant) uniform Push {
    vec2 texel;      // 1 / размер источника
    float threshold; // яркость, ниже которой пиксель в bloom не попадает
    float knee;      // ширина мягкого колена порога
    float prefilter; // 1 — первый уровень (порог и веса Каариса), 0 — обычный спуск
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 applyThreshold(vec3 c) {
    float brightness = max(max(c.r, c.g), c.b);
    float soft = clamp(brightness - pc.threshold + pc.knee, 0.0, 2.0 * pc.knee);
    soft = soft * soft / (4.0 * pc.knee + 1e-4);
    float contribution = max(soft, brightness - pc.threshold) / max(brightness, 1e-4);
    return c * contribution;
}

void main() {
    vec3 a = texture(uSrc, vUv + pc.texel * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(uSrc, vUv + pc.texel * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(uSrc, vUv + pc.texel * vec2( 2.0, -2.0)).rgb;
    vec3 d = texture(uSrc, vUv + pc.texel * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(uSrc, vUv).rgb;
    vec3 f = texture(uSrc, vUv + pc.texel * vec2( 2.0,  0.0)).rgb;
    vec3 g = texture(uSrc, vUv + pc.texel * vec2(-2.0,  2.0)).rgb;
    vec3 h = texture(uSrc, vUv + pc.texel * vec2( 0.0,  2.0)).rgb;
    vec3 i = texture(uSrc, vUv + pc.texel * vec2( 2.0,  2.0)).rgb;
    vec3 j = texture(uSrc, vUv + pc.texel * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(uSrc, vUv + pc.texel * vec2( 1.0, -1.0)).rgb;
    vec3 l = texture(uSrc, vUv + pc.texel * vec2(-1.0,  1.0)).rgb;
    vec3 m = texture(uSrc, vUv + pc.texel * vec2( 1.0,  1.0)).rgb;

    // Пять групп по четыре отсчёта: центральный квадрат весом 0,5 и четыре угловых по 0,125.
    vec3 g0 = (j + k + l + m) * 0.25;
    vec3 g1 = (a + b + d + e) * 0.25;
    vec3 g2 = (b + c + e + f) * 0.25;
    vec3 g3 = (d + e + g + h) * 0.25;
    vec3 g4 = (e + f + h + i) * 0.25;

    if (pc.prefilter > 0.5) {
        g0 = applyThreshold(g0);
        g1 = applyThreshold(g1);
        g2 = applyThreshold(g2);
        g3 = applyThreshold(g3);
        g4 = applyThreshold(g4);
        float w0 = 0.5 / (1.0 + luma(g0));
        float w1 = 0.125 / (1.0 + luma(g1));
        float w2 = 0.125 / (1.0 + luma(g2));
        float w3 = 0.125 / (1.0 + luma(g3));
        float w4 = 0.125 / (1.0 + luma(g4));
        outColor = vec4((g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4), 1.0);
    } else {
        outColor = vec4(g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125, 1.0);
    }
}
