#version 450
#extension GL_GOOGLE_include_directive : require
// Карта теней (E4): только глубина. Матрица каскада берётся из данных кадра, номер каскада и модель — из push constants.
#include "common.glsl"

layout(push_constant) uniform ShadowPush {
    mat4 model;
    uint cascade;
} pc;

layout(location = 0) in vec3 inPos;

void main() {
    gl_Position = frame.lightViewProj[pc.cascade] * (pc.model * vec4(inPos, 1.0));
}
