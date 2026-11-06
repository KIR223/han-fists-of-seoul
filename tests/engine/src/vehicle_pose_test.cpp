// Этап 05: поза корпуса машины — шаг и крен по колёсам, подвеска, высоты осей на мосту.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "han/render/street_models.h"
#include "han/render/vehicle_pose.h"

using namespace han;
using namespace han::render;

namespace {
core::Vec3 apply(const core::Mat4& m, core::Vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}
constexpr float kBase = 2.6f, kTrack = 1.5f;
}  // namespace

TEST_CASE("vehicle_pose: ровная дорога — без шага и крена, высота равна дороге") {
    const auto p = solveVehiclePose({5.f, 5.f, 5.f, 5.f}, kBase, kTrack);
    CHECK(p.pitch == doctest::Approx(0.f));
    CHECK(p.roll == doctest::Approx(0.f));
    CHECK(p.y == doctest::Approx(5.f));
}

TEST_CASE("vehicle_pose: подъём 10% вдоль курса поднимает нос, поперечный уклон поднимает правый борт; знаки совпадают с осями модели") {
    const float rise = 0.1f * kBase;
    const auto up = solveVehiclePose({rise, rise, 0.f, 0.f}, kBase, kTrack);  // впереди выше
    CHECK(up.pitch == doctest::Approx(std::atan(0.1f)).epsilon(0.01));
    CHECK(up.roll == doctest::Approx(0.f).scale(1.0));
    const auto bank = solveVehiclePose({0.f, 0.2f, 0.f, 0.2f}, kBase, kTrack);  // правый борт выше
    CHECK(bank.roll == doctest::Approx(std::atan2(0.2f, kTrack)).epsilon(0.01));
    // Матрица: при положительном шаге точка носа (по -Z) оказывается выше, при положительном крене — точка правого борта (+X).
    const auto nose = apply(vehicleTransform(0.f, 0.f, 0.f, up), {0.f, 0.f, -1.f});
    CHECK(nose.y > 0.05f);
    const auto side = apply(vehicleTransform(0.f, 0.f, 0.f, bank), {1.f, 0.f, 0.f});
    CHECK(side.y > 0.05f);
    // Курс: без шага и крена преобразование совпадает со старым streetModelTransform (курс 90° — нос на восток, +X).
    const auto east = apply(vehicleTransform(10.f, 20.f, 1.5707963f, VehiclePose{}, 0.f), {0.f, 0.f, -1.f});
    CHECK(east.x == doctest::Approx(11.f).epsilon(0.001));
    CHECK(east.z == doctest::Approx(20.f).epsilon(0.001));
    const auto legacy = apply(streetModelTransform({10.f, 0.f, 20.f}, 1.5707963f), {0.f, 0.f, -1.f});
    CHECK(legacy.x == doctest::Approx(east.x));
    CHECK(legacy.z == doctest::Approx(east.z));
}

TEST_CASE("vehicle_pose: ни одно колесо не уходит в дорогу на неровной поверхности, зависание мало") {
    unsigned seed = 12345;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<float>((seed >> 8) & 0xffff) / 65535.f; };
    float worstSink = 0.f, worstHover = 0.f;
    for (int i = 0; i < 2000; ++i) {
        const float base = rnd() * 40.f - 10.f;
        const WheelHeights w{base + rnd() * 0.5f, base + rnd() * 0.5f, base + rnd() * 0.5f, base + rnd() * 0.5f};  // бугристая поверхность до 0,5 м
        const auto pose = solveVehiclePose(w, kBase, kTrack);
        const auto m = vehicleTransform(0.f, 0.f, 0.f, pose);
        const float tops[4] = {apply(m, {-kTrack / 2, 0.f, -kBase / 2}).y, apply(m, {kTrack / 2, 0.f, -kBase / 2}).y, apply(m, {-kTrack / 2, 0.f, kBase / 2}).y,
                               apply(m, {kTrack / 2, 0.f, kBase / 2}).y};
        const float ground[4] = {w.frontLeft, w.frontRight, w.rearLeft, w.rearRight};
        for (int k = 0; k < 4; ++k) {
            worstSink = std::max(worstSink, ground[k] - tops[k]);   // > 0 — колесо ниже дороги
            worstHover = std::max(worstHover, tops[k] - ground[k]); // > 0 — колесо над дорогой
        }
    }
    CHECK(worstSink < 1e-3f);   // не проваливается
    CHECK(worstHover <= 0.5f + 1e-3f);  // на перекошенной (непланарной) поверхности колесо висит не больше амплитуды бугра
}

TEST_CASE("vehicle_pose: на плоской (планарной) дороге любого наклона все четыре колеса касаются дороги") {
    unsigned seed = 777;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<float>((seed >> 8) & 0xffff) / 65535.f; };
    float worst = 0.f;
    for (int i = 0; i < 500; ++i) {
        const float h0 = rnd() * 30.f, along = (rnd() - 0.5f) * 0.4f, across = (rnd() - 0.5f) * 0.3f;  // уклон вдоль до ±20%, поперёк до ±15%
        const WheelHeights w{h0 + along * kBase * 0.5f - across * kTrack * 0.5f, h0 + along * kBase * 0.5f + across * kTrack * 0.5f,
                             h0 - along * kBase * 0.5f - across * kTrack * 0.5f, h0 - along * kBase * 0.5f + across * kTrack * 0.5f};
        const auto m = vehicleTransform(0.f, 0.f, 0.f, solveVehiclePose(w, kBase, kTrack));
        const float tops[4] = {apply(m, {-kTrack / 2, 0.f, -kBase / 2}).y, apply(m, {kTrack / 2, 0.f, -kBase / 2}).y, apply(m, {-kTrack / 2, 0.f, kBase / 2}).y,
                               apply(m, {kTrack / 2, 0.f, kBase / 2}).y};
        const float ground[4] = {w.frontLeft, w.frontRight, w.rearLeft, w.rearRight};
        for (int k = 0; k < 4; ++k) worst = std::max(worst, std::fabs(tops[k] - ground[k]));
    }
    CHECK(worst < 0.02f);  // угол по осредняющему наклону и поворот на шаг/крен не создают заметного зазора
}

TEST_CASE("vehicle_pose: выброс данных ограничен — шаг и крен не больше пределов") {
    const auto p = solveVehiclePose({50.f, 50.f, 0.f, 0.f}, kBase, kTrack);
    CHECK(p.pitch <= VehiclePoseLimits{}.maxPitch + 1e-6f);
    const auto r = solveVehiclePose({0.f, 50.f, 0.f, 50.f}, kBase, kTrack);
    CHECK(r.roll <= VehiclePoseLimits{}.maxRoll + 1e-6f);
}

TEST_CASE("vehicle_pose: подвеска — первый вызов берёт цель, дальше приближение без перелёта, за секунду почти у цели") {
    VehiclePoseFilter f(0.12f);
    const VehiclePose a{1.f, 0.f, 0.f}, b{2.f, 0.2f, -0.1f};
    CHECK(f.update(a, 1.f / 60.f).y == doctest::Approx(1.f));  // первый вызов
    float lastY = 1.f, lastPitch = 0.f;
    for (int i = 0; i < 60; ++i) {
        const auto v = f.update(b, 1.f / 60.f);
        CHECK(v.y >= lastY - 1e-6f);      // монотонно вверх
        CHECK(v.y <= b.y + 1e-6f);        // без перелёта
        CHECK(v.pitch >= lastPitch - 1e-6f);
        CHECK(v.pitch <= b.pitch + 1e-6f);
        lastY = v.y;
        lastPitch = v.pitch;
    }
    CHECK(f.value().y == doctest::Approx(b.y).epsilon(0.01));
    CHECK(f.value().roll == doctest::Approx(b.roll).epsilon(0.01));
    CHECK(f.update(b, 0.f).y == doctest::Approx(f.value().y));  // dt = 0 ничего не меняет
}

TEST_CASE("vehicle_pose: качение колёс от пройденного пути и поворот от траектории") {
    WheelAnimation w;
    for (int i = 0; i < 60; ++i) w = advanceWheels(w, 10.f, 0.f, 1.f / 60.f, 0.32f, kBase);
    // 10 м пути по колесу радиусом 0,32 м: 31,25 рад, в [0, 2π)
    CHECK(w.rollAngle == doctest::Approx(std::fmod(31.25f, 6.28318530718f)).epsilon(0.001));
    CHECK(w.steer == doctest::Approx(0.f));
    // Поворот по дуге радиуса R: угол колёс atan(база / R); при скорости 8 м/с и R = 10 м скорость курса 0,8 рад/с.
    const auto turn = advanceWheels({}, 8.f, 0.8f, 0.016f, 0.32f, kBase);
    CHECK(turn.steer == doctest::Approx(std::atan(kBase / 10.f)).epsilon(0.001));
    CHECK(advanceWheels({}, 8.f, -0.8f, 0.016f, 0.32f, kBase).steer == doctest::Approx(-turn.steer));  // налево — отрицательный
    // Остановка: поворот ограничен, качение не идёт.
    const auto stopped = advanceWheels({1.f, 0.f}, 0.f, 5.f, 0.016f, 0.32f, kBase);
    CHECK(stopped.rollAngle == doctest::Approx(1.f));
    CHECK(std::fabs(stopped.steer) <= 0.6f + 1e-6f);
    // Назад: угол качения нормируется в положительный диапазон.
    CHECK(advanceWheels({0.1f, 0.f}, -10.f, 0.f, 0.1f, 0.32f, kBase).rollAngle >= 0.f);
}

TEST_CASE("vehicle_pose: проезд гребня и впадины — наклон меняется плавно, колёса не проникают, фильтр не даёт скачков") {
    // Профиль: подъём 10% до x = 0, затем спуск 10%; затем впадина (перелом вверх) в x = 40.
    const auto height = [](float x) {
        if (x < 0.f) return 0.1f * x;
        if (x < 40.f) return -0.1f * x;
        return -4.f + 0.1f * (x - 40.f);
    };
    VehiclePoseFilter filter(0.12f);
    float prevPitchRaw = 0.f, prevPitchFiltered = 0.f, maxRawStep = 0.f, maxFilteredStep = 0.f, worstSink = 0.f;
    bool first = true;
    const float speed = 15.f, dt = 1.f / 60.f;
    for (float x = -30.f; x < 70.f; x += speed * dt) {
        const WheelHeights w{height(x + kBase / 2), height(x + kBase / 2), height(x - kBase / 2), height(x - kBase / 2)};
        const auto raw = solveVehiclePose(w, kBase, kTrack);
        const auto smooth = filter.update(raw, dt);
        const auto m = vehicleTransform(0.f, 0.f, 0.f, raw);
        worstSink = std::max(worstSink, w.frontLeft - apply(m, {-kTrack / 2, 0.f, -kBase / 2}).y);
        if (!first) {
            maxRawStep = std::max(maxRawStep, std::fabs(raw.pitch - prevPitchRaw));
            maxFilteredStep = std::max(maxFilteredStep, std::fabs(smooth.pitch - prevPitchFiltered));
        }
        first = false;
        prevPitchRaw = raw.pitch;
        prevPitchFiltered = smooth.pitch;
    }
    CHECK(worstSink < 1e-3f);
    CHECK(maxRawStep < 0.05f);                 // даже «сырой» шаг меняется непрерывно (база больше шага кадра)
    CHECK(maxFilteredStep <= maxRawStep + 1e-6f);  // подвеска скачков не добавляет
}
