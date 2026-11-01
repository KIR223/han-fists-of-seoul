#include "han/player/player_controller.h"

#include <algorithm>
#include <cmath>

namespace han::player {
using contracts::Vec3;

namespace {
float hypot2(float x, float z) { return std::sqrt(x * x + z * z); }
float damp(float sharpness, float dt) { return 1.f - std::exp(-sharpness * dt); }
float wrapPi(float a) {
    const float twoPi = 6.2831853f;
    while (a > 3.1415927f) a -= twoPi;
    while (a < -3.1415927f) a += twoPi;
    return a;
}
}  // namespace

float PlayerController::jumpSpeedForHeight(float h, float gravity) { return std::sqrt(2.f * gravity * std::max(h, 0.f)); }

float PlayerController::horizontalSpeed() const {
    const Vec3 v = character_.state(id_).velocity;
    return hypot2(v.x, v.z);
}

float PlayerController::speedFraction() const { return std::clamp(horizontalSpeed() / s_.sprintSpeed, 0.f, 1.f); }

LedgeInfo PlayerController::probe(const contracts::IPhysicsQuery& query, Vec3 feet, Vec3 dir, const PlayerSettings& s) {
    LedgeInfo info;
    const float len = hypot2(dir.x, dir.z);
    if (len < 1e-4f) return info;
    dir = {dir.x / len, 0.f, dir.z / len};
    info.forward = dir;
    // Горизонтальные лучи на разных высотах: ближайший упор — лицо препятствия.
    const float heights[] = {0.55f, 1.0f, 1.5f, 2.0f, 2.4f};
    float best = 1e9f;
    Vec3 hitPoint;
    for (const float h : heights) {
        const Vec3 from{feet.x, feet.y + h, feet.z};
        const Vec3 to{from.x + dir.x * s.probeDistance, from.y, from.z + dir.z * s.probeDistance};
        const contracts::RayHit hit = query.raycast(from, to);
        if (hit.hit && std::fabs(hit.normal.y) < 0.5f && hit.distance < best) {  // вертикальная стена, не пологий склон
            best = hit.distance;
            hitPoint = hit.point;
        }
    }
    if (best > 1e8f) return info;
    // Верхняя грань: луч сверху вниз чуть «внутри» препятствия, за его лицом.
    const float startAbove = s.mantleMaxHeight + 0.8f;
    const Vec3 top{hitPoint.x + dir.x * 0.18f, feet.y + startAbove, hitPoint.z + dir.z * 0.18f};
    const contracts::RayHit down = query.raycast(top, {top.x, feet.y - 0.1f, top.z});
    if (!down.hit) return info;
    const float height = down.point.y - feet.y;
    if (height < s.minLedge || height > s.mantleMaxHeight) return info;
    // Над уступом должно хватать места, чтобы встать.
    const contracts::RayHit roof = query.raycast({down.point.x, down.point.y + 0.05f, down.point.z}, {down.point.x, down.point.y + s.headroom, down.point.z});
    if (roof.hit) return info;
    info.topHeight = height;
    info.kind = height <= s.vaultMaxHeight ? ParkourKind::Vault : ParkourKind::Mantle;
    return info;
}

void PlayerController::startJump(float verticalSpeed, PlayerAction action) {
    character_.applyImpulse(id_, {0.f, verticalSpeed * s_.mass, 0.f});
    action_ = action;
    jumpBufferLeft_ = 0.f;
    sinceGrounded_ = 1.f;  // койот-время израсходовано: второй прыжок в воздухе невозможен
}

void PlayerController::update(float dt, const PlayerInput& in, float cameraYaw) {
    dt = std::max(dt, 1e-4f);
    const contracts::CharacterState st = character_.state(id_);
    sinceGrounded_ = st.grounded ? 0.f : sinceGrounded_ + dt;
    cooldown_ = std::max(0.f, cooldown_ - dt);
    parkourTimer_ = std::max(0.f, parkourTimer_ - dt);
    jumpBufferLeft_ = in.jumpPressed ? s_.jumpBuffer : std::max(0.f, jumpBufferLeft_ - dt);

    // Желаемое направление: ввод в осях камеры (вправо, вперёд) → мировые оси.
    const float inputLen = std::min(1.f, hypot2(in.moveX, in.moveZ));
    const Vec3 fwd{std::sin(cameraYaw), 0.f, -std::cos(cameraYaw)}, right{std::cos(cameraYaw), 0.f, std::sin(cameraYaw)};
    Vec3 wish{right.x * in.moveX + fwd.x * in.moveZ, 0.f, right.z * in.moveX + fwd.z * in.moveZ};
    const float wishLen = hypot2(wish.x, wish.z);
    if (wishLen > 1e-4f) wish = {wish.x / wishLen, 0.f, wish.z / wishLen};
    const float topSpeed = in.sprint ? s_.sprintSpeed : s_.runSpeed;
    // Лёгкий наклон стика/клавиши даёт шаг: от ходьбы до бега по величине ввода.
    const float analogSpeed = inputLen < 0.5f ? s_.walkSpeed * (inputLen / 0.5f) : s_.walkSpeed + (topSpeed - s_.walkSpeed) * std::min(1.f, (inputLen - 0.5f) / 0.5f);
    const Vec3 target = inputLen > 1e-3f ? Vec3{wish.x * analogSpeed, 0.f, wish.z * analogSpeed} : Vec3{};

    if (parkourTimer_ > 0.f) {
        // Паркур: короткое время ввод не управляет движением, персонаж идёт к препятствию и через него.
        velocity_ = {parkourDir_.x * parkourSpeed_, 0.f, parkourDir_.z * parkourSpeed_};
    } else {
        if (action_ == PlayerAction::Vaulting || action_ == PlayerAction::Mantling) action_ = st.grounded ? PlayerAction::Idle : PlayerAction::Airborne;
        const float rate = (inputLen > 1e-3f ? s_.acceleration : s_.deceleration) * (st.grounded ? 1.f : s_.airControl);
        Vec3 delta{target.x - velocity_.x, 0.f, target.z - velocity_.z};
        const float dl = hypot2(delta.x, delta.z), step = rate * dt;
        if (dl > step && dl > 1e-5f) delta = {delta.x * step / dl, 0.f, delta.z * step / dl};
        velocity_ = {velocity_.x + delta.x, 0.f, velocity_.z + delta.z};
    }
    character_.setDesiredVelocity(id_, velocity_);

    // Поворот тела по ходу движения.
    const float speed = hypot2(velocity_.x, velocity_.z);
    if (speed > 0.4f) {
        const float targetYaw = std::atan2(velocity_.x, -velocity_.z);
        facing_ += wrapPi(targetYaw - facing_) * damp(s_.turnSharpness, dt);
        facing_ = wrapPi(facing_);
    }

    // Прыжок и паркур.
    const bool canJump = st.grounded || sinceGrounded_ <= s_.coyoteTime;
    if (jumpBufferLeft_ > 0.f && canJump && parkourTimer_ <= 0.f) {
        bool done = false;
        if (s_.parkour && cooldown_ <= 0.f) {
            Vec3 dir = wishLen > 1e-4f && inputLen > 0.2f ? wish : Vec3{std::sin(facing_), 0.f, -std::cos(facing_)};
            if (speed > 0.5f && wishLen > 1e-4f) dir = wish;
            ledge_ = probe(query_, st.position, dir, s_);
            const bool moving = st.grounded && (speed >= s_.vaultMinSpeed || ledge_.kind == ParkourKind::Mantle);
            if (ledge_.kind != ParkourKind::None && moving && (inputLen > 0.2f || speed > 0.5f)) {
                const float vy = jumpSpeedForHeight(ledge_.topHeight + s_.clearance, s_.gravity);
                parkourDir_ = ledge_.forward;
                if (ledge_.kind == ParkourKind::Vault) {
                    parkourSpeed_ = std::max(speed, s_.runSpeed);
                    parkourTimer_ = 2.f * vy / s_.gravity * 0.85f;  // почти весь полёт ввод не мешает
                    startJump(vy, PlayerAction::Vaulting);
                } else {
                    parkourSpeed_ = 2.4f;
                    parkourTimer_ = vy / s_.gravity + 0.55f;       // подъём и выход на уступ
                    startJump(vy, PlayerAction::Mantling);
                }
                cooldown_ = s_.parkourCooldown;
                done = true;
            }
        }
        if (!done) startJump(s_.jumpSpeed, PlayerAction::Airborne);
    }

    if (action_ != PlayerAction::Vaulting && action_ != PlayerAction::Mantling) {
        if (!st.grounded && sinceGrounded_ > 0.05f) action_ = PlayerAction::Airborne;
        else if (speed < 0.2f) action_ = PlayerAction::Idle;
        else if (speed > s_.runSpeed + 0.5f) action_ = PlayerAction::Sprinting;
        else if (speed > s_.walkSpeed + 0.8f) action_ = PlayerAction::Running;
        else action_ = PlayerAction::Walking;
    }
}
}  // namespace han::player
