#version 450
// Небо: один треугольник на весь экран на дальней плоскости (z = 1). Рисуется после мешей с тестом глубины «меньше или равно».
layout(location = 0) out vec2 vNdc;

void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
