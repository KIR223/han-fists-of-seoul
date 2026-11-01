// han/combat/scenario.h — сценарии боя для консольного симулятора и тестов (G1): JSON «бойцы + действия по кадрам» → лог и итог.
//   {"frames": 90, "seed": 1, "official": false,
//    "fighters": [{"x": 0, "z": 0, "stats": {"strength": 5}}, {"x": 0, "z": -1.2}],
//    "inputs": [{"frame": 1, "fighter": 0, "attack": "boxing_jab"},
//               {"frame": 4, "fighter": 1, "defense": "slip_left"},
//               {"frame": 1, "fighter": 1, "block": 30}]}
// Защиты: parry, slip_left, slip_right, duck, bob_left, bob_right, lean_back, sidestep_left, sidestep_right, step_back.
// «block»: сколько кадров держать блок с указанного кадра. «forward», «right», «dash» действуют один кадр (держите несколькими записями).
#pragma once
#include <string>
#include <vector>

#include "han/combat/sim.h"

namespace han::combat {
struct ScenarioResult {
    std::vector<std::string> log;     // строки по-русски: кадр и что произошло
    std::vector<float> health, stamina, balance;
    int frames = 0;
    int hits = 0, blocks = 0, evades = 0, knockdowns = 0, knockouts = 0;
    int winner = -1;                  // индекс бойца, если бой закончился нокаутом
};

bool defenseFromName(const std::string& name, DefenseAction& out);

// Прогоняет сценарий. false — ошибка в сценарии (неизвестный приём, боец, защита) с описанием в error.
bool runScenario(const core::Json& scenario, const CombatRules& rules, const MoveLibrary& moves, ScenarioResult& out, std::string& error);
}  // namespace han::combat
