#version 450
// Bloom, подъём по мипам (E4): фильтр-«палатка» 3x3 из нижнего уровня; результат аддитивно прибавляется к верхнему (смешивание в конвейере).
layout(set = 0, binding = 0) uniform sampler2D uSrc;

layout(push_constant) uniform Push {
    vec2 texel;   // 1 / размер источника (нижнего уровня)
    float radius; // множитель шага отсчётов: больше — мягче и шире ореол
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = pc.texel * pc.radius;
    vec3 a = texture(uSrc, vUv + vec2(-d.x,  d.y)).rgb;
    vec3 b = texture(uSrc, vUv + vec2( 0.0,  d.y)).rgb;
    vec3 c = texture(uSrc, vUv + vec2( d.x,  d.y)).rgb;
    vec3 e = texture(uSrc, vUv + vec2(-d.x,  0.0)).rgb;
    vec3 f = texture(uSrc, vUv).rgb;
    vec3 g = texture(uSrc, vUv + vec2( d.x,  0.0)).rgb;
    vec3 h = texture(uSrc, vUv + vec2(-d.x, -d.y)).rgb;
    vec3 i = texture(uSrc, vUv + vec2( 0.0, -d.y)).rgb;
    vec3 j = texture(uSrc, vUv + vec2( d.x, -d.y)).rgb;
    outColor = vec4((f * 4.0 + (b + e + g + i) * 2.0 + (a + c + h + j)) / 16.0, 1.0);
}
