#include "han/combat/combos.h"

#include <algorithm>
#include <filesystem>

namespace han::combat {
using core::Json;

bool ComboLibrary::parse(const Json& json, std::string& error) {
    if (!json["combos"].isArray()) {
        error = "комбо: нужен массив combos";
        return false;
    }
    std::vector<ComboDef> parsed;
    for (const Json& c : json["combos"].items()) {
        ComboDef d;
        d.id = c["id"].asString();
        d.nameKey = c["name_key"].asString();
        if (d.id.empty() || d.nameKey.empty()) {
            error = "комбо: у каждого нужны id и name_key";
            return false;
        }
        for (const ComboDef& other : combos_)
            if (other.id == d.id) {
                error = "комбо: повторяющийся id " + d.id;
                return false;
            }
        for (const ComboDef& other : parsed)
            if (other.id == d.id) {
                error = "комбо: повторяющийся id " + d.id;
                return false;
            }
        for (const Json& m : c["moves"].items()) d.moves.push_back(m.asString());
        if (d.moves.size() < 2) {
            error = d.id + ": в комбо минимум два приёма";
            return false;
        }
        d.maxGapFrames = static_cast<int>(c["max_gap_frames"].asNumber(50));
        d.streamPoints = static_cast<int>(c["reward"]["stream_points"].asNumber(0));
        d.flowHits = static_cast<int>(c["reward"]["flow_hits"].asNumber(0));
        if (d.maxGapFrames < 5 || d.maxGapFrames > 300 || d.streamPoints < 0 || d.flowHits < 0) {
            error = d.id + ": неверные max_gap_frames (5..300) или награда";
            return false;
        }
        parsed.push_back(std::move(d));
    }
    for (ComboDef& d : parsed) combos_.push_back(std::move(d));
    return true;
}

bool ComboLibrary::loadDirectory(const std::string& dir, std::string& error) {
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
        if (!core::loadJsonFile(file.string(), json, err) || !parse(json, err)) problems += (problems.empty() ? "" : "\n") + file.filename().string() + ": " + err;
    }
    error = problems;
    return problems.empty();
}

const ComboDef* ComboLibrary::byId(const std::string& id) const {
    for (const ComboDef& c : combos_)
        if (c.id == id) return &c;
    return nullptr;
}

ComboTracker::History& ComboTracker::of(int fighter) {
    if (static_cast<std::size_t>(fighter) >= history_.size()) history_.resize(static_cast<std::size_t>(fighter) + 1);
    return history_[static_cast<std::size_t>(fighter)];
}

int ComboTracker::streak(int fighter) const {
    return fighter >= 0 && static_cast<std::size_t>(fighter) < history_.size() ? static_cast<int>(history_[static_cast<std::size_t>(fighter)].moves.size()) : 0;
}

std::vector<ComboEvent> ComboTracker::update(const std::vector<CombatEvent>& events) {
    std::vector<ComboEvent> done;
    for (const CombatEvent& e : events) {
        if (!e.move || e.attacker < 0) continue;
        // Чужое попадание обрывает серию защитника.
        if (e.type == CombatEventType::Hit && e.defender >= 0) {
            History& victim = of(e.defender);
            victim.moves.clear();
        }
        const bool landed = e.type == CombatEventType::Hit || e.type == CombatEventType::Blocked;
        if (!landed) continue;
        History& h = of(e.attacker);
        int maxGap = 0;
        for (const ComboDef& c : lib_.all()) maxGap = std::max(maxGap, c.maxGapFrames);
        if (e.frame - h.lastFrame > maxGap) h.moves.clear();
        h.moves.push_back(e.move->id);
        h.lastFrame = e.frame;
        if (h.moves.size() > 16) h.moves.erase(h.moves.begin());
        for (const ComboDef& c : lib_.all()) {
            if (h.moves.size() < c.moves.size()) continue;
            if (!std::equal(c.moves.begin(), c.moves.end(), h.moves.end() - static_cast<std::ptrdiff_t>(c.moves.size()))) continue;
            done.push_back({e.attacker, e.frame, &c});
        }
    }
    return done;
}
}  // namespace han::combat
