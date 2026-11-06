// han/render/vehicle_pose.h — положение корпуса машины на дороге (этап 05 плана города): высота, шаг (нос вверх/вниз) и крен по четырём точкам
// контакта колёс, подвеска-сглаживание во времени. Только CPU, без Vulkan. Высоты точек контакта даёт вызывающий: рельеф или полотно моста.
// Оси модели: нос смотрит по -Z (курс 0 — на север), правый борт — +X, вверх — +Y; начало координат модели на дороге под центром колёсной базы.
#pragma once
#include "han/core/math.h"

namespace han::render {

// Высоты поверхности (абсолютные, м) под колёсами.
struct WheelHeights {
    float frontLeft = 0.f, frontRight = 0.f, rearLeft = 0.f, rearRight = 0.f;
};

struct VehiclePose {
    float y = 0.f;      // высота начала координат модели (основание кузова над дорогой в центре базы)
    float pitch = 0.f;  // радианы, > 0 — нос выше кормы
    float roll = 0.f;   // радианы, > 0 — правый борт выше левого
};

struct VehiclePoseLimits {
    float maxPitch = 0.6f;  // около 34°: выше — выброс данных рельефа, а не дорога
    float maxRoll = 0.4f;
};

// Плоскость по четырём точкам контакта: шаг по осредняющему наклону вдоль базы, крен — поперёк колеи. Высота — по самому высокому колесу:
// ни одно колесо не уходит в дорогу, остальные могут зависнуть на сантиметры (на бугре они «ловят» подвеской).
VehiclePose solveVehiclePose(const WheelHeights& wheels, float wheelbase, float track, const VehiclePoseLimits& limits = {});

// Матрица модели: перенос в (x, pose.y + lift, z), курс города (0 — на север), затем шаг и крен.
core::Mat4 vehicleTransform(float x, float z, float heading, const VehiclePose& pose, float lift = 0.f);

// Подвеска: первый порядок с постоянной времени tau — следует за дорогой без броска и перелёта; на сглаживаемых значениях нет скачков при
// резкой смене уклона (ровно → склон → вершина). Свежая машина (первый вызов) берёт цель сразу.
class VehiclePoseFilter {
public:
    explicit VehiclePoseFilter(float tau = 0.12f) : tau_(tau) {}
    VehiclePose update(const VehiclePose& target, float dt);
    void reset() { started_ = false; }
    const VehiclePose& value() const { return value_; }

private:
    float tau_;
    bool started_ = false;
    VehiclePose value_;
};

// Кинематика колёс: качение от пройденного пути и поворот передних колёс от траектории (велосипедная модель: tan(угол) = база * скорость курса / скорость).
// Готова для моделей с отдельными колёсами и пивотами; на слитом меше (как процедурная машина) применить нечему — подключается вместе с такими моделями.
struct WheelAnimation {
    float rollAngle = 0.f;  // накопленный угол качения, радианы (нормируется в [0, 2π))
    float steer = 0.f;      // угол поворота передних колёс, радианы, > 0 — вправо
};
WheelAnimation advanceWheels(const WheelAnimation& previous, float speed, float yawRate, float dt, float wheelRadius, float wheelbase, float maxSteer = 0.6f);

}  // namespace han::render
