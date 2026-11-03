#version 450
#extension GL_GOOGLE_include_directive : require
// Меши (E2–E4): вершины из буфера, матрица модели через push constants (vertex, 64 байта), остальное — данные кадра.
#include "common.glsl"
#include "skinning.glsl"

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
    mat4 deform = skinMatrix();
    vec4 world = vpc.model * deform * vec4(inPos, 1.0);
    gl_Position = frame.viewProj * world;
    vWorldPos = world.xyz;
    mat3 normalTransform = mat3(vpc.model * deform);
    // LBS может дать почти вырожденную матрицу на сильно согнутом суставе.
    // inverse(0) дал бы NaN и через bloom испортил весь кадр.
    vec3 normal = abs(determinant(normalTransform)) > 0.000001
        ? transpose(inverse(normalTransform)) * inNormal
        : normalTransform * inNormal;
    vNormal = dot(normal, normal) > 0.000001 ? normal : inNormal;
    vColor = inColor;
    vUV = inUV;
    vViewDepth = gl_Position.w;
}
