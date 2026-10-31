#version 450
// Меши E2/E3: вершины из буфера, матрицы через push constants (viewProj + model = 128 байт).
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUV;

void main() {
    gl_Position = pc.viewProj * (pc.model * vec4(inPos, 1.0));
    vNormal = mat3(pc.model) * inNormal;  // верно для равномерного масштаба
    vColor = inColor;
    vUV = inUV;
}
