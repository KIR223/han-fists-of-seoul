// han/combat/combos.h — фирменные комбо (часть 19): последовательность приёмов подряд, собранная из событий боя.
// Данные — data/combat/combos/*.json: {"combos":[{"id","name_key","moves":["boxing_jab",...],"max_gap_frames":50,"reward":{"flow_hits":1,"stream_points":20}}]}.
// Трекер смотрит на разрешённые удары (попадание, блок) бойца: между ударами не больше max_gap_frames и серия не прерывается чужим попаданием.
#pragma once
#include <string>
#include <vector>

#include "han/combat/sim.h"
#include "han/core/json.h"

namespace han::combat {
struct ComboDef {
    std::string id, nameKey;
    std::vector<std::string> moves;
    int maxGapFrames = 50;
    int streamPoints = 0;  // очки зрелищности для стрима (G10)
    int flowHits = 0;      // сколько попаданий засчитать в ритм сверх обычных
};

struct ComboEvent {
    int fighter = -1;
    int frame = 0;
    const ComboDef* combo = nullptr;
};

class ComboLibrary {
public:
    bool parse(const core::Json& json, std::string& error);
    bool loadDirectory(const std::string& dir, std::string& error);
    const ComboDef* byId(const std::string& id) const;
    const std::vector<ComboDef>& all() const { return combos_; }

private:
    std::vector<ComboDef> combos_;
};

class ComboTracker {
public:
    explicit ComboTracker(const ComboLibrary& lib) : lib_(lib) {}
    // Передавать события каждого кадра (CombatSim::events()). Возвращает комбо, завершённые в этом кадре.
    std::vector<ComboEvent> update(const std::vector<CombatEvent>& events);
    // Сколько приёмов подряд сейчас в серии бойца (для интерфейса).
    int streak(int fighter) const;

private:
    struct History {
        std::vector<std::string> moves;
        int lastFrame = -1000;
    };
    const ComboLibrary& lib_;
    std::vector<History> history_;
    History& of(int fighter);
};
}  // namespace han::combat
// Пример: ComboTracker t(lib); for (auto& c : t.update(sim.events())) award(c.fighter, c.combo->streamPoints);
