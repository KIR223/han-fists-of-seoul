#include "han/combat/move_data.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace han::combat {
namespace {
using core::Json;

constexpr const char* kTraj[] = {"straight", "arc_left", "arc_right", "up", "down", "spin", "low", "push"};
constexpr const char* kHeight[] = {"head", "torso", "legs"};
constexpr const char* kMove[] = {"static", "step_in", "retreat", "sidestep", "pivot", "circle", "run"};

template <std::size_t N, typename E>
bool fromName(const char* const (&names)[N], const std::string& name, E& out) {
    for (std::size_t i = 0; i < N; ++i)
        if (name == names[i]) {
            out = static_cast<E>(i);
            return true;
        }
    return false;
}

bool idString(const std::string& s, std::size_t maxLen) {
    if (s.size() < 2 || s.size() > maxLen || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
}  // namespace

const char* trajectoryName(Trajectory t) { return kTraj[static_cast<int>(t)]; }
const char* heightName(Height h) { return kHeight[static_cast<int>(h)]; }
const char* movementName(Movement m) { return kMove[static_cast<int>(m)]; }
bool trajectoryFromName(const std::string& n, Trajectory& o) { return fromName(kTraj, n, o); }
bool heightFromName(const std::string& n, Height& o) { return fromName(kHeight, n, o); }
bool movementFromName(const std::string& n, Movement& o) { return fromName(kMove, n, o); }

bool parseMove(const Json& j, MoveData& out, std::string& error) {
    MoveData m;
    const auto fail = [&](const std::string& why) {
        error = (m.id.empty() ? std::string("приём") : "приём «" + m.id + "»") + ": " + why;
        return false;
    };
    if (!j.isObject()) return fail("ожидался объект");
    static const char* known[] = {"id", "style", "name_key", "unlock_level", "type", "trajectory", "limb", "height", "movement_variant", "frames", "cancel_window",
                                  "range", "damage", "stamina", "balance", "focus", "on_block_frames", "on_hit_frames", "armor", "unblockable", "power", "impulse",
                                  "hitbox", "fx", "anim", "simple_rhythm_slots", "input_master", "variants", "optimal_range"};
    for (const auto& [key, v] : j.members())
        if (std::none_of(std::begin(known), std::end(known), [&](const char* k) { return key == k; })) return fail("лишнее поле «" + key + "»");

    m.id = j["id"].asString();
    if (!idString(m.id, 49)) return fail("id — строчные латинские буквы, цифры и «_», 2–49 знаков");
    m.style = j["style"].asString();
    if (!idString(m.style, 25)) return fail("style — строчные латинские буквы, цифры и «_»");
    m.nameKey = j["name_key"].asString();
    if (m.nameKey.rfind("move.", 0) != 0) return fail("name_key должен начинаться с «move.»");
    const double level = j["unlock_level"].asNumber(-1);
    if (!(level >= 1 && level <= 10 && level == std::floor(level))) return fail("unlock_level — целое 1..10");
    m.unlockLevel = static_cast<int>(level);
    m.type = j["type"].asString();
    if (m.type.empty()) return fail("не указан type");
    if (!trajectoryFromName(j["trajectory"].asString(), m.trajectory)) return fail("неизвестная траектория «" + j["trajectory"].asString() + "»");
    m.limb = j["limb"].asString();
    if (m.limb.empty()) return fail("не указана конечность limb");
    if (!heightFromName(j["height"].asString(), m.height)) return fail("неизвестная высота «" + j["height"].asString() + "»");
    if (j.has("movement_variant") && !movementFromName(j["movement_variant"].asString(), m.movement)) return fail("неизвестный movement_variant");

    const Json& f = j["frames"];
    const double s = f["startup"].asNumber(-1), a = f["active"].asNumber(-1), r = f["recovery"].asNumber(-1);
    if (!(s >= 1 && s <= 60 && a >= 1 && a <= 30 && r >= 1 && r <= 90)) return fail("frames: startup 1..60, active 1..30, recovery 1..90");
    m.startup = static_cast<int>(s);
    m.active = static_cast<int>(a);
    m.recovery = static_cast<int>(r);
    const Json& c = j["cancel_window"];
    if (c.size() != 2 || !c.at(0).isNumber() || !c.at(1).isNumber()) return fail("cancel_window — два числа");
    m.cancelFrom = static_cast<int>(c.at(0).asNumber());
    m.cancelTo = static_cast<int>(c.at(1).asNumber());
    if (m.cancelFrom < 0 || m.cancelTo < m.cancelFrom) return fail("cancel_window: от не больше до");
    if (m.cancelFrom < m.startup || m.cancelTo > m.totalFrames()) return fail("cancel_window должно лежать внутри приёма: после начала активной фазы и не позже конца");

    m.range = static_cast<float>(j["range"].asNumber(-1));
    if (!(m.range > 0.f && m.range <= 4.f)) return fail("range — число 0..4 м");
    for (const char* field : {"damage", "stamina", "balance", "focus"}) {
        if (!j.has(field) && std::string(field) == "focus") continue;
        const double v = j[field].asNumber(-1);
        if (!(v >= 0 && v <= 200)) return fail(std::string(field) + " — число 0..200");
        (field[0] == 'd' ? m.damage : field[0] == 's' ? m.stamina : field[0] == 'b' ? m.balance : m.focus) = static_cast<float>(v);
    }
    m.onBlockFrames = static_cast<int>(j["on_block_frames"].asNumber(0));
    m.onHitFrames = static_cast<int>(j["on_hit_frames"].asNumber(0));
    m.armor = j["armor"].asBool(false);
    m.unblockable = j["unblockable"].asBool(false);
    m.power = j["power"].asBool(false);
    m.impulse = static_cast<float>(j["impulse"].asNumber(0));
    if (m.impulse < 0 || m.impulse > 2000) return fail("impulse — число 0..2000");
    m.hitstopMs = static_cast<int>(j["fx"]["hitstop_ms"].asNumber(0));
    m.shake = static_cast<float>(j["fx"]["shake"].asNumber(0));
    m.sound = j["fx"]["sound"].asString();
    m.anim = j["anim"].asString();
    out = std::move(m);
    return true;
}

bool MoveLibrary::add(MoveData move, std::string& error) {
    if (byId(move.id)) {
        error = "повторяющийся id приёма «" + move.id + "»";
        return false;
    }
    moves_.push_back(std::move(move));
    return true;
}

bool MoveLibrary::loadDirectory(const std::string& dir, std::string& error) {
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
        MoveData move;
        if (!core::loadJsonFile(file.string(), json, err) || !parseMove(json, move, err) || !add(std::move(move), err))
            problems += (problems.empty() ? "" : "\n") + file.filename().string() + ": " + err;
    }
    if (!problems.empty()) {
        error = problems;
        return false;
    }
    return true;
}

const MoveData* MoveLibrary::byId(const std::string& id) const {
    for (const MoveData& m : moves_)
        if (m.id == id) return &m;
    return nullptr;
}
}  // namespace han::combat
