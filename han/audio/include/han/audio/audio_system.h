// han/audio/audio_system.h — звук на miniaudio (E7): 3D-позиционирование, шины, музыка с плавной сменой, события из таблицы.
// Реализует контракт IAudio (contracts/audio_api.h). miniaudio наружу не торчит (pimpl).
// Режим headless — без звуковой карты: микшер читается вручную renderFrames() (тесты, CI, запись).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "contracts/audio_api.h"
#include "han/audio/audio_events.h"

namespace han::audio {
struct AudioSettings {
    bool headless = false;          // без устройства вывода
    std::uint32_t sampleRate = 48000;  // для headless; с устройством берётся его частота
    std::uint32_t maxVoices = 64;      // одновременных звуков; при переполнении вытесняется самый старый короткий
    std::uint32_t seed = 1;            // зерно разброса громкости и высоты (воспроизводимо)
};

// true, если модуль собран с miniaudio (иначе create() возвращает nullptr).
bool hasAudio();

class AudioSystem final : public contracts::IAudio {
public:
    static std::unique_ptr<AudioSystem> create(const AudioSettings& settings, std::string& error);
    ~AudioSystem() override;

    void addEvent(SoundEvent event);  // заменяет событие с тем же id
    bool hasEvent(std::string_view id) const;
    std::size_t eventCount() const;

    // Слушатель: камера или голова персонажа. forward и up не обязаны быть единичными.
    void setListener(contracts::Vec3 position, contracts::Vec3 forward, contracts::Vec3 up = {0.f, 1.f, 0.f});

    // IAudio
    void playEvent(std::string_view eventId, contracts::Vec3 position) override;
    void playUi(std::string_view eventId) override;
    void setBusVolume(contracts::Bus bus, float volume01) override;
    void playMusic(std::string_view trackId, float fadeSeconds = 1.f) override;

    float busVolume(contracts::Bus bus) const;
    void stopMusic(float fadeSeconds = 1.f);
    std::string currentMusic() const;  // пусто — музыки нет

    // Раз в кадр: убирает закончившиеся звуки, ведёт плавную смену музыки.
    void update(float dt);

    std::uint32_t activeVoices() const;
    std::uint32_t failedPlays() const;  // сколько раз событие не нашлось или файл не открылся
    std::string lastProblem() const;

    // Только headless: микширует frames кадров в out (стерео, float, чередование каналов). Возвращает прочитанное число кадров.
    std::uint32_t renderFrames(float* out, std::uint32_t frames);
    std::uint32_t sampleRate() const;

private:
    AudioSystem();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace han::audio
// Пример: auto a = AudioSystem::create({}, err); a->addEvent({"hit", Bus::Sfx, {"hit.wav"}}); a->playEvent("hit", point); a->update(dt);
