#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

void main() {
    // Направление взгляда: точка на дальней плоскости обратно в мир.
    vec4 world = frame.invViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w - frame.cameraPos.xyz);

    vec3 sky = skyGradient(dir);
    vec3 sunRadiance = frame.sunColor.rgb * frame.sunColor.a;

    // Свечение вокруг солнца (рассеяние вперёд) и сам диск (угловой радиус около 0,27°).
    float cosS = dot(dir, frame.sunDir.xyz);
    float halo = pow(max(cosS, 0.0), 24.0) * 0.12 + pow(max(cosS, 0.0), 480.0) * 0.5;
    float disk = smoothstep(0.999985, 0.999995, cosS);
    sky += sunRadiance * (halo + disk * 60.0) * step(0.0, frame.sunColor.a);

    outColor = vec4(sky, 1.0);
}
