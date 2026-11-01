// han/player/player_controller.h — управление персонажем (E6): ходьба, бег, спринт относительно камеры, плавный разгон, прыжок с буфером и «койот-временем»,
// паркур v1: перепрыгивание низких препятствий и запрыгивание на уступы. Работает только через контракты ICharacterController и IPhysicsQuery.
// Паркур v1 — расчётные прыжки без анимации захвата (анимации — этап A1/A4): персонаж подпрыгивает ровно настолько, чтобы пройти преграду.
#pragma once
#include "contracts/character_controller.h"
#include "contracts/physics_query.h"

namespace han::player {
struct PlayerSettings {
    float walkSpeed = 2.0f, runSpeed = 4.6f, sprintSpeed = 7.0f;  // м/с
    float acceleration = 28.f, deceleration = 36.f;               // м/с² на земле
    float airControl = 0.35f;                                      // доля управляемости в воздухе
    float jumpSpeed = 5.2f;                                        // вертикальная скорость прыжка, м/с
    float gravity = 9.81f;                                         // должна совпадать с физикой мира
    float mass = 75.f;                                             // для импульса прыжка
    float coyoteTime = 0.12f;                                      // можно прыгнуть сразу после схода с края
    float jumpBuffer = 0.14f;                                      // нажатие чуть раньше приземления сработает
    float turnSharpness = 16.f;                                    // как быстро тело поворачивается по ходу движения
    // Паркур
    bool parkour = true;
    float probeDistance = 0.9f;                                    // как далеко впереди ищем препятствие
    float minLedge = 0.5f;                                         // ниже — это ступенька, её берёт сам контроллер
    float vaultMaxHeight = 1.3f;                                   // до этой высоты — перепрыгивание
    float mantleMaxHeight = 2.4f;                                  // до этой высоты — запрыгивание на уступ
    float clearance = 0.3f;                                        // запас над краем при расчёте прыжка
    float headroom = 1.9f;                                         // место над уступом, чтобы встать
    float vaultMinSpeed = 2.5f;                                    // перепрыгивание — только на ходу
    float parkourCooldown = 0.6f;
};

struct PlayerInput {
    float moveX = 0, moveZ = 0;  // вправо, вперёд (−1..1), относительно камеры
    bool sprint = false;
    bool jumpPressed = false;    // нажатие в этом кадре
    bool jumpHeld = false;
};

enum class ParkourKind : unsigned char { None, Vault, Mantle };
enum class PlayerAction : unsigned char { Idle, Walking, Running, Sprinting, Airborne, Vaulting, Mantling };

struct LedgeInfo {
    ParkourKind kind = ParkourKind::None;
    float topHeight = 0;   // высота верхней грани над ногами, м
    contracts::Vec3 forward;  // направление к препятствию (горизонтальное, единичное)
};

class PlayerController {
public:
    PlayerController(contracts::EntityId id, contracts::ICharacterController& character, const contracts::IPhysicsQuery& query, const PlayerSettings& settings = {})
        : id_(id), character_(character), query_(query), s_(settings) {}

    // Раз в кадр (до шага физики). cameraYaw — куда смотрит камера, радианы.
    void update(float dt, const PlayerInput& input, float cameraYaw);

    contracts::CharacterState state() const { return character_.state(id_); }
    PlayerAction action() const { return action_; }
    float facingYaw() const { return facing_; }
    float horizontalSpeed() const;
    // Доля от скорости бега: для камеры (расширение обзора) и анимации.
    float speedFraction() const;
    // Последнее найденное препятствие впереди (для отладки и подсказок).
    const LedgeInfo& lastLedge() const { return ledge_; }

    // Поиск препятствия впереди по направлению dir (горизонтальное, единичное) от ног feet.
    static LedgeInfo probe(const contracts::IPhysicsQuery& query, contracts::Vec3 feet, contracts::Vec3 dir, const PlayerSettings& s);
    // Вертикальная скорость прыжка, чтобы подняться на высоту h над ногами: sqrt(2 g h).
    static float jumpSpeedForHeight(float h, float gravity);

private:
    void startJump(float verticalSpeed, PlayerAction action);

    contracts::EntityId id_;
    contracts::ICharacterController& character_;
    const contracts::IPhysicsQuery& query_;
    PlayerSettings s_;
    PlayerAction action_ = PlayerAction::Idle;
    contracts::Vec3 velocity_;  // горизонтальная желаемая скорость (сглаженная)
    float facing_ = 0.f;
    float sinceGrounded_ = 1.f, jumpBufferLeft_ = 0.f, cooldown_ = 0.f, parkourTimer_ = 0.f;
    contracts::Vec3 parkourDir_;
    float parkourSpeed_ = 0.f;
    LedgeInfo ledge_;
};
}  // namespace han::player
// Пример: PlayerController pc(id, physics, physics, {}); pc.update(dt, {moveX, moveZ, sprint, jumpPressed}, camera.yaw()); physics.step(dt);
