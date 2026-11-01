// Тесты мини-игр зала (G2): «Лапы» — окна точности, комбо, промахи, оценка; «Связка» — показ, ввод, рост длины, ошибки.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "han/minigames/drills.h"

using namespace han::minigames;

namespace {
std::string repo(const char* sub) { return (std::filesystem::path(HAN_REPO_DIR) / sub).string(); }

han::core::Json load(const char* path) {
    std::ifstream in(repo(path), std::ios::binary);
    REQUIRE_MESSAGE(in.good(), path);
    std::stringstream ss;
    ss << in.rdbuf();
    han::core::Json j;
    std::string error;
    REQUIRE_MESSAGE(han::core::parseJson(ss.str(), j, error), error);
    return j;
}

PadConfig pads() {
    PadConfig c;
    std::string error;
    REQUIRE_MESSAGE(parsePadConfig(load("data/combat/minigames/pads_first.json"), c, error), error);
    return c;
}
ChainConfig chain() {
    ChainConfig c;
    std::string error;
    REQUIRE_MESSAGE(parseChainConfig(load("data/combat/minigames/chain_first.json"), c, error), error);
    return c;
}
}  // namespace

TEST_CASE("pads: данные читаются, доли идут по возрастанию, окна короче полудоли") {
    const PadConfig c = pads();
    CHECK(c.calls.size() == 10);
    CHECK(c.framesPerBeat() == 45);
    CHECK(c.okFrames < c.framesPerBeat() / 2);
}

TEST_CASE("pads: точное попадание — идеально, в пределах окон — хорошо и нормально, мимо окна — нажатие мимо") {
    PadDrill d(pads());
    const int f0 = d.callFrame(0);
    CHECK(d.press(Action::Jab, f0 + 1) == Judgement::Perfect);
    CHECK(d.press(Action::Jab, d.callFrame(1) - 6) == Judgement::Good);
    CHECK(d.press(Action::Cross, d.callFrame(2) + 12) == Judgement::Ok);
    CHECK(d.press(Action::Jab, d.callFrame(2) + 30) == Judgement::Wrong);  // между лапами
    CHECK(d.wrongPresses() == 1);
    CHECK(d.combo() == 0);
}

TEST_CASE("pads: не то действие — лапа теряется, комбо сбрасывается; пропущенные лапы уходят в промах") {
    PadDrill d(pads());
    CHECK(d.press(Action::Jab, d.callFrame(0)) == Judgement::Perfect);
    CHECK(d.combo() == 1);
    CHECK(d.press(Action::Cross, d.callFrame(1)) == Judgement::Wrong);  // нужен был джеб
    CHECK(d.combo() == 0);
    d.update(d.endFrame() + 5);
    CHECK(d.judgement(1) == Judgement::Wrong);
    CHECK(d.count(Judgement::Miss) == 8);
    CHECK(d.rating() == Rating::D);
}

TEST_CASE("pads: идеальная игра даёт S, комбо равно числу лап; плохая игра ниже") {
    PadDrill d(pads());
    for (std::size_t i = 0; i < d.config().calls.size(); ++i) d.press(d.config().calls[i].action, d.callFrame(i));
    d.update(d.endFrame());
    CHECK(d.accuracy() == doctest::Approx(1.f));
    CHECK(d.rating() == Rating::S);
    CHECK(d.bestCombo() == 10);
    CHECK(d.finished(d.endFrame()));

    PadDrill late(pads());
    for (std::size_t i = 0; i < late.config().calls.size(); ++i) late.press(late.config().calls[i].action, late.callFrame(i) + 11);
    late.update(late.endFrame());
    CHECK(late.accuracy() == doctest::Approx(0.4f));
    CHECK(late.rating() == Rating::D);
}

TEST_CASE("pads: плохие данные отклоняются с понятной ошибкой") {
    PadConfig c;
    std::string error;
    han::core::Json j;
    REQUIRE(han::core::parseJson(R"({"id":"x","bpm":90,"calls":[{"beat":2,"action":"jab"},{"beat":1,"action":"jab"}]})", j, error));
    CHECK_FALSE(parsePadConfig(j, c, error));
    CHECK(error.find("возрастать") != std::string::npos);
    REQUIRE(han::core::parseJson(R"({"id":"x","calls":[{"beat":0,"action":"fly"}]})", j, error));
    CHECK_FALSE(parsePadConfig(j, c, error));
    CHECK(error.find("неизвестное действие") != std::string::npos);
}

TEST_CASE("shadow: нажатие чуть раньше такта точное, с опозданием на тот же срок — хуже") {
    ShadowConfig c;
    std::string error;
    REQUIRE_MESSAGE(parseShadowConfig(load("data/combat/minigames/shadow_first.json"), c, error), error);
    ShadowDrill early(c);
    CHECK(early.press(Action::Jab, early.drill().callFrame(0) - c.leadFrames) == Judgement::Perfect);
    ShadowDrill late(c);
    CHECK(late.press(Action::Jab, late.drill().callFrame(0) + c.leadFrames) != Judgement::Perfect);
    ShadowDrill all(c);
    for (std::size_t i = 0; i < c.pads.calls.size(); ++i) all.press(c.pads.calls[i].action, all.drill().callFrame(i) - c.leadFrames);
    all.update(all.endFrame() + 30);
    CHECK(all.drill().rating() == Rating::S);
}

TEST_CASE("bag: шкала — треугольная волна, зелёная зона даёт полное качество, промах мимо метки обнуляет удар") {
    BagConfig c;
    std::string error;
    REQUIRE_MESSAGE(parseBagConfig(load("data/combat/minigames/bag_first.json"), c, error), error);
    HeavyBag bag(c);
    CHECK(bag.meter(0) == doctest::Approx(0.f));
    CHECK(bag.meter(30) == doctest::Approx(0.5f));
    CHECK(bag.meter(60) == doctest::Approx(1.f));
    CHECK(bag.meter(90) == doctest::Approx(0.5f));
    CHECK(bag.meter(120) == doctest::Approx(0.f));
    const BagStrike perfect = bag.release(47, 0.f);  // 47/60 = 0.78 — в зелёной зоне
    CHECK(perfect.green);
    CHECK(perfect.quality == doctest::Approx(1.f));
    const BagStrike weak = bag.release(12, 0.f);
    CHECK_FALSE(weak.green);
    CHECK(weak.quality < 0.1f);
    const BagStrike off = bag.release(47, 0.6f);
    CHECK(off.missed);
    CHECK(off.quality == doctest::Approx(0.f));
    CHECK(bag.swingOf(perfect) > bag.swingOf(weak));
    CHECK(bag.swingOf(off) < 0.2f);
}

TEST_CASE("bag: серия точных ударов — S, серия слабых — D, лишние удары после конца не считаются") {
    BagConfig c;
    std::string error;
    REQUIRE_MESSAGE(parseBagConfig(load("data/combat/minigames/bag_first.json"), c, error), error);
    HeavyBag good(c), bad(c);
    for (int i = 0; i < c.strikes; ++i) {
        good.release(47, 0.02f);
        bad.release(8, 0.f);
    }
    CHECK(good.finished());
    CHECK(good.rating() == Rating::S);
    CHECK(good.greenCount() == c.strikes);
    CHECK(bad.rating() == Rating::D);
    good.release(8, 0.f);
    CHECK(good.strikesDone() == c.strikes);
    CHECK(good.rating() == Rating::S);
}

TEST_CASE("bag: неверные параметры шкалы отклоняются") {
    BagConfig c;
    std::string error;
    han::core::Json j;
    REQUIRE(han::core::parseJson(R"({"id":"x","green":[0.9,0.8]})", j, error));
    CHECK_FALSE(parseBagConfig(j, c, error));
    CHECK(error.find("шкалы") != std::string::npos);
}

namespace {
// Проигрывает связку: ждёт конца показа и вводит верную последовательность.
void playCorrect(ChainDrill& d) {
    while (d.state() == ChainDrill::State::Showing) d.advance(1);
    const std::vector<Action> seq = d.sequence();
    for (Action a : seq) CHECK(d.press(a));
}
}  // namespace

TEST_CASE("chain: показ идёт по иконкам, во время показа нажатия не считаются") {
    ChainDrill d(chain(), 7);
    CHECK(d.length() == 4);
    CHECK(d.shownIndex() == 0);
    d.advance(31);
    CHECK(d.shownIndex() == 1);
    CHECK_FALSE(d.press(Action::Jab));
    CHECK(d.mistakes() == 0);
    d.advance(100);
    CHECK(d.state() == ChainDrill::State::Input);
    CHECK(d.shownIndex() == -1);
}

TEST_CASE("chain: верный ввод растит длину до максимума и выигрывает без ошибок на S") {
    ChainDrill d(chain(), 3);
    for (int len = 4; len <= 8; ++len) {
        CHECK(d.length() == len);
        playCorrect(d);
    }
    CHECK(d.state() == ChainDrill::State::Won);
    CHECK(d.rating() == Rating::S);
}

TEST_CASE("chain: ошибка или просрочка — та же длина заново; слишком много ошибок — провал") {
    ChainDrill d(chain(), 5);
    while (d.state() == ChainDrill::State::Showing) d.advance(1);
    const Action wrong = d.sequence()[0] == Action::Jab ? Action::Cross : Action::Jab;
    CHECK_FALSE(d.press(wrong));
    CHECK(d.mistakes() == 1);
    CHECK(d.length() == 4);
    CHECK(d.state() == ChainDrill::State::Showing);
    while (d.state() == ChainDrill::State::Showing) d.advance(1);
    d.advance(200);  // просрочка ввода
    CHECK(d.mistakes() == 2);
    for (int i = 0; i < 20 && d.state() != ChainDrill::State::Failed; ++i) d.advance(500);
    CHECK(d.state() == ChainDrill::State::Failed);
    CHECK(d.rating() == Rating::D);
}

TEST_CASE("chain: одно и то же зерно — одна и та же последовательность; оценка по числу ошибок") {
    ChainDrill a(chain(), 11), b(chain(), 11), c(chain(), 12);
    CHECK(a.sequence() == b.sequence());
    CHECK(a.sequence() != c.sequence());
    ChainDrill d(chain(), 9);
    for (int k = 0; k < 2; ++k) {
        while (d.state() == ChainDrill::State::Showing) d.advance(1);
        d.press(d.sequence()[0] == Action::Jab ? Action::Cross : Action::Jab);
    }
    for (int len = 4; len <= 8; ++len) playCorrect(d);
    CHECK(d.state() == ChainDrill::State::Won);
    CHECK(d.mistakes() == 2);
    CHECK(d.rating() == Rating::B);
}
