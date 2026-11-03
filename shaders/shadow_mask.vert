#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "skinning.glsl"
layout(push_constant) uniform ShadowPush { mat4 model; uint cascade; } pc;
layout(location = 0) in vec3 inPos;
layout(location = 3) in vec2 inUV;
layout(location = 0) out vec2 vUV;
void main() {
    gl_Position = frame.lightViewProj[pc.cascade] * pc.model * skinMatrix() * vec4(inPos, 1.0);
    vUV = inUV;
}
