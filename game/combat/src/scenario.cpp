#include "han/combat/scenario.h"

#include <cstdio>
#include <map>

namespace han::combat {
namespace {
using core::Json;

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}

const char* zoneName(contracts::HitZone z) { return z == contracts::HitZone::Head ? "голова" : (z == contracts::HitZone::Torso ? "корпус" : "ноги"); }

std::string describe(const CombatEvent& e, const std::vector<std::string>& names) {
    const std::string who = names[static_cast<std::size_t>(e.attacker < 0 ? 0 : e.attacker)] + "→" + names[static_cast<std::size_t>(e.defender < 0 ? 0 : e.defender)];
    const std::string move = e.move ? e.move->id : "";
    switch (e.type) {
        case CombatEventType::Hit:
            return who + " попадание " + move + " урон " + fmt("%.1f", e.damage) + " (" + zoneName(e.zone) + ")" + (e.counter ? ", КОНТРУДАР" : "") + (e.crit ? ", КРИТ" : "") +
                   (e.back ? ", со спины" : "") + (e.downed ? ", по лежащему" : "") + (e.defense.empty() ? "" : ", защита «" + e.defense + "» не спасла");
        case CombatEventType::Blocked: return who + " блок (" + e.defense + ") " + move + " урон " + fmt("%.1f", e.damage);
        case CombatEventType::Evaded: return who + " уклонение (" + e.defense + ") от " + move + (e.angle ? ", выход на угол" : "");
        case CombatEventType::PerfectParry: return who + " ИДЕАЛЬНОЕ ПАРИРОВАНИЕ " + move;
        case CombatEventType::Whiff: return who + " промах " + move;
        case CombatEventType::Knockdown: return names[static_cast<std::size_t>(e.defender)] + " НОКДАУН";
        case CombatEventType::GetUp: return names[static_cast<std::size_t>(e.defender)] + " встал";
        case CombatEventType::KnockedOut: return names[static_cast<std::size_t>(e.defender)] + " НОКАУТ";
    }
    return "";
}
}  // namespace

bool defenseFromName(const std::string& n, DefenseAction& out) {
    static const std::map<std::string, DefenseAction> names = {
        {"parry", DefenseAction::Parry}, {"slip_left", DefenseAction::SlipLeft}, {"slip_right", DefenseAction::SlipRight}, {"duck", DefenseAction::Duck},
        {"bob_left", DefenseAction::BobLeft}, {"bob_right", DefenseAction::BobRight}, {"lean_back", DefenseAction::LeanBack},
        {"sidestep_left", DefenseAction::SidestepLeft}, {"sidestep_right", DefenseAction::SidestepRight}, {"step_back", DefenseAction::StepBack},
        {"shoulder_roll_left", DefenseAction::ShoulderRollLeft}, {"shoulder_roll_right", DefenseAction::ShoulderRollRight}};
    const auto it = names.find(n);
    if (it == names.end()) return false;
    out = it->second;
    return true;
}

bool runScenario(const Json& sc, const CombatRules& rules, const MoveLibrary& moves, ScenarioResult& out, std::string& error, const ProfileLibrary* profiles) {
    const auto fail = [&](const std::string& why) {
        error = "сценарий: " + why;
        return false;
    };
    if (!sc.isObject()) return fail("ожидался объект");
    const int frames = static_cast<int>(sc["frames"].asNumber(120));
    if (frames < 1 || frames > 36000) return fail("frames — от 1 до 36000");
    if (sc["fighters"].size() < 2 || sc["fighters"].size() > 8) return fail("fighters — от 2 до 8 бойцов");
    CombatSim sim(rules, static_cast<std::uint32_t>(sc["seed"].asNumber(1)));
    sim.setOfficialRules(sc["official"].asBool(false));
    std::vector<std::string> names;
    std::vector<const FighterProfile*> aiProfiles;
    for (const Json& f : sc["fighters"].items()) {
        FighterStats st;
        const FighterProfile* profile = nullptr;
        if (f.has("ai")) {
            if (!profiles) return fail("«ai» указан, но библиотека профилей не передана");
            profile = profiles->byId(f["ai"].asString());
            if (!profile) return fail("неизвестный боевой профиль «" + f["ai"].asString() + "»");
            st = profile->stats();
        }
        aiProfiles.push_back(profile);
        const Json& s = f["stats"];
        st.strength = static_cast<float>(s["strength"].asNumber(st.strength));
        st.endurance = static_cast<float>(s["endurance"].asNumber(st.endurance));
        st.technique = static_cast<float>(s["technique"].asNumber(st.technique));
        st.toughness = static_cast<float>(s["toughness"].asNumber(st.toughness));
        st.stars = static_cast<int>(s["stars"].asNumber(st.stars));
        sim.addFighter({static_cast<float>(f["x"].asNumber(0)), static_cast<float>(f["z"].asNumber(0))}, st);
        names.push_back(f["name"].isString() ? f["name"].asString() : std::string(1, static_cast<char>('A' + static_cast<int>(names.size()))));
    }
    const int n = sim.fighterCount();
    for (int i = 0; i < n; ++i) sim.setTarget(i, i == 0 ? 1 : 0);  // по умолчанию дуэль: все бьют первого, первый — второго
    for (std::size_t i = 0; i < sc["fighters"].size(); ++i) {
        const Json& t = sc["fighters"].at(i)["target"];
        if (t.isNumber()) sim.setTarget(static_cast<int>(i), static_cast<int>(t.asNumber()));
    }

    // ИИ бойцов: общий набор жетонов атаки (в групповой драке бьют не больше двух сразу).
    AttackTokens tokens(2, 70);
    std::vector<FightAI> ais;
    std::vector<int> aiIndex(static_cast<std::size_t>(n), -1);
    std::vector<const MoveData*> styleMoves;
    for (const MoveData& m : moves.all()) styleMoves.push_back(&m);
    for (int i = 0; i < n; ++i)
        if (aiProfiles[static_cast<std::size_t>(i)]) {
            aiIndex[static_cast<std::size_t>(i)] = static_cast<int>(ais.size());
            ais.emplace_back(*aiProfiles[static_cast<std::size_t>(i)], styleMoves, rules, static_cast<std::uint32_t>(sc["fighters"].at(static_cast<std::size_t>(i))["seed_ai"].asNumber(100 + i)), &tokens);
        }

    struct Scheduled {
        int fighter;
        FighterInput input;
        int blockFrames = 0;
    };
    std::map<int, std::vector<Scheduled>> byFrame;
    for (const Json& in : sc["inputs"].items()) {
        const int frame = static_cast<int>(in["frame"].asNumber(-1)), fighter = static_cast<int>(in["fighter"].asNumber(-1));
        if (frame < 1 || frame > frames) return fail("frame записи ввода вне 1.." + std::to_string(frames));
        if (fighter < 0 || fighter >= n) return fail("fighter записи ввода — номер бойца 0.." + std::to_string(n - 1));
        Scheduled s{fighter, {}, 0};
        if (in.has("attack")) {
            s.input.attack = moves.byId(in["attack"].asString());
            if (!s.input.attack) return fail("неизвестный приём «" + in["attack"].asString() + "»");
        }
        if (in.has("defense") && !defenseFromName(in["defense"].asString(), s.input.defense)) return fail("неизвестная защита «" + in["defense"].asString() + "»");
        s.input.forward = static_cast<float>(in["forward"].asNumber(0));
        s.input.right = static_cast<float>(in["right"].asNumber(0));
        s.input.dash = in["dash"].asBool(false);
        s.blockFrames = static_cast<int>(in["block"].asNumber(0));
        byFrame[frame].push_back(s);
    }

    std::vector<int> blockUntil(static_cast<std::size_t>(n), 0);
    for (int frame = 1; frame <= frames; ++frame) {
        std::vector<FighterInput> inputs(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            if (aiIndex[static_cast<std::size_t>(i)] >= 0) inputs[static_cast<std::size_t>(i)] = ais[static_cast<std::size_t>(aiIndex[static_cast<std::size_t>(i)])].decide(sim, i);
        if (const auto it = byFrame.find(frame); it != byFrame.end())
            for (const Scheduled& s : it->second) {
                FighterInput& slot = inputs[static_cast<std::size_t>(s.fighter)];
                if (s.input.attack) slot.attack = s.input.attack;
                if (s.input.defense != DefenseAction::None) slot.defense = s.input.defense;
                slot.forward += s.input.forward;
                slot.right += s.input.right;
                slot.dash = slot.dash || s.input.dash;
                if (s.blockFrames > 0) blockUntil[static_cast<std::size_t>(s.fighter)] = frame + s.blockFrames;
            }
        for (int i = 0; i < n; ++i)
            if (aiIndex[static_cast<std::size_t>(i)] < 0 || frame < blockUntil[static_cast<std::size_t>(i)]) inputs[static_cast<std::size_t>(i)].blockHeld = frame < blockUntil[static_cast<std::size_t>(i)];
        sim.step(inputs);
        for (const CombatEvent& e : sim.events()) {
            out.log.push_back("кадр " + std::to_string(frame) + ": " + describe(e, names));
            switch (e.type) {
                case CombatEventType::Hit: ++out.hits; break;
                case CombatEventType::Blocked: ++out.blocks; break;
                case CombatEventType::Evaded:
                case CombatEventType::PerfectParry: ++out.evades; break;
                case CombatEventType::Knockdown: ++out.knockdowns; break;
                case CombatEventType::KnockedOut: ++out.knockouts; break;
                default: break;
            }
        }
        out.frames = frame;
        if (sim.fightOver()) break;
    }
    for (int i = 0; i < n; ++i) {
        out.health.push_back(sim.fighter(i).health);
        out.stamina.push_back(sim.fighter(i).stamina);
        out.balance.push_back(sim.fighter(i).balance);
        if (sim.fightOver() && sim.fighter(i).alive()) out.winner = i;
    }
    return true;
}
}  // namespace han::combat
