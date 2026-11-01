// contracts/combat_events.h — поставщик: B, потребители: A (звук, эффекты, реакции толпы, полиция, розыск).
// Зачем: любой удар сообщает миру, что произошло, не зная, кто слушает.
#pragma once
#include <cstdint>

#include "contracts/common.h"

namespace han::contracts {
enum class HitZone : std::uint8_t { Head, Torso, Legs };

struct HitEvent {
    EntityId attacker = kNoEntity, defender = kNoEntity;
    HitZone zone = HitZone::Torso;
    float damage = 0, impulse = 0;  // импульс — для физики и звука
    Vec3 point;
    bool blocked = false, counter = false;
};
struct KnockdownEvent {
    EntityId fighter = kNoEntity, cause = kNoEntity;
};
struct FinisherEvent {
    EntityId attacker = kNoEntity, defender = kNoEntity;
};
struct DeathEvent {
    EntityId fighter = kNoEntity, killer = kNoEntity;
};

struct ICombatEventSink {
    virtual void onHit(const HitEvent&) = 0;
    virtual void onKnockdown(const KnockdownEvent&) = 0;
    virtual void onFinisher(const FinisherEvent&) = 0;
    virtual void onDeath(const DeathEvent&) = 0;
    virtual ~ICombatEventSink() = default;
};
}  // namespace han::contracts
// Пример: combat.events().subscribe(&mySink);
