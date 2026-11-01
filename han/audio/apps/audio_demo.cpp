// Демо E7: проверка звука на настоящем устройстве. Генерирует тон (синус 440 Гц и короткий «щелчок»), играет его 3D-источником,
// который облетает слушателя по кругу, затем музыку-тон с плавным включением. Слушайте в наушниках: звук должен ходить слева направо и сзади.
// Запуск: han_audio_demo.exe [--seconds N]  (по умолчанию 8 с). Код выхода 0 — всё сыграно без ошибок.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "han/audio/audio_system.h"

namespace {
void writeTone(const std::filesystem::path& path, float freq, float seconds, float amplitude, bool click) {
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
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        float envelope = 1.f;
        if (click) envelope = std::exp(-t * 18.f);  // затухающий «щелчок»
        const float edge = std::min({1.f, t * 50.f, (seconds - t) * 50.f});  // без щелчков на краях
        const float v = std::sin(6.2831853f * freq * t) * amplitude * envelope * std::max(edge, 0.f);
        u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v * 32767.f)));
    }
}
}  // namespace

int main(int argc, char** argv) {
    float seconds = 8.f;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::max(2.f, static_cast<float>(std::atof(argv[i + 1])));

    const auto dir = std::filesystem::temp_directory_path() / "han_audio_demo";
    std::filesystem::create_directories(dir);
    writeTone(dir / "beep.wav", 660.f, 0.25f, 0.6f, true);
    writeTone(dir / "music.wav", 220.f, 2.0f, 0.25f, false);

    han::audio::AudioSettings settings;  // с устройством (headless = false)
    std::string error;
    auto audio = han::audio::AudioSystem::create(settings, error);
    if (!audio) {
        std::fprintf(stderr, "звук не запустился: %s\n", error.c_str());
        return 1;
    }
    std::printf("звуковая система запущена, частота %u Гц\n", audio->sampleRate());

    han::audio::SoundEvent beep;
    beep.id = "beep";
    beep.files = {dir / "beep.wav"};
    beep.bus = han::contracts::Bus::Sfx;
    beep.minDistance = 1.f;
    beep.maxDistance = 30.f;
    beep.pitchJitter = 0.f;
    audio->addEvent(beep);
    han::audio::SoundEvent music;
    music.id = "music";
    music.files = {dir / "music.wav"};
    music.bus = han::contracts::Bus::Music;
    music.loop = true;
    music.spatial = false;
    audio->addEvent(music);

    audio->setListener({0, 0, 0}, {0, 0, -1});
    audio->setBusVolume(han::contracts::Bus::Music, 0.35f);
    audio->playMusic("music", 1.5f);

    const auto start = std::chrono::steady_clock::now();
    auto last = start;
    int beeps = 0;
    double nextBeep = 0;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const double t = std::chrono::duration<double>(now - start).count();
        if (t >= seconds) break;
        audio->update(static_cast<float>(std::chrono::duration<double>(now - last).count()));
        last = now;
        // Источник идёт по кругу радиусом 6 м: сначала справа от слушателя, потом сзади, слева, спереди (оборот за 4 с).
        const float angle = static_cast<float>(t) * 1.5707963f;
        if (t >= nextBeep) {
            audio->playEvent("beep", {std::sin(angle) * 6.f, 0.f, std::cos(angle) * 6.f});  // z > 0 — сзади (слушатель смотрит в −Z)
            nextBeep = t + 0.4;
            ++beeps;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("сыграно щелчков: %d, неудачных воспроизведений: %u\n", beeps, audio->failedPlays());
    if (audio->failedPlays() > 0) std::fprintf(stderr, "последняя проблема: %s\n", audio->lastProblem().c_str());
    return audio->failedPlays() == 0 ? 0 : 1;
}
