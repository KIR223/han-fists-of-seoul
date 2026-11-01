#include "han/minigames/drills.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace han::minigames {
namespace {
using core::Json;
const struct {
    const char* name;
    Action action;
} kActions[] = {{"jab", Action::Jab}, {"cross", Action::Cross}, {"left_hook", Action::LeftHook}, {"right_hook", Action::RightHook},
                {"uppercut", Action::Uppercut}, {"duck", Action::Duck}, {"evade", Action::Evade}};

bool readActions(const Json& arr, std::vector<Action>& out, std::string& error, const char* what) {
    for (std::size_t i = 0; i < arr.size(); ++i) {
        Action a;
        if (!actionFromName(arr.at(i).asString(), a)) {
            error = std::string(what) + ": неизвестное действие «" + arr.at(i).asString() + "»";
            return false;
        }
        out.push_back(a);
    }
    return true;
}
}  // namespace

bool actionFromName(const std::string& name, Action& out) {
    for (const auto& a : kActions)
        if (name == a.name) {
            out = a.action;
            return true;
        }
    return false;
}

const char* actionName(Action a) {
    for (const auto& x : kActions)
        if (x.action == a) return x.name;
    return "?";
}

const char* ratingName(Rating r) {
    switch (r) {
        case Rating::S: return "S";
        case Rating::A: return "A";
        case Rating::B: return "B";
        case Rating::C: return "C";
        case Rating::D: return "D";
    }
    return "D";
}

// ---------------------------------------------------------------- Лапы

Judgement PadDrill::press(Action action, int frame) {
    update(frame);
    int best = -1, bestDist = 1 << 30;
    for (std::size_t i = 0; i < cfg_.calls.size(); ++i) {
        if (judged_[i] != Judgement::None) continue;
        const int d = std::abs(frame - callFrame(i));
        if (d <= cfg_.okFrames && d < bestDist) {
            best = static_cast<int>(i);
            bestDist = d;
        }
    }
    if (best < 0) {  // нажатие мимо любой лапы
        ++wrong_;
        combo_ = 0;
        return Judgement::Wrong;
    }
    Judgement j;
    if (cfg_.calls[static_cast<std::size_t>(best)].action != action) {
        j = Judgement::Wrong;
        ++wrong_;
        combo_ = 0;
    } else {
        j = bestDist <= cfg_.perfectFrames ? Judgement::Perfect : (bestDist <= cfg_.goodFrames ? Judgement::Good : Judgement::Ok);
        bestCombo_ = std::max(bestCombo_, ++combo_);
    }
    judged_[static_cast<std::size_t>(best)] = j;
    return j;
}

void PadDrill::update(int frame) {
    for (std::size_t i = 0; i < cfg_.calls.size(); ++i)
        if (judged_[i] == Judgement::None && frame > callFrame(i) + cfg_.okFrames) {
            judged_[i] = Judgement::Miss;
            combo_ = 0;
        }
}

int PadDrill::count(Judgement j) const {
    int n = 0;
    for (Judgement x : judged_) n += x == j ? 1 : 0;
    return n;
}

float PadDrill::accuracy() const {
    if (judged_.empty()) return 0.f;
    float pts = 0.f;
    for (Judgement x : judged_) {
        if (x == Judgement::Perfect) pts += cfg_.perfectPoints;
        else if (x == Judgement::Good) pts += cfg_.goodPoints;
        else if (x == Judgement::Ok) pts += cfg_.okPoints;
    }
    return pts / static_cast<float>(judged_.size());
}

Rating PadDrill::rating() const {
    const float a = accuracy();
    if (a >= cfg_.ratingS) return Rating::S;
    if (a >= cfg_.ratingA) return Rating::A;
    if (a >= cfg_.ratingB) return Rating::B;
    if (a >= cfg_.ratingC) return Rating::C;
    return Rating::D;
}

bool parsePadConfig(const Json& json, PadConfig& out, std::string& error) {
    if (!json.isObject()) {
        error = "лапы: ожидался объект";
        return false;
    }
    PadConfig c;
    c.id = json["id"].asString();
    if (c.id.empty()) {
        error = "лапы: нет id";
        return false;
    }
    c.bpm = static_cast<int>(json["bpm"].asNumber(90));
    c.leadInBeats = static_cast<int>(json["lead_in_beats"].asNumber(2));
    const Json& w = json["windows"];
    c.perfectFrames = static_cast<int>(w["perfect"].asNumber(3));
    c.goodFrames = static_cast<int>(w["good"].asNumber(7));
    c.okFrames = static_cast<int>(w["ok"].asNumber(12));
    const Json& p = json["points"];
    c.perfectPoints = static_cast<float>(p["perfect"].asNumber(1.0));
    c.goodPoints = static_cast<float>(p["good"].asNumber(0.7));
    c.okPoints = static_cast<float>(p["ok"].asNumber(0.4));
    const Json& r = json["rating"];
    c.ratingS = static_cast<float>(r["S"].asNumber(0.92));
    c.ratingA = static_cast<float>(r["A"].asNumber(0.8));
    c.ratingB = static_cast<float>(r["B"].asNumber(0.65));
    c.ratingC = static_cast<float>(r["C"].asNumber(0.5));
    if (c.bpm < 40 || c.bpm > 200) {
        error = c.id + ": bpm должен быть 40..200";
        return false;
    }
    if (!(c.perfectFrames <= c.goodFrames && c.goodFrames <= c.okFrames && c.okFrames < c.framesPerBeat() / 2)) {
        error = c.id + ": окна должны идти по возрастанию и быть короче полудоли";
        return false;
    }
    const Json& calls = json["calls"];
    if (!calls.isArray() || calls.size() == 0) {
        error = c.id + ": нет calls";
        return false;
    }
    int lastBeat = -1;
    for (std::size_t i = 0; i < calls.size(); ++i) {
        PadCall pc;
        if (!actionFromName(calls.at(i)["action"].asString(), pc.action)) {
            error = c.id + ": неизвестное действие «" + calls.at(i)["action"].asString() + "»";
            return false;
        }
        pc.beat = static_cast<int>(calls.at(i)["beat"].asNumber(-1));
        if (pc.beat <= lastBeat) {
            error = c.id + ": доли должны строго возрастать";
            return false;
        }
        lastBeat = pc.beat;
        c.calls.push_back(pc);
    }
    out = std::move(c);
    return true;
}

// ---------------------------------------------------------------- Связка

ChainDrill::ChainDrill(ChainConfig config, std::uint32_t seed) : cfg_(std::move(config)), rng_(seed ? seed : 1u) { newSequence(cfg_.startLength); }

std::uint32_t ChainDrill::next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void ChainDrill::newSequence(int length) {
    sequence_.clear();
    for (int i = 0; i < length; ++i) sequence_.push_back(cfg_.alphabet[next() % cfg_.alphabet.size()]);
    state_ = State::Showing;
    stateFrame_ = 0;
    inputIndex_ = 0;
    sinceInput_ = 0;
}

void ChainDrill::fail() {
    ++mistakes_;
    if (mistakes_ > cfg_.maxMistakes) {
        state_ = State::Failed;
        return;
    }
    newSequence(length());  // та же длина, новая последовательность
}

int ChainDrill::shownIndex() const {
    if (state_ != State::Showing) return -1;
    const int i = stateFrame_ / cfg_.showFramesPerItem;
    return i < length() ? i : -1;
}

void ChainDrill::advance(int dt) {
    for (int k = 0; k < dt; ++k) {
        if (state_ == State::Won || state_ == State::Failed) return;
        ++elapsed_;
        ++stateFrame_;
        if (state_ == State::Showing) {
            if (stateFrame_ >= length() * cfg_.showFramesPerItem) {
                state_ = State::Input;
                stateFrame_ = 0;
                sinceInput_ = 0;
            }
        } else if (++sinceInput_ > cfg_.inputFramesPerItem) {
            fail();
        }
    }
}

bool ChainDrill::press(Action action) {
    if (state_ != State::Input) return false;  // во время показа нажатия не считаются
    if (action != sequence_[static_cast<std::size_t>(inputIndex_)]) {
        fail();
        return false;
    }
    sinceInput_ = 0;
    if (++inputIndex_ >= length()) {
        if (length() >= cfg_.maxLength) state_ = State::Won;
        else newSequence(length() + 1);
    }
    return true;
}

Rating ChainDrill::rating() const {
    if (state_ != State::Won) return Rating::D;
    if (mistakes_ <= cfg_.mistakesForS) return Rating::S;
    if (mistakes_ <= cfg_.mistakesForA) return Rating::A;
    if (mistakes_ <= cfg_.mistakesForB) return Rating::B;
    if (mistakes_ <= cfg_.mistakesForC) return Rating::C;
    return Rating::D;
}

bool parseChainConfig(const Json& json, ChainConfig& out, std::string& error) {
    if (!json.isObject()) {
        error = "связка: ожидался объект";
        return false;
    }
    ChainConfig c;
    c.id = json["id"].asString();
    if (c.id.empty()) {
        error = "связка: нет id";
        return false;
    }
    c.startLength = static_cast<int>(json["start_length"].asNumber(4));
    c.maxLength = static_cast<int>(json["max_length"].asNumber(12));
    c.showFramesPerItem = static_cast<int>(json["show_frames"].asNumber(30));
    c.inputFramesPerItem = static_cast<int>(json["input_frames"].asNumber(90));
    c.maxMistakes = static_cast<int>(json["max_mistakes"].asNumber(6));
    const Json& r = json["rating_mistakes"];
    c.mistakesForS = static_cast<int>(r["S"].asNumber(0));
    c.mistakesForA = static_cast<int>(r["A"].asNumber(1));
    c.mistakesForB = static_cast<int>(r["B"].asNumber(2));
    c.mistakesForC = static_cast<int>(r["C"].asNumber(4));
    if (!readActions(json["alphabet"], c.alphabet, error, c.id.c_str())) return false;
    if (c.alphabet.size() < 2) {
        error = c.id + ": в alphabet нужно минимум 2 действия";
        return false;
    }
    if (c.startLength < 1 || c.maxLength < c.startLength || c.showFramesPerItem < 1 || c.inputFramesPerItem < 1) {
        error = c.id + ": неверные длины или тайминги";
        return false;
    }
    out = std::move(c);
    return true;
}

// ---------------------------------------------------------------- Тень

bool parseShadowConfig(const Json& json, ShadowConfig& out, std::string& error) {
    ShadowConfig c;
    if (!parsePadConfig(json, c.pads, error)) return false;
    c.leadFrames = static_cast<int>(json["lead_frames"].asNumber(6));
    if (c.leadFrames < 0 || c.leadFrames > c.pads.okFrames) {
        error = c.pads.id + ": lead_frames должен быть от 0 до окна ok";
        return false;
    }
    out = std::move(c);
    return true;
}

// ---------------------------------------------------------------- Тяжёлый мешок

float HeavyBag::meter(int framesHeld) const {
    const int period = std::max(2, cfg_.chargeFrames);
    const float t = static_cast<float>(framesHeld % (2 * period)) / static_cast<float>(period);
    return t <= 1.f ? t : 2.f - t;
}

BagStrike HeavyBag::release(int framesHeld, float aimError) {
    BagStrike s;
    s.power = meter(framesHeld);
    s.green = s.power >= cfg_.greenLow && s.power <= cfg_.greenHigh;
    s.missed = aimError > cfg_.aimTolerance;
    const float mid = 0.5f * (cfg_.greenLow + cfg_.greenHigh);
    const float half = 0.5f * (cfg_.greenHigh - cfg_.greenLow);
    const float off = std::fabs(s.power - mid);
    // В зелёной зоне сила идеальна; за её краем качество падает линейно до нуля на расстоянии в две ширины зоны.
    const float powerQ = off <= half ? 1.f : std::max(0.f, 1.f - (off - half) / std::max(0.05f, 2.f * half));
    const float aimQ = s.missed ? 0.f : 1.f - 0.5f * std::min(1.f, aimError / std::max(0.01f, cfg_.aimTolerance));
    s.quality = powerQ * aimQ;
    if (!finished()) {
        ++done_;
        total_ += s.quality;
        greens_ += s.green && !s.missed ? 1 : 0;
    }
    return s;
}

Rating HeavyBag::rating() const {
    const float a = average();
    if (a >= cfg_.ratingS) return Rating::S;
    if (a >= cfg_.ratingA) return Rating::A;
    if (a >= cfg_.ratingB) return Rating::B;
    if (a >= cfg_.ratingC) return Rating::C;
    return Rating::D;
}

bool parseBagConfig(const Json& json, BagConfig& out, std::string& error) {
    if (!json.isObject()) {
        error = "мешок: ожидался объект";
        return false;
    }
    BagConfig c;
    c.id = json["id"].asString();
    if (c.id.empty()) {
        error = "мешок: нет id";
        return false;
    }
    c.chargeFrames = static_cast<int>(json["charge_frames"].asNumber(60));
    c.greenLow = static_cast<float>(json["green"].at(0).asNumber(0.7));
    c.greenHigh = static_cast<float>(json["green"].at(1).asNumber(0.85));
    c.aimTolerance = static_cast<float>(json["aim_tolerance"].asNumber(0.25));
    c.strikes = static_cast<int>(json["strikes"].asNumber(8));
    const Json& r = json["rating"];
    c.ratingS = static_cast<float>(r["S"].asNumber(0.85));
    c.ratingA = static_cast<float>(r["A"].asNumber(0.7));
    c.ratingB = static_cast<float>(r["B"].asNumber(0.5));
    c.ratingC = static_cast<float>(r["C"].asNumber(0.3));
    if (c.chargeFrames < 10 || c.strikes < 1 || !(0.f <= c.greenLow && c.greenLow < c.greenHigh && c.greenHigh <= 1.f) || c.aimTolerance <= 0.f) {
        error = c.id + ": неверные параметры шкалы";
        return false;
    }
    out = std::move(c);
    return true;
}
}  // namespace han::minigames
