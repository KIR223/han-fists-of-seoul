// han/combat/profile.h — боевые профили NPC (G3): умения, характер, ошибки техники. Формат — contracts/schemas/fighter_profile.schema.json,
// данные — data/combat/profiles/*.json. Профиль не знает о мире: он только описывает бойца, решения принимает FightAI (ai.h).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "han/combat/sim.h"
#include "han/core/json.h"

namespace han::combat {
enum class Sobriety : unsigned char { Sober, Tipsy, Drunk };
enum class Personality : unsigned char { Aggressor, Counter, Defensive, Panic, Balanced };

struct FighterProfile {
    std::string id, archetype;
    float experience = 5, striking = 5, grappling = 5, defense = 5, cardio = 5, courage = 5, aggression = 5;  // 0..10
    Sobriety sobriety = Sobriety::Sober;
    Personality personality = Personality::Balanced;
    std::map<std::string, float> actionWeights;  // тип приёма ("jab", "hook"…) → вес выбора
    float techniqueQuality = 0.7f;               // 0..1: чистота техники, влияет на ошибки
    std::vector<std::string> mistakes;           // "drops_hands", "telegraphs", "leans_forward"

    bool hasMistake(const std::string& m) const;
    // Статы для боевого ядра: сила и крепость из умений, выносливость из кардио, техника из качества.
    FighterStats stats() const;
    // Опьянение замедляет реакцию и портит технику.
    float impairment() const { return sobriety == Sobriety::Sober ? 0.f : (sobriety == Sobriety::Tipsy ? 0.15f : 0.4f); }
};

const char* personalityName(Personality p);
bool parseProfile(const core::Json& json, FighterProfile& out, std::string& error);

class ProfileLibrary {
public:
    bool add(FighterProfile p, std::string& error);
    bool loadDirectory(const std::string& dir, std::string& error);
    const FighterProfile* byId(const std::string& id) const;
    const std::vector<FighterProfile>& all() const { return profiles_; }
    std::size_t size() const { return profiles_.size(); }

private:
    std::vector<FighterProfile> profiles_;
};
}  // namespace han::combat
// Пример: ProfileLibrary lib; lib.loadDirectory("data/combat/profiles", err); FightAI ai(*lib.byId("boxer_veteran"), moves, rules, seed);
