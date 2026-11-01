#version 450
// Временное сглаживание (E4, часть 34.4): накапливает историю прошлых кадров, подпиксельный джиттер камеры даёт сглаживание.
//  1. Пиксель находит себя в прошлом кадре по глубине и матрице переотображения.
//  2. История обрезается по окрестности 3x3 текущего кадра в YCoCg (отсечение по дисперсии): так не остаётся «шлейфов» за движущимися
//     объектами и при открытии фона. У объектов без вектора движения (они движутся сами) история гасится именно этим.
//  3. Смешивание идёт в «сжатом» цвете c / (1 + яркость): яркие блики не заливают пиксель и не мерцают.
layout(set = 0, binding = 0) uniform sampler2D uCurrent;  // сцена этого кадра (с подпиксельным сдвигом), линейный HDR
layout(set = 0, binding = 1) uniform sampler2D uHistory;  // результат прошлого кадра
layout(set = 0, binding = 2) uniform sampler2D uDepth;    // глубина этого кадра

layout(push_constant) uniform Push {
    mat4 reproject;       // клип этого кадра → клип прошлого кадра (оба без джиттера)
    float feedback;       // доля истории в результате, 0,9 по умолчанию
    float historyValid;   // 0 — истории нет (первый кадр, смена размера, склейка камеры)
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 compress(vec3 c) { return c / (1.0 + luma(c)); }
vec3 decompress(vec3 c) { return c / max(1.0 - luma(c), 1e-3); }

vec3 toYCoCg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

// Ближайшая к history точка внутри коробки [lo, hi] на луче к её центру.
vec3 clipToBox(vec3 lo, vec3 hi, vec3 history) {
    vec3 center = 0.5 * (hi + lo);
    vec3 extent = 0.5 * (hi - lo) + 1e-5;
    vec3 v = history - center;
    vec3 a = abs(v / extent);
    float m = max(a.x, max(a.y, a.z));
    return m > 1.0 ? center + v / m : history;
}

void main() {
    ivec2 size = textureSize(uCurrent, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 current = texelFetch(uCurrent, p, 0).rgb;

    float depth = texelFetch(uDepth, p, 0).r;
    vec2 uv = gl_FragCoord.xy / vec2(size);
    vec4 prevClip = pc.reproject * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec2 prevUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    bool usable = pc.historyValid > 0.5 && prevClip.w > 0.0 && all(greaterThanEqual(prevUv, vec2(0.0))) && all(lessThanEqual(prevUv, vec2(1.0)));
    if (!usable) {
        outColor = vec4(current, 1.0);
        return;
    }

    // Окрестность 3x3: среднее и разброс цвета.
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec3 c = toYCoCg(compress(texelFetch(uCurrent, clamp(p + ivec2(x, y), ivec2(0), size - 1), 0).rgb));
            m1 += c;
            m2 += c * c;
        }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    const float kGamma = 1.25;

    vec3 currentY = toYCoCg(compress(current));
    vec3 historyY = toYCoCg(compress(texture(uHistory, prevUv).rgb));
    historyY = clipToBox(mean - kGamma * sigma, mean + kGamma * sigma, historyY);

    // Чем быстрее пиксель смещается на экране, тем меньше доверия истории (размытие в движении).
    float velocityPixels = length((prevUv - uv) * vec2(size));
    float weight = pc.feedback * (1.0 - 0.3 * clamp(velocityPixels / 6.0, 0.0, 1.0));

    vec3 resolved = decompress(fromYCoCg(mix(currentY, historyY, weight)));
    outColor = vec4(max(resolved, vec3(0.0)), 1.0);
}
