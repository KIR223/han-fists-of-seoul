#pragma once
// Малая математика движка (E2): векторы и матрицы 4x4 по столбцам (m[столбец * 4 + строка]), правая система координат:
// X вправо, Y вверх, камера смотрит вдоль -Z. Проекция — под Vulkan (глубина 0..1, Y клипа вниз).
#include <cmath>

namespace han::core {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float l = length(a);
    return l > 0 ? a * (1.0f / l) : Vec3{};
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return {}; }
    static Mat4 translation(Vec3 t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    static Mat4 scale(Vec3 s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }
    static Mat4 rotationY(float radians) {
        Mat4 r;
        const float c = std::cos(radians), s = std::sin(radians);
        r.m[0] = c;
        r.m[8] = s;
        r.m[2] = -s;
        r.m[10] = c;
        return r;
    }
    // Перспектива под Vulkan: вид вдоль -Z, глубина 0..1, ось Y клипа направлена вниз.
    static Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
        Mat4 r;
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        for (float& v : r.m) v = 0;
        r.m[0] = f / aspect;
        r.m[5] = -f;
        r.m[10] = farZ / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = nearZ * farZ / (nearZ - farZ);
        return r;
    }
    // Вид из точки eye вдоль направления forward (единичный), world-up = +Y.
    static Mat4 lookDirection(Vec3 eye, Vec3 forward) {
        const Vec3 s = normalize(cross(forward, Vec3{0, 1, 0}));
        const Vec3 u = cross(s, forward);
        Mat4 r;
        r.m[0] = s.x;  r.m[4] = s.y;  r.m[8] = s.z;
        r.m[1] = u.x;  r.m[5] = u.y;  r.m[9] = u.z;
        r.m[2] = -forward.x;  r.m[6] = -forward.y;  r.m[10] = -forward.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(forward, eye);
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = sum;
        }
    return r;
}

// Преобразует точку матрицей (w = 1) и возвращает однородные координаты (x, y, z, w).
struct Vec4 {
    float x, y, z, w;
};
inline Vec4 transform(const Mat4& a, Vec3 p) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12], a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
            a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14], a.m[3] * p.x + a.m[7] * p.y + a.m[11] * p.z + a.m[15]};
}

}  // namespace han::core
// Пример: Mat4 mvp = proj * view * Mat4::translation({0, 1, 0}); Vec4 clip = transform(mvp, point);
