// han/combat/rules.h — правила боя из data/combat/combat_rules.json (G1): формулы урона и ресурсов, углы, таблица исходов «защита против траектории».
// Ничего из чисел не зашито в код: ИИ и игрок пользуются одними и теми же правилами (часть 22.6 дизайна).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "han/combat/move_data.h"
#include "han/core/json.h"

namespace han::combat {
enum class OutcomeKind : unsigned char { Hit, Block, Evade, Partial, Perfect };

struct Outcome {
    OutcomeKind kind = OutcomeKind::Hit;
    float multiplier = 1.f;  // «попадание ×1,3»: ошибка в выборе защиты наказывается сильнее
    bool angle = false;      // «уход + угол»: защитник выходит на выгодный угол и получает окно контратаки
};

// Колонки таблицы исходов (часть 22.6). Дуга «с той же стороны» — это удар, навстречу которому шагнул защитник.
enum class TrajClass : unsigned char { Straight, ArcSame, ArcOther, Up, Down, Low };

struct DefenseDef {
    std::string name;
    std::vector<Height> covers;  // от ударов какой высоты защищает вообще
    bool hold = false;           // удерживаемая (блок) или одноразовое действие
    bool sided = false;          // направление влево/вправо важно (слип, сайдстеп, нырок с уходом)
    int startup = 0;             // для удерживаемых: через сколько кадров блок «встал»
    int total = 0;               // для действий: полная длительность в кадрах
    int windowFrom = 0, windowTo = 0;  // кадры действия, в которые оно спасает
    int perfectUntil = -1;       // для парирования: до этого кадра включительно — идеально
    float cost = 0;              // стамина
    float shift = 0;             // на сколько метров защитник смещается за действие
    Outcome outcomes[6];

    bool covered(Height h) const;
    const Outcome& outcome(TrajClass c) const { return outcomes[static_cast<int>(c)]; }
};

struct CombatRules {
    int fps = 60;
    // Урон
    float strengthPerPoint = 0.05f, toughnessPerPoint = 0.03f;
    float star[3] = {1.f, 1.1f, 1.2f};
    float zoneHead = 1.15f, zoneTorso = 1.f, zoneLegs = 0.85f;
    float counter = 1.5f, perfectParryCounter = 2.f, back = 1.25f, downed = 0.6f, crit = 1.5f, flow = 1.3f;
    float blockTaken = 0.7f, blockTakenPower = 0.4f, partialTaken = 0.5f;
    float critBase = 0.03f, critTechniquePerPoint = 0.02f;
    // Ресурсы
    float enduranceStaminaPerPoint = 0.04f, starStaminaDiscount = 0.1f;
    float staminaRegen = 18.f, staminaRegenBlock = 6.f, retreatRegenMultiplier = 1.5f;
    float balanceMax = 100.f, balanceRegen = 12.f, strengthBalancePerPoint = 0.04f;
    // Нокдаун
    float headWindowSeconds = 3.f, knockdownBase = 25.f, knockdownToughnessPerPoint = 3.f;
    int downFrames = 120, getupFrames = 40, knockdownsToKo = 3;
    // Движение и геометрия
    float walkSpeed = 1.8f, dashSpeed = 3.6f, bodyRadius = 0.3f;
    float stepInRange = 0.4f, stepInDamage = 1.1f, stepInStamina = 1.15f, retreatDamage = 0.8f, sidestepBlockEffect = 0.7f;
    float arcStraight = 22.f, arcArc = 45.f, arcUp = 30.f, arcDown = 30.f, arcSpin = 180.f, arcLow = 30.f, arcPush = 30.f;
    struct AngleStep {
        float maxDegrees, factor;
    };
    std::vector<AngleStep> blockByAngle;
    // Бокс: «движение головой» (заряд за успешный уклон усиливает следующий удар) и «работа по корпусу» (накопленный урон сгибает врага)
    float headMoveBonus = 0.1f;
    int headMoveMaxCharges = 5, headMoveDecayFrames = 240;
    float bodyWorkThreshold = 28.f, bodyWorkDecayPerSecond = 1.5f;
    int bodyDoubledFrames = 50;
    // Ритм: серия чистых попаданий без пропущенных ударов открывает «Поток» (урон ×flow)
    int rhythmHitsForFlow = 5, flowFrames = 360, rhythmDecayFrames = 240;
    int inputBufferFrames = 8;
    std::map<std::string, DefenseDef> defenses;

    bool parse(const core::Json& json, std::string& error);
    bool load(const std::string& file, std::string& error);

    const DefenseDef* defense(const std::string& name) const;
    // Доля эффективности блока по углу к атакующему (0° — спереди, 180° — со спины).
    float blockFactorForAngle(float degrees) const;
    // Полуугол поражения для траектории, градусы.
    float hitArcDegrees(Trajectory t) const;
    float zoneMultiplier(Height h) const { return h == Height::Head ? zoneHead : (h == Height::Torso ? zoneTorso : zoneLegs); }
    // Порог накопленного урона в голову для нокдауна.
    float knockdownThreshold(float toughness) const { return knockdownBase + knockdownToughnessPerPoint * (toughness - 5.f); }
};

// Колонка таблицы для траектории удара и стороны защитника (−1 влево, +1 вправо, 0 — без стороны).
TrajClass classifyTrajectory(Trajectory t, int defenderSide);
// Разбор строки исхода: "block", "hit", "evade", "evade_angle", "partial", "perfect", "hit*1.3".
bool parseOutcome(const std::string& text, Outcome& out);
}  // namespace han::combat
// Пример: CombatRules r; std::string e; r.load("data/combat/combat_rules.json", e); const Outcome& o = r.defense("slip")->outcome(TrajClass::Straight);
