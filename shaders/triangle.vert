#version 450
// Тестовый треугольник E2: вершины в шейдере, вращение по времени.
layout(push_constant) uniform PushConstants {
    float time;
    float aspect;
} pc;

layout(location = 0) out vec3 vColor;

const vec2 kPos[3] = vec2[](vec2(0.0, -0.6), vec2(0.6, 0.6), vec2(-0.6, 0.6));
const vec3 kCol[3] = vec3[](vec3(1.0, 0.2, 0.2), vec3(0.2, 1.0, 0.2), vec3(0.2, 0.4, 1.0));

void main() {
    float c = cos(pc.time);
    float s = sin(pc.time);
    vec2 p = kPos[gl_VertexIndex];
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    p.x /= pc.aspect;
    gl_Position = vec4(p, 0.0, 1.0);
    vColor = kCol[gl_VertexIndex];
}
