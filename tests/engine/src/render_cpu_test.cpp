#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "han/core/math.h"
#include "han/render/camera.h"
#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/png_io.h"
#include "han/render/shadow.h"
#include "han/render/taa.h"

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

namespace {
// Углы среза камеры между глубинами d0 и d1 вдоль взгляда: разворачиваем на NDC-глубине каждого среза (так же, как в коде теней).
std::vector<Vec3> frustumSlice(const Camera& cam, float aspect, float d0, float d1) {
    Mat4 inv;
    REQUIRE(invert(cam.viewProjection(aspect), inv));
    std::vector<Vec3> out;
    for (const float d : {d0, d1}) {
        const float ndcZ = cam.farZ * (d - cam.nearZ) / (d * (cam.farZ - cam.nearZ));
        for (const float x : {-1.0f, 1.0f})
            for (const float y : {-1.0f, 1.0f}) {
                const Vec4 p = transform(inv, {x, y, ndcZ});
                out.push_back({p.x / p.w, p.y / p.w, p.z / p.w});
            }
    }
    return out;
}
}  // namespace

TEST_CASE("тени: границы каскадов 15 / 50 / 150 и их порядок") {
    Camera cam;
    const ShadowCascades c = computeShadowCascades(cam.viewProjection(1.6f), cam.nearZ, cam.farZ, normalize({0.4f, 0.8f, 0.3f}), {});
    CHECK(c.cascade[0].splitDistance == doctest::Approx(15.0f));
    CHECK(c.cascade[1].splitDistance == doctest::Approx(50.0f));
    CHECK(c.cascade[2].splitDistance == doctest::Approx(150.0f));
    CHECK(c.cascade[0].radius < c.cascade[1].radius);
    CHECK(c.cascade[1].radius < c.cascade[2].radius);
    for (const auto& k : c.cascade) CHECK(k.texelWorldSize == doctest::Approx(2.0f * k.radius / 2048.0f).epsilon(1e-4));

    Camera nearCam;
    nearCam.farZ = 30.0f;  // дальняя плоскость ближе третьей границы: границы обрезаются по ней
    const ShadowCascades clipped = computeShadowCascades(nearCam.viewProjection(1.6f), nearCam.nearZ, nearCam.farZ, normalize({0, 1, 0.2f}), {});
    CHECK(clipped.cascade[1].splitDistance <= 30.0f + 1e-4f);
    CHECK(clipped.cascade[2].splitDistance <= 30.0f + 1e-4f);
}

TEST_CASE("тени: все углы среза каждого каскада внутри его клипа") {
    const float suns[][3] = {{0.4f, 0.8f, 0.3f}, {-0.7f, 0.3f, 0.2f}, {0.0f, 1.0f, 0.0f}, {0.9f, 0.05f, -0.4f}, {0.2f, -0.1f, 0.9f}};
    const float yaws[] = {0.0f, 1.0f, -2.2f, 3.0f};
    for (const auto& s : suns)
        for (const float yaw : yaws) {
            Camera cam;
            cam.position = {12.0f, 3.0f, -40.0f};
            cam.yaw = yaw;
            cam.pitch = -0.3f;
            const Vec3 sun = normalize({s[0], s[1], s[2]});
            const ShadowCascades c = computeShadowCascades(cam.viewProjection(1.78f), cam.nearZ, cam.farZ, sun, {});
            float previous = cam.nearZ;
            for (const auto& k : c.cascade) {
                for (const Vec3& corner : frustumSlice(cam, 1.78f, previous, k.splitDistance)) {
                    const Vec4 p = transform(k.viewProjection, corner);
                    CHECK_MESSAGE(std::fabs(p.x) <= 1.0f + 1e-3f, "x=" << p.x);
                    CHECK_MESSAGE(std::fabs(p.y) <= 1.0f + 1e-3f, "y=" << p.y);
                    CHECK_MESSAGE(p.z >= -1e-3f, "z=" << p.z);
                    CHECK_MESSAGE(p.z <= 1.0f + 1e-3f, "z=" << p.z);
                    CHECK(p.w == doctest::Approx(1.0f));  // ортографическая проекция
                }
                previous = k.splitDistance;
            }
        }
}

TEST_CASE("тени: радиус каскада не зависит от поворота камеры, а тень привязана к сетке текселей") {
    const Vec3 sun = normalize({0.4f, 0.8f, 0.3f});
    Camera a;
    a.position = {5, 2, 5};
    const ShadowCascades base = computeShadowCascades(a.viewProjection(1.6f), a.nearZ, a.farZ, sun, {});

    Camera rotated = a;
    rotated.yaw = 2.4f;
    rotated.pitch = -0.5f;
    const ShadowCascades turned = computeShadowCascades(rotated.viewProjection(1.6f), rotated.nearZ, rotated.farZ, sun, {});
    for (int i = 0; i < kShadowCascades; ++i)
        CHECK_MESSAGE(turned.cascade[i].radius == doctest::Approx(base.cascade[i].radius).epsilon(1e-4), "каскад " << i << ": " << turned.cascade[i].radius << " против " << base.cascade[i].radius);

    // Мировая точка попадает в карту теней с одной и той же дробной частью текселя при любом сдвиге камеры:
    // иначе края теней «плыли» бы при движении.
    const Vec3 point{3.7f, 0.0f, -11.3f};
    const auto fractionalTexel = [&](const ShadowCascade& k, int axis) {
        const Vec4 p = transform(k.viewProjection, point);
        const float texel = ((axis == 0 ? p.x : p.y) * 0.5f + 0.5f) * 2048.0f;
        return texel - std::floor(texel);
    };
    for (const float shift : {0.013f, 0.37f, 2.9f, 17.0f}) {
        Camera moved = a;
        moved.position = {a.position.x + shift, a.position.y, a.position.z - shift * 0.7f};
        const ShadowCascades after = computeShadowCascades(moved.viewProjection(1.6f), moved.nearZ, moved.farZ, sun, {});
        for (int i = 0; i < kShadowCascades; ++i)
            for (const int axis : {0, 1}) {
                const float f0 = fractionalTexel(base.cascade[i], axis), f1 = fractionalTexel(after.cascade[i], axis);
                const float diff = std::fabs(f0 - f1);
                CHECK_MESSAGE(std::min(diff, 1.0f - diff) < 0.02f, "каскад " << i << ", ось " << axis << ", сдвиг " << shift << ": " << f0 << " и " << f1);
            }
    }
}

TEST_CASE("тени: вырожденные входы дают нулевой результат без NaN") {
    Mat4 zero;
    for (float& v : zero.m) v = 0;
    const ShadowCascades none = computeShadowCascades(zero, 0.1f, 100.0f, {0, 1, 0}, {});
    CHECK(none.cascade[0].radius == doctest::Approx(0.0f));

    Camera cam;
    const ShadowCascades reversed = computeShadowCascades(cam.viewProjection(1.6f), 50.0f, 10.0f, {0, 1, 0}, {});  // far < near
    CHECK(reversed.cascade[2].radius == doctest::Approx(0.0f));

    ShadowSettings tiny;
    tiny.mapSize = 0;
    CHECK(computeShadowCascades(cam.viewProjection(1.6f), cam.nearZ, cam.farZ, {0, 1, 0}, tiny).cascade[0].radius == doctest::Approx(0.0f));

    const ShadowCascades zenith = computeShadowCascades(cam.viewProjection(1.6f), cam.nearZ, cam.farZ, {0, 1, 0}, {});  // солнце в зените
    for (const auto& k : zenith.cascade)
        for (const float v : k.viewProjection.m) CHECK(std::isfinite(v));
}

TEST_CASE("Mat4: ортографическая проекция переводит ящик в клип 0..1 по глубине") {
    const Mat4 o = Mat4::orthographic(-2, 2, -1, 1, 1.0f, 11.0f);
    const Vec4 nearPt = transform(o, {0, 0, -1.0f}), farPt = transform(o, {0, 0, -11.0f}), corner = transform(o, {2, 1, -6.0f});
    CHECK(nearPt.z == doctest::Approx(0.0f));
    CHECK(farPt.z == doctest::Approx(1.0f));
    CHECK(corner.x == doctest::Approx(1.0f));
    CHECK(corner.y == doctest::Approx(1.0f));
    CHECK(corner.z == doctest::Approx(0.5f));
}

TEST_CASE("TAA: джиттер Холтона — 8 различных фаз внутри пикселя с периодом 8") {
    std::vector<Jitter> phases;
    float sumX = 0, sumY = 0;
    for (int i = 0; i < kTaaJitterPhases; ++i) {
        const Jitter j = haltonJitter(i);
        CHECK(j.x > -0.5f);
        CHECK(j.x < 0.5f);
        CHECK(j.y > -0.5f);
        CHECK(j.y < 0.5f);
        for (const Jitter& other : phases) CHECK((std::fabs(other.x - j.x) > 1e-4f || std::fabs(other.y - j.y) > 1e-4f));
        phases.push_back(j);
        sumX += j.x;
        sumY += j.y;
    }
    CHECK(std::fabs(sumX / kTaaJitterPhases) < 0.1f);  // фазы не смещены в одну сторону пикселя
    CHECK(std::fabs(sumY / kTaaJitterPhases) < 0.1f);
    CHECK(haltonJitter(3).x == doctest::Approx(haltonJitter(11).x));
    CHECK(haltonJitter(3).y == doctest::Approx(haltonJitter(3 + 8 * 100).y));
    CHECK(std::isfinite(haltonJitter(-5).x));  // отрицательные номера кадров не ломают последовательность
}

TEST_CASE("TAA: сдвиг проекции смещает NDC ровно на долю пикселя") {
    Camera cam;
    const Mat4 vp = cam.viewProjection(1.6f);
    const Vec3 point{1.0f, 0.5f, -6.0f};
    const Vec4 a = transform(vp, point);
    const Jitter jitter{0.25f, -0.4f};
    const Vec4 b = transform(jitteredViewProjection(vp, jitter, 1280, 720), point);
    CHECK(b.x / b.w - a.x / a.w == doctest::Approx(2.0f * 0.25f / 1280.0f).epsilon(1e-3));
    CHECK(b.y / b.w - a.y / a.w == doctest::Approx(2.0f * -0.4f / 720.0f).epsilon(1e-3));
    CHECK(b.z / b.w == doctest::Approx(a.z / a.w));  // глубина не меняется
    CHECK(b.w == doctest::Approx(a.w));
    const Mat4 unchanged = jitteredViewProjection(vp, jitter, 0, 0);  // нулевой размер — матрица как есть
    for (int i = 0; i < 16; ++i) CHECK(unchanged.m[i] == doctest::Approx(vp.m[i]));
}

TEST_CASE("TAA: матрица переотображения находит пиксель в прошлом кадре") {
    Camera previous;
    previous.position = {0, 2, 8};
    Camera current = previous;
    current.position = {1.5f, 2.2f, 7.0f};
    current.yaw = 0.15f;
    const Mat4 prevVp = previous.viewProjection(1.6f), curVp = current.viewProjection(1.6f);
    Mat4 reproject;
    REQUIRE(reprojectionMatrix(curVp, prevVp, reproject));

    for (const Vec3 world : {Vec3{0, 0, -5}, Vec3{3, 1, -20}, Vec3{-4, 0.5f, -60}}) {
        const Vec4 cur = transform(curVp, world), prev = transform(prevVp, world);
        const Vec4 mapped = transform(reproject, {cur.x / cur.w, cur.y / cur.w, cur.z / cur.w});
        CHECK(mapped.x / mapped.w == doctest::Approx(prev.x / prev.w).epsilon(1e-3));
        CHECK(mapped.y / mapped.w == doctest::Approx(prev.y / prev.w).epsilon(1e-3));
    }

    // Неподвижная камера: история лежит ровно в том же месте.
    Mat4 same;
    REQUIRE(reprojectionMatrix(curVp, curVp, same));
    const Vec4 clip = transform(curVp, {2, 1, -9});
    const Vec4 mapped = transform(same, {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w});
    CHECK(mapped.x / mapped.w == doctest::Approx(clip.x / clip.w).epsilon(1e-3));

    Mat4 zero;
    for (float& v : zero.m) v = 0;
    Mat4 out;
    CHECK_FALSE(reprojectionMatrix(zero, curVp, out));
}

namespace {
std::uint32_t be32(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 | static_cast<std::uint32_t>(p[2]) << 8 | p[3]; }
}  // namespace

TEST_CASE("PNG: контрольные суммы на известных значениях") {
    const std::uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc32(digits, sizeof(digits)) == 0xCBF43926u);
    CHECK(crc32(digits, 0) == 0u);
    const std::uint8_t wiki[] = {'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'};
    CHECK(adler32(wiki, sizeof(wiki)) == 0x11E60398u);
    CHECK(adler32(wiki, 0) == 1u);
}

TEST_CASE("PNG: структура файла — сигнатура, IHDR, все CRC блоков, поток deflate") {
    const std::vector<std::uint8_t> pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30, 40};  // 2x2 RGBA
    const auto png = encodePng(2, 2, 4, pixels.data());
    REQUIRE(png.size() > 40);
    const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) CHECK(png[static_cast<std::size_t>(i)] == signature[i]);

    std::size_t pos = 8;
    std::vector<std::string> kinds;
    std::vector<std::uint8_t> idat;
    while (pos < png.size()) {
        const std::uint32_t length = be32(&png[pos]);
        REQUIRE(pos + 12 + length <= png.size());
        const std::uint8_t* body = &png[pos + 4];  // тип и данные
        CHECK(crc32(body, 4 + length) == be32(&png[pos + 8 + length]));
        kinds.emplace_back(reinterpret_cast<const char*>(body), 4);
        if (kinds.back() == "IHDR") {
            CHECK(be32(body + 4) == 2);
            CHECK(be32(body + 8) == 2);
            CHECK(body[12] == 8);  // бит на канал
            CHECK(body[13] == 6);  // RGBA
        }
        if (kinds.back() == "IDAT") idat.assign(body + 4, body + 4 + length);
        pos += 12 + length;
    }
    CHECK(kinds == std::vector<std::string>{"IHDR", "IDAT", "IEND"});

    // zlib: 78 01, один финальный несжатый блок на (1 + 8) * 2 = 18 байт, затем Adler-32 этих данных.
    REQUIRE(idat.size() == 2 + 5 + 18 + 4);
    CHECK(idat[0] == 0x78);
    CHECK(idat[2] == 1);
    CHECK((idat[3] | idat[4] << 8) == 18);
    CHECK((idat[5] | idat[6] << 8) == (~18 & 0xFFFF));
    CHECK(idat[7] == 0);  // строка начинается с байта фильтра 0
    CHECK(idat[8] == 255);
    CHECK(be32(&idat[idat.size() - 4]) == adler32(&idat[7], 18));
}

TEST_CASE("PNG: большое изображение режется на несколько блоков, неверные входы отклоняются") {
    const std::vector<std::uint8_t> rgb(300 * 300 * 3, 77);
    const auto png = encodePng(300, 300, 3, rgb.data());
    // Данные: (1 + 900) * 300 = 270 300 байт → блоки по 65 535: четыре полных и остаток.
    std::size_t pos = 8 + 12 + 13;  // после сигнатуры и IHDR
    const std::uint32_t idatLength = be32(&png[pos]);
    CHECK(idatLength == 2 + 5 * 5 + 270300 + 4);
    const std::uint8_t* z = &png[pos + 8];
    CHECK(z[2] == 0);  // первый блок не последний
    CHECK(z[2 + 5 + 65535] == 0);
    CHECK(z[2 + 5 * 5 + 4 * 65535 - 5] == 1);  // последний блок помечен

    CHECK(encodePng(0, 4, 4, rgb.data()).empty());
    CHECK(encodePng(4, 4, 2, rgb.data()).empty());
    CHECK(encodePng(4, 4, 3, nullptr).empty());
    std::string err;
    CHECK_FALSE(writePng("Z:/нет/такой/папки/x.png", 4, 4, 3, rgb.data(), err));
    CHECK_FALSE(err.empty());
}
