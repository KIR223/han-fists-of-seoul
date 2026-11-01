// Тесты звука (E7). Таблица событий — всегда; звуковой движок — если модуль собран с miniaudio (на CI — да). Без звуковой карты: режим headless.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "han/audio/audio_system.h"

using namespace han;
using contracts::Bus;
using contracts::Vec3;

TEST_CASE("audio: таблица событий разбирается, пути склеиваются с базовой папкой") {
    std::vector<audio::SoundEvent> events;
    std::string error;
    const std::string text =
        "# комментарий\n"
        "hit_heavy\tSfx\thit1.wav;hit2.wav\t0.8\t0.1\t1.0\t0.05\t1\t2\t40\t0\r\n"
        "\n"
        "theme\tMusic\tmusic/theme.wav\t1\t0\t1\t0\t0\t1\t10\t1\n";
    REQUIRE_MESSAGE(audio::parseEventTable(text, "assets", events, error), error);
    REQUIRE(events.size() == 2);
    CHECK(events[0].id == "hit_heavy");
    CHECK(events[0].bus == Bus::Sfx);
    REQUIRE(events[0].files.size() == 2);
    CHECK(events[0].files[1] == std::filesystem::path("assets") / "hit2.wav");
    CHECK(events[0].volume == doctest::Approx(0.8f));
    CHECK(events[0].maxDistance == doctest::Approx(40.f));
    CHECK(events[0].spatial);
    CHECK_FALSE(events[0].loop);
    CHECK(events[1].bus == Bus::Music);
    CHECK_FALSE(events[1].spatial);
    CHECK(events[1].loop);
}

TEST_CASE("audio: таблица событий отклоняет ошибки с номером строки и ничего не добавляет") {
    std::vector<audio::SoundEvent> events;
    std::string error;
    const auto bad = [&](const std::string& row, const char* expectedLine) {
        events.clear();
        CHECK_FALSE(audio::parseEventTable("# шапка\n" + row, ".", events, error));
        CHECK(error.find(expectedLine) != std::string::npos);
        CHECK(events.empty());
    };
    bad("a\tSfx\tx.wav\t1\t0\t1\t0\t1\t1\t10\n", "строка 2");  // 10 колонок
    bad("a\tHorn\tx.wav\t1\t0\t1\t0\t1\t1\t10\t0\n", "шина");
    bad("a\tSfx\t\t1\t0\t1\t0\t1\t1\t10\t0\n", "файл");
    bad("a\tSfx\t../x.wav\t1\t0\t1\t0\t1\t1\t10\t0\n", "относительным");
    bad("a\tSfx\tx.wav\t5\t0\t1\t0\t1\t1\t10\t0\n", "громкость");
    bad("a\tSfx\tx.wav\t1\t0\t9\t0\t1\t1\t10\t0\n", "высота");
    bad("a\tSfx\tx.wav\t1\t0\t1\t0\t2\t1\t10\t0\n", "3D");
    bad("a\tSfx\tx.wav\t1\t0\t1\t0\t1\t10\t5\t0\n", "макс. дистанция");
    bad("a\tSfx\tx.wav\t1\t0\t1\t0\t1\t1\t10\t0\na\tSfx\ty.wav\t1\t0\t1\t0\t1\t1\t10\t0\n", "дважды");
    CHECK_FALSE(audio::loadEventTable("нет_такого_файла.tsv", ".", events, error));
}

TEST_CASE("audio: имена шин и hasAudio согласованы со сборкой") {
    for (int i = 0; i < 6; ++i) {
        Bus back;
        REQUIRE(audio::busFromName(audio::busName(static_cast<Bus>(i)), back));
        CHECK(back == static_cast<Bus>(i));
    }
#ifdef HAN_HAS_MINIAUDIO
    CHECK(audio::hasAudio());
#else
    CHECK_FALSE(audio::hasAudio());
#endif
}

#ifdef HAN_HAS_MINIAUDIO
namespace {
// Синусоида 16 бит, моно, 48 кГц.
void writeWav(const std::filesystem::path& path, float freq, float seconds, float amplitude = 0.5f) {
    const std::uint32_t rate = 48000, frames = static_cast<std::uint32_t>(seconds * static_cast<float>(rate));
    std::ofstream out(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    u32(36 + frames * 2);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(frames * 2);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto s = static_cast<std::int16_t>(std::sin(6.2831853f * freq * static_cast<float>(i) / static_cast<float>(rate)) * amplitude * 32767.f);
        u16(static_cast<std::uint16_t>(s));
    }
}

struct Rms {
    float left = 0, right = 0;
};

// Микширует seconds секунд и возвращает среднеквадратичную громкость каждого канала.
Rms render(audio::AudioSystem& a, float seconds) {
    const std::uint32_t total = static_cast<std::uint32_t>(seconds * static_cast<float>(a.sampleRate()));
    std::vector<float> buf(2 * 480);
    double l = 0, r = 0;
    std::uint32_t done = 0;
    while (done < total) {
        const std::uint32_t n = std::min<std::uint32_t>(480, total - done);
        const std::uint32_t got = a.renderFrames(buf.data(), n);
        for (std::uint32_t i = 0; i < got; ++i) {
            l += static_cast<double>(buf[2 * i]) * buf[2 * i];
            r += static_cast<double>(buf[2 * i + 1]) * buf[2 * i + 1];
        }
        a.update(0.01f);
        if (got == 0) break;
        done += got;
    }
    return {static_cast<float>(std::sqrt(l / total)), static_cast<float>(std::sqrt(r / total))};
}

struct Fixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "han_audio_test";
    std::unique_ptr<audio::AudioSystem> sys;
    explicit Fixture(std::uint32_t maxVoices = 64, std::uint32_t seed = 1) {
        std::filesystem::create_directories(dir);
        writeWav(dir / "tone.wav", 440.f, 1.0f);
        writeWav(dir / "short.wav", 880.f, 0.2f);
        writeWav(dir / "music_a.wav", 220.f, 0.5f);
        writeWav(dir / "music_b.wav", 330.f, 0.5f);
        audio::AudioSettings s;
        s.headless = true;
        s.maxVoices = maxVoices;
        s.seed = seed;
        s.streamMusic = false;  // поток читается фоновым заданием: в тесте это даёт щель при зацикливании короткого файла
        std::string error;
        sys = audio::AudioSystem::create(s, error);
        REQUIRE_MESSAGE(sys, error);
        sys->setListener({0, 0, 0}, {0, 0, -1});
        add("tone", "tone.wav", Bus::Sfx, true);
        add("short", "short.wav", Bus::Sfx, true);
        add("ui_click", "tone.wav", Bus::Ui, false);
        add("music_a", "music_a.wav", Bus::Music, false, true);
        add("music_b", "music_b.wav", Bus::Music, false, true);
    }
    void add(const char* id, const char* file, Bus bus, bool spatial, bool loop = false) {
        audio::SoundEvent e;
        e.id = id;
        e.files = {dir / file};
        e.bus = bus;
        e.spatial = spatial;
        e.loop = loop;
        e.minDistance = 1.f;
        e.maxDistance = 60.f;
        sys->addEvent(e);
    }
};
}  // namespace

TEST_CASE("audio: событие играет, даёт звук и после конца файла голос освобождается") {
    Fixture f;
    f.sys->playEvent("short", {0, 0, -2});
    CHECK(f.sys->activeVoices() == 1);
    const Rms loud = render(*f.sys, 0.1f);
    CHECK(loud.left > 0.01f);
    CHECK(loud.right > 0.01f);
    render(*f.sys, 0.5f);  // файл (0.2 с) закончился
    CHECK(f.sys->activeVoices() == 0);
    const Rms quiet = render(*f.sys, 0.1f);
    CHECK(quiet.left < 0.0005f);
}

TEST_CASE("audio: неизвестное событие и отсутствующий файл не ломают игру, а считаются") {
    Fixture f;
    f.sys->playEvent("нет_такого", {0, 0, 0});
    CHECK(f.sys->failedPlays() == 1);
    CHECK(f.sys->lastProblem().find("нет события") != std::string::npos);
    audio::SoundEvent e;
    e.id = "ghost";
    e.files = {f.dir / "ghost.wav"};
    f.sys->addEvent(e);
    f.sys->playEvent("ghost", {0, 0, 0});
    CHECK(f.sys->failedPlays() == 2);
    CHECK(f.sys->activeVoices() == 0);
    f.sys->playMusic("нет_трека");
    CHECK(f.sys->failedPlays() == 3);
    CHECK(f.sys->currentMusic().empty());
}

TEST_CASE("audio: 3D — звук справа громче в правом канале, слева — в левом") {
    {
        Fixture f;
        f.sys->playEvent("tone", {8, 0, 0});
        render(*f.sys, 0.05f);
        const Rms r = render(*f.sys, 0.2f);
        CHECK(r.right > r.left * 1.5f);
    }
    {
        Fixture f;
        f.sys->playEvent("tone", {-8, 0, 0});
        render(*f.sys, 0.05f);
        const Rms r = render(*f.sys, 0.2f);
        CHECK(r.left > r.right * 1.5f);
    }
}

TEST_CASE("audio: громкость падает с расстоянием и на максимальной дистанции пропадает") {
    const auto level = [](float distance) {
        Fixture f;
        f.sys->playEvent("tone", {0, 0, -distance});
        render(*f.sys, 0.05f);
        const Rms r = render(*f.sys, 0.2f);
        return r.left + r.right;
    };
    const float near = level(2.f), mid = level(30.f), far = level(100.f);
    CHECK(near > mid * 1.5f);
    CHECK(mid > far);
    CHECK(far < 0.001f);
}

TEST_CASE("audio: громкость шин — Sfx выключена даёт тишину, Master 0.5 уменьшает вдвое") {
    const auto level = [](Bus bus, float volume) {
        Fixture f;
        f.sys->setBusVolume(bus, volume);
        f.sys->playEvent("tone", {0, 0, -2});
        render(*f.sys, 0.05f);
        const Rms r = render(*f.sys, 0.2f);
        return (r.left + r.right) * 0.5f;
    };
    const float full = level(Bus::Sfx, 1.f);
    CHECK(level(Bus::Sfx, 0.f) < 0.0005f);
    CHECK(level(Bus::Master, 0.5f) == doctest::Approx(full * 0.5f).epsilon(0.1));
    Fixture f;
    f.sys->setBusVolume(Bus::Ui, 2.f);  // выше 1 обрезается
    CHECK(f.sys->busVolume(Bus::Ui) == doctest::Approx(1.f));
    f.sys->setBusVolume(Bus::Ui, -1.f);
    CHECK(f.sys->busVolume(Bus::Ui) == doctest::Approx(0.f));
}

TEST_CASE("audio: звук интерфейса плоский — одинаков в обоих каналах независимо от слушателя") {
    Fixture f;
    f.sys->setListener({50, 0, 50}, {1, 0, 0});
    f.sys->playUi("ui_click");
    render(*f.sys, 0.05f);
    const Rms r = render(*f.sys, 0.2f);
    CHECK(r.left > 0.01f);
    CHECK(r.left == doctest::Approx(r.right).epsilon(0.05));
}

TEST_CASE("audio: музыка — плавная смена трека, остановка с затуханием") {
    Fixture f;
    f.sys->playMusic("music_a", 0.2f);
    CHECK(f.sys->currentMusic() == "music_a");
    CHECK(f.sys->activeVoices() == 1);
    f.sys->playMusic("music_a", 0.2f);  // повтор того же трека ничего не добавляет
    CHECK(f.sys->activeVoices() == 1);
    render(*f.sys, 0.5f);
    f.sys->playMusic("music_b", 0.2f);
    CHECK(f.sys->currentMusic() == "music_b");
    CHECK(f.sys->activeVoices() == 2);  // старый затухает, новый нарастает
    render(*f.sys, 0.5f);
    CHECK(f.sys->activeVoices() == 1);  // старый убран после затухания
    const Rms playing = render(*f.sys, 0.1f);
    CHECK(playing.left > 0.01f);        // музыка зациклена и продолжает играть дольше длины файла (0.5 с)
    f.sys->stopMusic(0.2f);
    CHECK(f.sys->currentMusic().empty());
    render(*f.sys, 0.5f);
    CHECK(f.sys->activeVoices() == 0);
    CHECK(render(*f.sys, 0.1f).left < 0.0005f);
}

TEST_CASE("audio: лимит голосов — лишние вытесняют самые старые, музыка не вытесняется") {
    Fixture f(4);
    f.sys->playMusic("music_a", 0.f);
    for (int i = 0; i < 12; ++i) f.sys->playEvent("tone", {0, 0, -3});
    CHECK(f.sys->activeVoices() <= 4);
    CHECK(f.sys->currentMusic() == "music_a");
    render(*f.sys, 0.1f);
    CHECK(render(*f.sys, 0.1f).left > 0.01f);
}

TEST_CASE("audio: разброс громкости воспроизводим при одинаковом зерне и меняется при другом") {
    const auto level = [](std::uint32_t seed) {
        Fixture f(64, seed);
        audio::SoundEvent e;
        e.id = "jitter";
        e.files = {f.dir / "tone.wav"};
        e.volume = 1.f;
        e.volumeJitter = 0.5f;
        e.spatial = false;
        f.sys->addEvent(e);
        f.sys->playEvent("jitter", {0, 0, 0});
        render(*f.sys, 0.05f);
        const Rms r = render(*f.sys, 0.2f);
        return r.left;
    };
    CHECK(level(7) == doctest::Approx(level(7)).epsilon(0.001));
    CHECK(level(7) != doctest::Approx(level(12345)).epsilon(0.01));
}

namespace {
// Число пересечений нуля левым каналом за seconds секунд микширования: оценка частоты звука (для синуса — 2 на период).
int zeroCrossings(audio::AudioSystem& a, float seconds) {
    const std::uint32_t total = static_cast<std::uint32_t>(seconds * static_cast<float>(a.sampleRate()));
    std::vector<float> buf(2 * 480);
    int crossings = 0;
    float previous = 0.f;
    std::uint32_t done = 0;
    while (done < total) {
        const std::uint32_t got = a.renderFrames(buf.data(), 480);
        if (got == 0) break;
        for (std::uint32_t i = 0; i < got; ++i) {
            const float v = buf[2 * i];
            if ((previous < 0.f && v >= 0.f) || (previous > 0.f && v <= 0.f)) ++crossings;
            if (v != 0.f) previous = v;
        }
        a.update(0.01f);
        done += got;
    }
    return crossings;
}
}  // namespace

TEST_CASE("audio E12: управляемый голос — высота и громкость меняются на лету (двигатель машины)") {
    Fixture f;
    f.add("engine", "tone.wav", Bus::Sfx, true, true);  // 440 Гц, зациклен
    const auto voice = f.sys->playLoop("engine", {0, 0, -3});
    REQUIRE(voice != 0);
    CHECK(f.sys->voiceAlive(voice));
    render(*f.sys, 0.2f);  // разгон сглаживания громкости
    const int base = zeroCrossings(*f.sys, 0.5f);
    f.sys->setVoice(voice, {0, 0, -3}, 1.f, 2.f);
    render(*f.sys, 0.1f);
    const int high = zeroCrossings(*f.sys, 0.5f);
    CHECK(base > 300);   // ~440 Гц за 0,5 с: около 440 пересечений
    CHECK(high > base * 1.8);
    CHECK(high < base * 2.2);
    f.sys->setVoice(voice, {0, 0, -3}, 0.f, 1.f);
    render(*f.sys, 0.3f);
    CHECK(render(*f.sys, 0.1f).left < 0.001f);  // нулевая громкость — тишина
    // Зацикленный голос переживает конец файла (1 с) и лимит голосов; stopVoice его убирает.
    for (int i = 0; i < 100; ++i) f.sys->playEvent("short", {0, 0, -3});
    render(*f.sys, 1.5f);
    CHECK(f.sys->voiceAlive(voice));
    f.sys->stopVoice(voice);
    CHECK_FALSE(f.sys->voiceAlive(voice));
    f.sys->setVoice(voice, {0, 0, 0}, 1.f, 1.f);  // после остановки безопасно
    f.sys->stopVoice(voice);
    f.sys->stopVoice(0);
}

TEST_CASE("audio E12: playLoop отказывает для незацикленного и неизвестного события") {
    Fixture f;
    CHECK(f.sys->playLoop("tone", {0, 0, 0}) == 0);  // не зациклен
    CHECK(f.sys->playLoop("нет_такого", {0, 0, 0}) == 0);
    CHECK(f.sys->failedPlays() == 2);
    CHECK(f.sys->activeVoices() == 0);
}
#endif
