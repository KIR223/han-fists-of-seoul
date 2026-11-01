// han/learning/mastery.h — путь приёма (G2): неизвестен → подсмотрен → разучен → отработан → звёзды мастерства.
// Опыт даёт тренировка (мини-игры), бой и подсмотренные приёмы врага. Пороги и множители — из data/combat/learning.json.
// Чистая логика, сохраняется в JSON (формат сейвов ведёт game/save).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "han/core/json.h"

namespace han::learning {
enum class MoveStage : unsigned char { Unknown, Seen, Remembered, Learned, Mastered };

struct LearningConfig {
    int observationsToRemember = 3;     // столько раз надо увидеть приём у врага, чтобы «запомнить»
    float learnXp = 40.f;               // опыта до «разучен»
    std::vector<float> starXp{100.f, 250.f, 500.f};  // кумулятивный опыт для 1, 2, 3 звёзд мастерства
    float trainingXpByRating[5] = {4.f, 8.f, 14.f, 22.f, 30.f};  // D, C, B, A, S за мини-игру
    float fightXpUse = 1.f, fightXpHit = 2.5f;  // применил / попал приёмом в бою
    float observationBonus = 0.5f;      // при высокой Наблюдательности подсмотр даёт множитель к опыту разучивания, 0..1
    float dailyTrainingCap = 120.f;     // сколько опыта за день можно получить тренировкой (против «фарма»)
};

struct MoveProgress {
    int observations = 0;
    float xp = 0.f;
    bool remembered = false;
};

class MoveMastery {
public:
    explicit MoveMastery(LearningConfig c = {}) : cfg_(std::move(c)) {}

    // Игрок видел приём у врага. observationSkill 0..10 (Наблюдательность). Возвращает true, если приём только что запомнен.
    bool observe(const std::string& moveId, float observationSkill = 0.f);
    // Игрок сразу знает приём (стартовый набор стиля, учитель, кассета).
    void grant(const std::string& moveId);
    // Тренировка: оценка мини-игры 0..4 (D..S) → опыт с дневным лимитом. Возвращает выданный опыт.
    float train(const std::string& moveId, int ratingIndex);
    // Бой: приём применён или попал.
    void fightUse(const std::string& moveId, bool hit);
    void newDay() { trainedToday_ = 0.f; }

    MoveStage stage(const std::string& moveId) const;
    int stars(const std::string& moveId) const;  // 0..3
    float xp(const std::string& moveId) const;
    bool canUse(const std::string& moveId) const { return stage(moveId) >= MoveStage::Learned; }
    // Множитель качества приёма от звёзд (меньше замах, лучше урон — применяет боевое ядро): 1.0, 1.04, 1.08, 1.12.
    float starBonus(const std::string& moveId) const { return 1.f + 0.04f * static_cast<float>(stars(moveId)); }

    core::Json toJson() const;
    bool fromJson(const core::Json& json, std::string& error);
    const std::map<std::string, MoveProgress>& all() const { return progress_; }
    const LearningConfig& config() const { return cfg_; }

private:
    LearningConfig cfg_;
    std::map<std::string, MoveProgress> progress_;
    float trainedToday_ = 0.f;
};

const char* stageName(MoveStage s);
bool parseLearningConfig(const core::Json& json, LearningConfig& out, std::string& error);
}  // namespace han::learning
// Пример: MoveMastery m; m.observe("boxing_cross", 6.f); m.train("boxing_jab", 3); if (m.canUse("boxing_jab")) ...
