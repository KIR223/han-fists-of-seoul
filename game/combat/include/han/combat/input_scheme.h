// han/combat/input_scheme.h — «простая» схема управления боем (часть 17 дизайна): мышь и клавиши → FighterInput.
// ЛКМ — удары (ритм нажатий строит серию, направление движения выбирает ветку), удержание ЛКМ — сильный удар, ПКМ — блок / парирование,
// Space — уклон (сам выбирается по летящему удару), R — спецприём. Схема не знает о мире: только о бойце, враге и симуляции.
#pragma once
#include "han/combat/sim.h"

namespace han::combat {
// Состояние клавиш и кнопок на один кадр. forward/right — −1..1 относительно врага (W = к врагу).
struct RawInput {
    bool lmbPressed = false, lmbHeld = false;
    bool rmbPressed = false, rmbHeld = false;
    bool spacePressed = false, specialPressed = false;
    bool shoulderPressed = false;  // Q + ← / → : шолдер-ролл (сторона по right; без направления — вправо)
    bool dash = false;
    float forward = 0, right = 0;
};

struct SchemeSettings {
    int chainResetFrames = 50;  // пауза между ударами дольше этого начинает серию заново (~0,8 с)
    int heavyHoldFrames = 20;   // удержание ЛКМ дольше этого — сильный удар (~0,33 с)
};

class SimpleScheme {
public:
    SimpleScheme(const MoveLibrary& moves, const std::string& style = "boxing", const SchemeSettings& settings = {});

    // Раз в кадр: ввод бойца self. sim нужен, чтобы видеть летящий удар врага и фокус.
    FighterInput map(const RawInput& raw, const CombatSim& sim, int self);

    // Что выбрала схема в последний раз (для подсказок интерфейса и тестов).
    const MoveData* lastAttack() const { return lastAttack_; }
    DefenseAction lastDefense() const { return lastDefense_; }
    int chainIndex() const { return chain_; }

private:
    const MoveData* find(const char* suffix) const;

    const MoveLibrary& moves_;
    std::string style_;
    SchemeSettings s_;
    int chain_ = 0, lastAttackFrame_ = -1000, holdFrames_ = 0;
    bool heavyFired_ = false;
    const MoveData* lastAttack_ = nullptr;
    DefenseAction lastDefense_ = DefenseAction::None;
};
}  // namespace han::combat
// Пример: SimpleScheme scheme(moves); sim.step({scheme.map(raw, sim, 0), ai.decide(sim, 1)});
