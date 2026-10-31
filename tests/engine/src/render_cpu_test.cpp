#include <doctest/doctest.h>

#include <cmath>

#include "han/core/math.h"
#include "han/render/camera.h"
#include "han/render/mesh.h"

using namespace han::core;
using namespace han::render;

TEST_CASE("Mat4: умножение и перенос") {
    const Mat4 t = Mat4::translation({1, 2, 3});
    const Vec4 p = transform(t * Mat4::scale({2, 2, 2}), {1, 1, 1});
    CHECK(p.x == doctest::Approx(3));
    CHECK(p.y == doctest::Approx(4));
    CHECK(p.z == doctest::Approx(5));
    CHECK(p.w == doctest::Approx(1));
}

TEST_CASE("Mat4: поворот вокруг Y на 90° переводит +X в -Z") {
    const Vec4 p = transform(Mat4::rotationY(1.5707963f), {1, 0, 0});
    CHECK(p.x == doctest::Approx(0).epsilon(1e-5));
    CHECK(p.z == doctest::Approx(-1));
}

TEST_CASE("Camera: направления") {
    Camera c;
    CHECK(c.forward().z == doctest::Approx(-1));
    CHECK(c.right().x == doctest::Approx(1));
    c.yaw = 1.5707963f;  // поворот вправо на 90°: смотрим в +X
    CHECK(c.forward().x == doctest::Approx(1));
    CHECK(c.right().z == doctest::Approx(1));
}

TEST_CASE("Camera: тангаж ограничен, мышь вниз опускает взгляд") {
    Camera c;
    c.look(0, 100000);
    CHECK(c.pitch < 0);
    CHECK(c.pitch >= -1.55f);
    c.look(0, -1000000);
    CHECK(c.pitch <= 1.55f);
}

TEST_CASE("Camera: проекция — точка перед камерой видна, позади — нет, верх остаётся сверху") {
    Camera c;
    c.position = {0, 0, 0};
    const Mat4 vp = c.viewProjection(16.0f / 9.0f);

    const Vec4 front = transform(vp, {0, 0, -5});
    CHECK(front.w > 0);
    CHECK(front.z / front.w >= 0);
    CHECK(front.z / front.w <= 1);

    const Vec4 behind = transform(vp, {0, 0, 5});
    CHECK(behind.w < 0);

    // В Vulkan ось Y клипа направлена вниз: точка выше центра имеет отрицательный y.
    const Vec4 above = transform(vp, {0, 1, -5});
    CHECK(above.y / above.w < 0);
    const Vec4 rightPt = transform(vp, {1, 0, -5});
    CHECK(rightPt.x / rightPt.w > 0);
}

TEST_CASE("Camera: плоскости отсечения дают глубину 0 и 1") {
    Camera c;
    c.position = {0, 0, 0};
    const Mat4 vp = c.viewProjection(1.0f);
    const Vec4 n = transform(vp, {0, 0, -c.nearZ});
    const Vec4 f = transform(vp, {0, 0, -c.farZ});
    CHECK(n.z / n.w == doctest::Approx(0).epsilon(1e-4));
    CHECK(f.z / f.w == doctest::Approx(1).epsilon(1e-4));
}

TEST_CASE("Camera: движение вперёд идёт по взгляду") {
    Camera c;
    c.position = {0, 0, 0};
    c.move(1, 0, 0, 2.0f, 0.5f);
    CHECK(c.position.z == doctest::Approx(-1));
}

namespace {
// Каждый треугольник против часовой стрелки снаружи: геометрическая нормаль совпадает с нормалью вершины.
void checkOutwardWinding(const MeshData& m) {
    REQUIRE(m.indices.size() % 3 == 0);
    for (std::size_t i = 0; i < m.indices.size(); i += 3) {
        const Vertex& a = m.vertices[m.indices[i]];
        const Vertex& b = m.vertices[m.indices[i + 1]];
        const Vertex& c = m.vertices[m.indices[i + 2]];
        const Vec3 pa{a.position[0], a.position[1], a.position[2]};
        const Vec3 pb{b.position[0], b.position[1], b.position[2]};
        const Vec3 pc{c.position[0], c.position[1], c.position[2]};
        const Vec3 n = cross(pb - pa, pc - pa);
        const Vec3 vn{a.normal[0], a.normal[1], a.normal[2]};
        CHECK(dot(n, vn) > 0);
    }
}
}  // namespace

TEST_CASE("makeBox: 24 вершины, 36 индексов, обход против часовой стрелки") {
    const MeshData box = makeBox({1, 2, 3}, {1, 1, 1});
    CHECK(box.vertices.size() == 24);
    CHECK(box.indices.size() == 36);
    checkOutwardWinding(box);
    for (const auto& v : box.vertices) {
        CHECK(std::fabs(v.position[0]) == doctest::Approx(1));
        CHECK(std::fabs(v.position[1]) <= 2.0001f);
        CHECK(std::fabs(v.position[2]) <= 3.0001f);
    }
}

TEST_CASE("makePlane: нормаль вверх, обход правильный") {
    const MeshData plane = makePlane(10, {0.5f, 0.5f, 0.5f});
    CHECK(plane.vertices.size() == 4);
    CHECK(plane.indices.size() == 6);
    checkOutwardWinding(plane);
    for (const auto& v : plane.vertices) {
        CHECK(v.normal[1] == doctest::Approx(1));
        CHECK(v.position[1] == doctest::Approx(0));
    }
}
