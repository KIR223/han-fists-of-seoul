// han/player/camera_rig.h — камера третьего лица (E6): орбита вокруг персонажа, ограничение наклона, плавное следование,
// столкновение со стеной (камера не уходит за геометрию), расширение угла обзора на беге. Работает через contracts::IPhysicsQuery.
// Соглашения как у камеры движка: yaw 0 смотрит вдоль −Z, положительный yaw — к +X; pitch > 0 — вверх.
#pragma once
#include "contracts/physics_query.h"

namespace han::player {
struct CameraRigSettings {
    float distance = 3.6f;          // м от точки опоры до камеры
    float minDistance = 0.6f;       // ближе камера не подходит, даже в узком проходе
    float pivotHeight = 1.55f;      // высота точки опоры над ногами персонажа
    float shoulderOffset = 0.35f;   // смещение вправо: персонаж чуть левее центра кадра
    float minPitch = -1.15f, maxPitch = 1.25f;  // радианы
    float sensitivity = 0.0025f;    // радиан на пиксель
    float followSharpness = 14.f;   // чем больше, тем жёстче держится за персонажем (1/с)
    float collisionMargin = 0.25f;  // отступ от стены, м
    float baseFov = 1.0f;           // радианы
    float sprintFovBonus = 0.12f;
    float fovSharpness = 6.f;
};

class CameraRig {
public:
    explicit CameraRig(const CameraRigSettings& settings = {}) : s_(settings), distance_(settings.distance), fov_(settings.baseFov) {}

    // Поворот мышью: dx вправо — вправо, dy вниз — взгляд вниз (как Camera::look).
    void look(float dx, float dy);
    void setAngles(float yaw, float pitch);
    // Раз в кадр. feet — позиция ног персонажа; speedFraction 0..1 (доля от бега) расширяет обзор; query может быть nullptr (без столкновений).
    void update(float dt, contracts::Vec3 feet, float speedFraction, const contracts::IPhysicsQuery* query);
    // Сразу поставить камеру за персонажем (после телепорта и при старте).
    void snapTo(contracts::Vec3 feet, const contracts::IPhysicsQuery* query);

    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    float fov() const { return fov_; }
    contracts::Vec3 position() const { return position_; }
    contracts::Vec3 forward() const;
    contracts::Vec3 pivot() const { return pivot_; }
    const CameraRigSettings& settings() const { return s_; }

private:
    contracts::Vec3 desiredPosition(contracts::Vec3 pivot) const;
    contracts::Vec3 resolve(contracts::Vec3 pivot, contracts::Vec3 desired, const contracts::IPhysicsQuery* query);

    CameraRigSettings s_;
    float yaw_ = 0.f, pitch_ = -0.25f, distance_, fov_;
    contracts::Vec3 position_, pivot_;
    bool initialized_ = false;
};
}  // namespace han::player
// Пример: CameraRig rig; rig.look(mouseDx, mouseDy); rig.update(dt, feet, speed01, &world); camera.position = rig.position(); camera.yaw = rig.yaw();
