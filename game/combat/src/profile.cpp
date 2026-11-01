#include "han/combat/profile.h"

#include <algorithm>
#include <filesystem>

namespace han::combat {
namespace {
using core::Json;

bool idString(const std::string& s, std::size_t maxLen) {
    if (s.size() < 2 || s.size() > maxLen || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
}  // namespace

bool FighterProfile::hasMistake(const std::string& m) const { return std::find(mistakes.begin(), mistakes.end(), m) != mistakes.end(); }

FighterStats FighterProfile::stats() const {
    FighterStats s;
    s.strength = std::clamp(0.5f * striking + 0.5f * grappling * 0.5f + 2.5f, 1.f, 10.f);  // сила: удар плюс чуть от борьбы
    s.endurance = std::clamp(cardio, 1.f, 10.f);
    s.technique = std::clamp(techniqueQuality * 10.f - impairment() * 5.f, 0.f, 10.f);
    s.toughness = std::clamp(0.4f * courage + 0.3f * experience + 2.f, 1.f, 10.f);
    s.stars = experience >= 8.f ? 3 : (experience >= 5.f ? 2 : 1);
    s.headMovement = archetype == "boxer";
    return s;
}

const char* personalityName(Personality p) {
    switch (p) {
        case Personality::Aggressor: return "aggressor";
        case Personality::Counter: return "counter";
        case Personality::Defensive: return "defensive";
        case Personality::Panic: return "panic";
        case Personality::Balanced: return "balanced";
    }
    return "balanced";
}

bool parseProfile(const Json& j, FighterProfile& out, std::string& error) {
    FighterProfile p;
    const auto fail = [&](const std::string& why) {
        error = (p.id.empty() ? std::string("профиль") : "профиль «" + p.id + "»") + ": " + why;
        return false;
    };
    if (!j.isObject()) return fail("ожидался объект");
    static const char* known[] = {"id", "archetype", "experience", "striking", "grappling", "defense", "cardio", "courage", "aggression", "sobriety", "personality",
                                  "action_weights", "technique_quality", "mistakes", "background", "random_skills", "clothing", "items"};
    for (const auto& [key, v] : j.members())
        if (std::none_of(std::begin(known), std::end(known), [&](const char* k) { return key == k; })) return fail("лишнее поле «" + key + "»");
    p.id = j["id"].asString();
    if (!idString(p.id, 41)) return fail("id — строчные латинские буквы, цифры и «_»");
    p.archetype = j["archetype"].asString();
    if (!idString(p.archetype, 41)) return fail("archetype — строчные латинские буквы, цифры и «_»");
    const auto skill = [&](const char* key, float& dst, bool required) {
        if (!j.has(key)) return !required;
        const double v = j[key].asNumber(-1);
        if (!(v >= 0 && v <= 10)) return false;
        dst = static_cast<float>(v);
        return true;
    };
    const struct {
        const char* key;
        float* dst;
        bool required;
    } skills[] = {{"experience", &p.experience, true}, {"striking", &p.striking, true}, {"grappling", &p.grappling, false}, {"defense", &p.defense, true},
                  {"cardio", &p.cardio, true}, {"courage", &p.courage, true}, {"aggression", &p.aggression, true}};
    for (const auto& s : skills)
        if (!skill(s.key, *s.dst, s.required)) return fail(std::string(s.key) + " — число 0..10" + (s.required ? " (обязательно)" : ""));
    const double q = j["technique_quality"].asNumber(-1);
    if (!(q >= 0 && q <= 1)) return fail("technique_quality — число 0..1");
    p.techniqueQuality = static_cast<float>(q);
    const std::string sob = j.has("sobriety") ? j["sobriety"].asString() : "sober";
    if (sob == "sober") p.sobriety = Sobriety::Sober;
    else if (sob == "tipsy") p.sobriety = Sobriety::Tipsy;
    else if (sob == "drunk") p.sobriety = Sobriety::Drunk;
    else return fail("sobriety — sober, tipsy или drunk");
    const std::string pers = j["personality"].asString();
    bool found = false;
    for (const Personality c : {Personality::Aggressor, Personality::Counter, Personality::Defensive, Personality::Panic, Personality::Balanced})
        if (pers == personalityName(c)) {
            p.personality = c;
            found = true;
        }
    if (!found) return fail("неизвестный характер «" + pers + "»");
    if (j.has("action_weights")) {
        static const char* types[] = {"jab", "straight", "hook", "uppercut", "body", "power", "counter", "special"};
        for (const auto& [key, w] : j["action_weights"].members()) {
            if (std::none_of(std::begin(types), std::end(types), [&](const char* t) { return key == t; })) return fail("неизвестный тип приёма «" + key + "» в action_weights");
            const double v = w.asNumber(-1);
            if (!(v >= 0 && v <= 1)) return fail("вес «" + key + "» — число 0..1");
            p.actionWeights[key] = static_cast<float>(v);
        }
    }
    for (const Json& m : j["mistakes"].items()) {
        const std::string name = m.asString();
        if (name != "drops_hands" && name != "telegraphs" && name != "leans_forward") return fail("неизвестная ошибка «" + name + "»");
        if (!p.hasMistake(name)) p.mistakes.push_back(name);
    }
    out = std::move(p);
    return true;
}

bool ProfileLibrary::add(FighterProfile p, std::string& error) {
    if (byId(p.id)) {
        error = "повторяющийся id профиля «" + p.id + "»";
        return false;
    }
    profiles_.push_back(std::move(p));
    return true;
}

bool ProfileLibrary::loadDirectory(const std::string& dir, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        error = "нет папки " + dir;
        return false;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    std::string problems;
    for (const fs::path& file : files) {
        Json json;
        std::string err;
        FighterProfile p;
        if (!core::loadJsonFile(file.string(), json, err) || !parseProfile(json, p, err) || !add(std::move(p), err)) problems += (problems.empty() ? "" : "\n") + file.filename().string() + ": " + err;
    }
    if (!problems.empty()) {
        error = problems;
        return false;
    }
    return true;
}

const FighterProfile* ProfileLibrary::byId(const std::string& id) const {
    for (const FighterProfile& p : profiles_)
        if (p.id == id) return &p;
    return nullptr;
}
}  // namespace han::combat
