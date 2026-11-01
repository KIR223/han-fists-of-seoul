#version 450
#extension GL_GOOGLE_include_directive : require
// Меши (E2–E4): вершины из буфера, матрица модели через push constants (vertex, 64 байта), остальное — данные кадра.
#include "common.glsl"

layout(push_constant) uniform VertexPush {
    mat4 model;
} vpc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;
layout(location = 3) out vec2 vUV;
layout(location = 4) out float vViewDepth;  // глубина вдоль взгляда (clip.w) — для выбора каскада теней

void main() {
    vec4 world = vpc.model * vec4(inPos, 1.0);
    gl_Position = frame.viewProj * world;
    vWorldPos = world.xyz;
    vNormal = mat3(vpc.model) * inNormal;  // верно для равномерного масштаба
    vColor = inColor;
    vUV = inUV;
    vViewDepth = gl_Position.w;
}
