#include <doctest/doctest.h>

#include <cmath>

#include "han/core/math.h"
#include "han/render/camera.h"
#include "han/render/lighting.h"
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

TEST_CASE("Mat4: обратная матрица возвращает точку из клипа в мир") {
    const Mat4 vp = Mat4::perspective(1.0f, 1.6f, 0.1f, 2000.0f) * Mat4::lookDirection({1, 2, 3}, normalize({0.3f, -0.2f, -1.0f}));
    Mat4 inverse;
    REQUIRE(invert(vp, inverse));
    const Vec3 world{4, 1, -7};
    const Vec4 clip = transform(vp, world);
    const Vec4 back = transform(inverse, {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w});
    CHECK(back.x / back.w == doctest::Approx(4).epsilon(1e-3));
    CHECK(back.y / back.w == doctest::Approx(1).epsilon(1e-3));
    CHECK(back.z / back.w == doctest::Approx(-7).epsilon(1e-3));

    const Mat4 product = vp * inverse;
    for (int i = 0; i < 16; ++i) CHECK(product.m[i] == doctest::Approx((i % 5 == 0) ? 1.0f : 0.0f).epsilon(1e-3));
}

TEST_CASE("Mat4: вырожденная матрица не обращается и результат не трогает") {
    Mat4 zero;
    for (float& v : zero.m) v = 0;
    Mat4 out = Mat4::translation({1, 2, 3});
    CHECK_FALSE(invert(zero, out));
    CHECK(out.m[12] == doctest::Approx(1));
}

TEST_CASE("makeSphere: нормали наружу, обход против часовой стрелки, без вырожденных треугольников") {
    const MeshData sphere = makeSphere(2.0f, 24, 12, {1, 1, 1});
    REQUIRE(sphere.indices.size() % 3 == 0);
    for (const auto& v : sphere.vertices) {
        const float r = std::sqrt(v.position[0] * v.position[0] + v.position[1] * v.position[1] + v.position[2] * v.position[2]);
        CHECK(r == doctest::Approx(2.0f).epsilon(1e-4));
        CHECK(v.normal[0] * v.position[0] + v.normal[1] * v.position[1] + v.normal[2] * v.position[2] > 0);
    }
    int degenerate = 0;
    checkOutwardWinding(sphere);
    for (std::size_t i = 0; i < sphere.indices.size(); i += 3) {
        const Vertex &a = sphere.vertices[sphere.indices[i]], &b = sphere.vertices[sphere.indices[i + 1]], &c = sphere.vertices[sphere.indices[i + 2]];
        const Vec3 n = cross(Vec3{b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]},
                             Vec3{c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]});
        if (length(n) < 1e-6f) ++degenerate;
    }
    CHECK(degenerate == 0);
}

TEST_CASE("свет: цвет по температуре") {
    const Vec3 warm = colorFromTemperature(2000.0f), white = colorFromTemperature(6500.0f), cold = colorFromTemperature(12000.0f);
    CHECK(warm.x == doctest::Approx(1.0f));  // нормирован на наибольший канал
    CHECK(warm.x > warm.y);
    CHECK(warm.y > warm.z);
    CHECK(white.x > 0.9f);
    CHECK(white.y > 0.9f);
    CHECK(white.z > 0.9f);
    CHECK(cold.z == doctest::Approx(1.0f));
    CHECK(cold.z > cold.x);
}

TEST_CASE("свет: направление солнца и освещённость") {
    const SunLight zenith = makeSun(90.0f, 0.0f, 5500.0f);
    CHECK(zenith.direction.y == doctest::Approx(1.0f));
    const SunLight east = makeSun(0.0f, 90.0f, 5500.0f);
    CHECK(east.direction.x == doctest::Approx(1.0f));
    CHECK(east.direction.z == doctest::Approx(0.0f).epsilon(1e-5));
    const SunLight north = makeSun(0.0f, 0.0f, 5500.0f);
    CHECK(north.direction.z == doctest::Approx(-1.0f));  // север — это -Z, как «вперёд» у камеры
    for (const float elevation : {-30.0f, -4.0f, 3.0f, 35.0f, 80.0f}) CHECK(length(makeSun(elevation, 123.0f, 5000.0f).direction) == doctest::Approx(1.0f));

    CHECK(makeSun(-10.0f, 0.0f, 5000.0f).illuminance == doctest::Approx(0.0f));
    CHECK(makeSun(-4.0f, 0.0f, 5000.0f).illuminance == doctest::Approx(0.0f));
    CHECK(makeSun(3.0f, 0.0f, 5000.0f).illuminance > 0.0f);
    CHECK(makeSun(3.0f, 0.0f, 5000.0f).illuminance < makeSun(35.0f, 0.0f, 5000.0f).illuminance);
    CHECK(makeSun(60.0f, 0.0f, 5000.0f).illuminance == doctest::Approx(7.5f));
    CHECK(makeSun(60.0f, 0.0f, 5000.0f, 10.0f).illuminance == doctest::Approx(10.0f));
}

TEST_CASE("свет: небо светлеет с высотой солнца и тёмное ночью") {
    const auto luminance = [](Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
    float previous = -1.0f;
    for (const float elevation : {-30.0f, -15.0f, -6.0f, 0.0f, 10.0f, 30.0f, 60.0f}) {
        const SkyColors sky = skyFromSun(makeSun(elevation, 90.0f, 5000.0f));
        const float l = luminance(sky.horizon) + luminance(sky.zenith);
        CHECK_MESSAGE(l >= previous - 1e-4f, "высота " << elevation);
        previous = l;
        for (const Vec3 c : {sky.zenith, sky.horizon, sky.ground}) {
            CHECK(c.x >= 0.0f);
            CHECK(c.y >= 0.0f);
            CHECK(c.z >= 0.0f);
        }
    }
    const SkyColors night = skyFromSun(makeSun(-40.0f, 0.0f, 4000.0f));
    CHECK(luminance(night.zenith) < 0.01f);
    CHECK(luminance(night.horizon) < 0.03f);
    const SkyColors day = skyFromSun(makeSun(60.0f, 0.0f, 5800.0f));
    CHECK(day.zenith.z > day.zenith.x);  // днём небо над головой синее
    CHECK(luminance(day.horizon) > luminance(day.zenith));  // и светлее у горизонта
}
