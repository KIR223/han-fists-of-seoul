// Консольный симулятор боя (G1): combat_sim сценарий.json → лог кадров и итог.
//   combat_sim data/combat/scenarios/jab_vs_slip.json [--rules файл] [--moves папка] [--quiet]
// Запускать из корня репозитория. Код выхода: 0 — сценарий прошёл, 1 — ошибка данных или сценария.
#include <cstdio>
#include <cstring>
#include <string>

#include "han/combat/scenario.h"

int main(int argc, char** argv) {
    std::string scenarioPath, rulesPath = "data/combat/combat_rules.json", movesDir = "data/combat/moves";
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rules") && i + 1 < argc) rulesPath = argv[++i];
        else if (!std::strcmp(argv[i], "--moves") && i + 1 < argc) movesDir = argv[++i];
        else if (!std::strcmp(argv[i], "--quiet")) quiet = true;
        else scenarioPath = argv[i];
    }
    if (scenarioPath.empty()) {
        std::fprintf(stderr, "использование: combat_sim сценарий.json [--rules файл] [--moves папка] [--quiet]\n");
        return 1;
    }
    std::string error;
    han::combat::CombatRules rules;
    han::combat::MoveLibrary moves;
    han::core::Json scenario;
    if (!rules.load(rulesPath, error) || !moves.loadDirectory(movesDir, error) || !han::core::loadJsonFile(scenarioPath, scenario, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    han::combat::ScenarioResult result;
    if (!han::combat::runScenario(scenario, rules, moves, result, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (!quiet)
        for (const std::string& line : result.log) std::printf("%s\n", line.c_str());
    std::printf("--- итог за %d кадров (%.1f с): попаданий %d, блоков %d, уклонений %d, нокдаунов %d, нокаутов %d\n", result.frames, result.frames / 60.0, result.hits, result.blocks,
                result.evades, result.knockdowns, result.knockouts);
    for (std::size_t i = 0; i < result.health.size(); ++i)
        std::printf("боец %zu: здоровье %.1f, стамина %.1f, баланс %.1f%s\n", i, result.health[i], result.stamina[i], result.balance[i], static_cast<int>(i) == result.winner ? "  ← победил" : "");
    return 0;
}
