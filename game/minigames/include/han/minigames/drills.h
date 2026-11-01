// han/minigames/drills.h — мини-игры зала (G2): «Лапы» (ритм под счёт тренера) и «Связка» (повтори последовательность по памяти).
// Чистая логика без графики и звука: время — номера кадров 60 Гц, всё детерминировано. Числа — из data/combat/minigames/*.json.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "han/core/json.h"

namespace han::minigames {
// Действия игрока в мини-играх (то, что можно показать иконкой): удары, нырок (C) и уход (Space).
enum class Action : unsigned char { Jab, Cross, LeftHook, RightHook, Uppercut, Duck, Evade };
bool actionFromName(const std::string& name, Action& out);
const char* actionName(Action a);

enum class Rating : unsigned char { D, C, B, A, S };
const char* ratingName(Rating r);

// ---------------------------------------------------------------- Лапы

struct PadCall {
    Action action = Action::Jab;
    int beat = 0;  // номер доли от начала; тренер подставляет лапу на этой доле
};

struct PadConfig {
    std::string id;
    int bpm = 90;
    int leadInBeats = 2;  // доли отсчёта до первой лапы
    int perfectFrames = 3, goodFrames = 7, okFrames = 12;
    float perfectPoints = 1.f, goodPoints = 0.7f, okPoints = 0.4f;
    float ratingS = 0.92f, ratingA = 0.8f, ratingB = 0.65f, ratingC = 0.5f;
    std::vector<PadCall> calls;

    int framesPerBeat() const { return 3600 / (bpm > 0 ? bpm : 60); }
};

enum class Judgement : unsigned char { None, Perfect, Good, Ok, Miss, Wrong };

class PadDrill {
public:
    explicit PadDrill(PadConfig config) : cfg_(std::move(config)), judged_(cfg_.calls.size(), Judgement::None) {}

    // Игрок сделал действие в кадре frame (от начала мини-игры). Возвращает оценку нажатия.
    Judgement press(Action action, int frame);
    // Продвигает время: не пойманные лапы уходят в промах. Вызывать каждый кадр (или перед press и в конце).
    void update(int frame);

    int callFrame(std::size_t i) const { return (cfg_.leadInBeats + cfg_.calls[i].beat) * cfg_.framesPerBeat(); }
    int endFrame() const { return cfg_.calls.empty() ? 0 : callFrame(cfg_.calls.size() - 1) + cfg_.okFrames + 1; }
    bool finished(int frame) const { return frame >= endFrame(); }

    float accuracy() const;  // 0..1: набранные очки к максимуму
    Rating rating() const;
    int combo() const { return combo_; }
    int bestCombo() const { return bestCombo_; }
    int count(Judgement j) const;
    int wrongPresses() const { return wrong_; }
    Judgement judgement(std::size_t call) const { return judged_[call]; }
    const PadConfig& config() const { return cfg_; }

private:
    PadConfig cfg_;
    std::vector<Judgement> judged_;
    int combo_ = 0, bestCombo_ = 0, wrong_ = 0;
};

bool parsePadConfig(const core::Json& json, PadConfig& out, std::string& error);

// ---------------------------------------------------------------- Связка

struct ChainConfig {
    std::string id;
    int startLength = 4, maxLength = 12;
    int showFramesPerItem = 30;   // сколько показывается каждая иконка
    int inputFramesPerItem = 90;  // лимит времени на каждый ввод
    int maxMistakes = 6;          // больше ошибок — мини-игра провалена
    std::vector<Action> alphabet;
    int mistakesForS = 0, mistakesForA = 1, mistakesForB = 2, mistakesForC = 4;
};

class ChainDrill {
public:
    enum class State : unsigned char { Showing, Input, Won, Failed };

    ChainDrill(ChainConfig config, std::uint32_t seed);

    // Время идёт: показ последовательности, лимит ввода. dtFrames — сколько кадров прошло.
    void advance(int dtFrames);
    // Ввод игрока. Верно — идём дальше; ошибка или просрочка — та же длина заново (новая последовательность).
    // Возвращает true, если действие верное.
    bool press(Action action);

    State state() const { return state_; }
    const std::vector<Action>& sequence() const { return sequence_; }
    // Какая иконка показана сейчас (-1 — ничего).
    int shownIndex() const;
    int length() const { return static_cast<int>(sequence_.size()); }
    int inputIndex() const { return inputIndex_; }
    int mistakes() const { return mistakes_; }
    int totalFrames() const { return elapsed_; }
    Rating rating() const;
    const ChainConfig& config() const { return cfg_; }

private:
    void newSequence(int length);
    void fail();
    std::uint32_t next();

    ChainConfig cfg_;
    std::uint32_t rng_;
    std::vector<Action> sequence_;
    State state_ = State::Showing;
    int stateFrame_ = 0, inputIndex_ = 0, mistakes_ = 0, elapsed_ = 0, sinceInput_ = 0;
};

bool parseChainConfig(const core::Json& json, ChainConfig& out, std::string& error);

// ---------------------------------------------------------------- Тень

// Силуэт мастера выполняет связку, игрок повторяет синхронно с небольшим опережением: ожидаемое время нажатия сдвинуто
// на leadFrames раньше такта мастера. Остальное — как в «Лапах».
struct ShadowConfig {
    PadConfig pads;
    int leadFrames = 6;
};

class ShadowDrill {
public:
    explicit ShadowDrill(ShadowConfig c) : lead_(c.leadFrames), drill_(std::move(c.pads)) {}
    Judgement press(Action a, int frame) { return drill_.press(a, frame + lead_); }
    void update(int frame) { drill_.update(frame + lead_); }
    const PadDrill& drill() const { return drill_; }
    int endFrame() const { return drill_.endFrame() - lead_; }

private:
    int lead_;
    PadDrill drill_;
};

bool parseShadowConfig(const core::Json& json, ShadowConfig& out, std::string& error);

// ---------------------------------------------------------------- Тяжёлый мешок

struct BagConfig {
    std::string id;
    int chargeFrames = 60;                     // шкала силы: 0 → 1 за это число кадров, затем обратно
    float greenLow = 0.7f, greenHigh = 0.85f;  // зелёная зона шкалы
    float aimTolerance = 0.25f;                // допустимое отклонение метки от точки удара (0..1); дальше — промах
    int strikes = 8;
    float ratingS = 0.85f, ratingA = 0.7f, ratingB = 0.5f, ratingC = 0.3f;
};

struct BagStrike {
    float power = 0.f;    // значение шкалы в момент отпускания, 0..1
    float quality = 0.f;  // 0..1: точность силы и прицела
    bool green = false;   // сила в зелёной зоне
    bool missed = false;  // метка слишком далеко от точки удара
};

class HeavyBag {
public:
    explicit HeavyBag(BagConfig c) : cfg_(std::move(c)) {}
    float meter(int framesHeld) const;  // треугольная волна
    // Игрок отпустил кнопку: framesHeld — сколько кадров удерживал; aimError — отклонение метки от точки удара, 0..1.
    BagStrike release(int framesHeld, float aimError);
    bool finished() const { return done_ >= cfg_.strikes; }
    int strikesDone() const { return done_; }
    float average() const { return done_ ? total_ / static_cast<float>(done_) : 0.f; }
    int greenCount() const { return greens_; }
    Rating rating() const;
    // Размах мешка от силы удара (для физики груши): 0..1.
    float swingOf(const BagStrike& s) const { return s.missed ? 0.1f : 0.3f + 0.7f * s.power; }
    const BagConfig& config() const { return cfg_; }

private:
    BagConfig cfg_;
    int done_ = 0, greens_ = 0;
    float total_ = 0.f;
};

bool parseBagConfig(const core::Json& json, BagConfig& out, std::string& error);
}  // namespace han::minigames
// Пример: PadDrill d(cfg); d.press(Action::Jab, 62); d.update(200); auto r = d.rating();
