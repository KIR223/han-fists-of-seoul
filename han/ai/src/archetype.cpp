#include "han/ai/archetype.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace han::ai {
namespace {
using core::Json;

constexpr const char* kActivities[] = {"work", "commute", "cafe", "shop", "home", "wait", "walk", "eat", "exercise", "sleep"};
constexpr const char* kReactions[] = {"ignore", "flee", "watch", "film", "call_police", "intervene"};
constexpr const char* kEvents[] = {"fight", "gunshot", "siren", "explosion", "crash"};

bool inDay(float hour, float from, float to) {
    if (from <= to) return hour >= from && hour < to;
    return hour >= from || hour < to;  // через полночь
}

bool idString(const std::string& s, std::size_t maxLen) {
    if (s.size() < 2 || s.size() > maxLen || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
}  // namespace

const char* eventName(WorldEventType t) { return kEvents[static_cast<int>(t)]; }
const char* activityName(Activity a) { return kActivities[static_cast<int>(a)]; }
const char* reactionName(Reaction r) { return kReactions[static_cast<int>(r)]; }

bool activityFromName(const std::string& name, Activity& out) {
    for (int i = 0; i < 10; ++i)
        if (name == kActivities[i]) {
            out = static_cast<Activity>(i);
            return true;
        }
    return false;
}

bool reactionFromName(const std::string& name, Reaction& out) {
    for (int i = 0; i < 6; ++i)
        if (name == kReactions[i]) {
            out = static_cast<Reaction>(i);
            return true;
        }
    return false;
}

const char* defaultPlaceCategory(Activity a) {
    switch (a) {
        case Activity::Work: return "work";
        case Activity::Cafe: return "cafe";
        case Activity::Shop: return "shop";
        case Activity::Home:
        case Activity::Sleep: return "home";
        case Activity::Eat: return "eat";
        case Activity::Exercise: return "park";
        default: return "";
    }
}

const ScheduleEntry* Archetype::entryAt(float hour) const {
    hour = std::fmod(std::fmod(hour, 24.f) + 24.f, 24.f);
    for (const ScheduleEntry& e : schedule)
        if (inDay(hour, e.from, e.to)) return &e;
    return nullptr;
}

Activity Archetype::activityAt(float hour) const {
    const ScheduleEntry* e = entryAt(hour);
    return e ? e->activity : Activity::Walk;
}

Reaction Archetype::reactionTo(WorldEventType type) const {
    const auto it = reactions.find(eventName(type));
    if (it != reactions.end()) return it->second;
    // Нет записи: по храбрости. Смелый — смотрит и вмешивается в драке, робкий — убегает.
    if (courage < 0.25f) return Reaction::Flee;
    if (type == WorldEventType::Fight && courage > 0.8f) return Reaction::Intervene;
    return courage < 0.5f ? Reaction::Watch : Reaction::Film;
}

bool parseArchetype(const Json& j, Archetype& out, std::string& error) {
    Archetype a;
    const auto fail = [&](const std::string& why) {
        error = (a.id.empty() ? std::string("архетип") : "архетип «" + a.id + "»") + ": " + why;
        return false;
    };
    if (!j.isObject()) return fail("ожидался объект");
    static const char* allowed[] = {"id", "numeric_id", "name_key", "age_range", "money_range", "walk_speed", "courage", "fights_back", "schedule", "reactions", "line_keys"};
    for (const auto& [key, v] : j.members())
        if (std::none_of(std::begin(allowed), std::end(allowed), [&](const char* k) { return key == k; })) return fail("лишнее поле «" + key + "»");
    a.id = j["id"].asString();
    if (!idString(a.id, 41)) return fail("id должен быть строчными латинскими буквами, цифрами и «_» (от 2 до 41 знака)");
    if (!j["numeric_id"].isNumber() || j["numeric_id"].asNumber() < 0 || j["numeric_id"].asNumber() != std::floor(j["numeric_id"].asNumber()))
        return fail("numeric_id — целое число ≥ 0");
    a.numericId = static_cast<std::uint32_t>(j["numeric_id"].asNumber());
    a.nameKey = j["name_key"].asString();
    if (a.nameKey.rfind("people.", 0) != 0) return fail("name_key должен начинаться с «people.»");
    const Json& age = j["age_range"];
    if (age.size() != 2 || !age.at(0).isNumber() || !age.at(1).isNumber()) return fail("age_range — два числа");
    a.ageMin = static_cast<int>(age.at(0).asNumber());
    a.ageMax = static_cast<int>(age.at(1).asNumber());
    if (a.ageMin < 18) return fail("возраст младше 18 недопустим (правило проекта: детей и подростков в игре нет)");
    if (a.ageMax < a.ageMin || a.ageMax > 100) return fail("age_range: максимум меньше минимума или больше 100");
    if (j.has("money_range")) {
        const Json& m = j["money_range"];
        if (m.size() != 2 || !m.at(0).isNumber() || !m.at(1).isNumber() || m.at(0).asNumber() < 0 || m.at(1).asNumber() < m.at(0).asNumber()) return fail("money_range — два числа, 0 ≤ мин ≤ макс");
        a.moneyMin = static_cast<int>(m.at(0).asNumber());
        a.moneyMax = static_cast<int>(m.at(1).asNumber());
    }
    if (j.has("walk_speed")) {
        const double v = j["walk_speed"].asNumber(-1);
        if (!(v > 0 && v <= 3)) return fail("walk_speed должна быть в (0; 3] м/с");
        a.walkSpeed = static_cast<float>(v);
    }
    for (const char* field : {"courage", "fights_back"}) {
        if (!j.has(field)) continue;
        const double v = j[field].asNumber(-1);
        if (!(v >= 0 && v <= 1)) return fail(std::string(field) + " должно быть от 0 до 1");
        (field[0] == 'c' ? a.courage : a.fightsBack) = static_cast<float>(v);
    }
    if (!j["schedule"].isArray() || j["schedule"].size() == 0) return fail("schedule — непустой массив");
    for (const Json& e : j["schedule"].items()) {
        ScheduleEntry s;
        if (!e.isObject()) return fail("запись расписания должна быть объектом");
        for (const auto& [key, v] : e.members())
            if (key != "from" && key != "to" && key != "activity" && key != "place_category") return fail("в расписании лишнее поле «" + key + "»");
        const double from = e["from"].asNumber(-1), to = e["to"].asNumber(-1);
        if (!(from >= 0 && from <= 24 && to >= 0 && to <= 24)) return fail("from и to расписания — часы от 0 до 24");
        s.from = static_cast<float>(from);
        s.to = static_cast<float>(to);
        if (!activityFromName(e["activity"].asString(), s.activity)) return fail("неизвестное занятие «" + e["activity"].asString() + "»");
        s.placeCategory = e["place_category"].asString();
        a.schedule.push_back(s);
    }
    if (j.has("reactions")) {
        if (!j["reactions"].isObject()) return fail("reactions — объект");
        for (const auto& [event, value] : j["reactions"].members()) {
            if (std::none_of(std::begin(kEvents), std::end(kEvents), [&](const char* k) { return event == k; })) return fail("неизвестное событие «" + event + "» в reactions");
            Reaction r;
            if (!value.isString() || !reactionFromName(value.asString(), r)) return fail("неизвестная реакция для «" + event + "»");
            a.reactions[event] = r;
        }
    }
    for (const Json& k : j["line_keys"].items()) {
        if (!k.isString() || k.asString().rfind("people.line.", 0) != 0) return fail("line_keys: ключи начинаются с «people.line.»");
        a.lineKeys.push_back(k.asString());
    }
    out = std::move(a);
    return true;
}

bool ArchetypeLibrary::add(Archetype a, std::string& error) {
    for (const Archetype& b : list_) {
        if (b.id == a.id) {
            error = "повторяющийся id архетипа «" + a.id + "»";
            return false;
        }
        if (b.numericId == a.numericId) {
            error = "numeric_id " + std::to_string(a.numericId) + " у «" + a.id + "» уже занят «" + b.id + "»";
            return false;
        }
    }
    list_.push_back(std::move(a));
    return true;
}

bool ArchetypeLibrary::loadDirectory(const std::string& dir, std::string& error) {
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
    bool ok = true;
    std::string problems;
    for (const fs::path& file : files) {
        core::Json json;
        std::string err;
        Archetype a;
        if (!core::loadJsonFile(file.string(), json, err) || !parseArchetype(json, a, err) || !add(std::move(a), err)) {
            problems += (problems.empty() ? "" : "\n") + file.filename().string() + ": " + err;
            ok = false;
        }
    }
    if (!ok) error = problems;
    return ok;
}

std::vector<const Archetype*> ArchetypeLibrary::pointers() const {
    std::vector<const Archetype*> out;
    out.reserve(list_.size());
    for (const Archetype& a : list_) out.push_back(&a);
    return out;
}

const Archetype* ArchetypeLibrary::byId(const std::string& id) const {
    for (const Archetype& a : list_)
        if (a.id == id) return &a;
    return nullptr;
}

const Archetype* ArchetypeLibrary::byNumericId(std::uint32_t id) const {
    for (const Archetype& a : list_)
        if (a.numericId == id) return &a;
    return nullptr;
}
}  // namespace han::ai
