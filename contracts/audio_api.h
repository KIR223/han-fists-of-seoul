// contracts/audio_api.h — поставщик: A, потребители: B.
// Зачем: сыграть событие в точке, управлять шинами, музыкой и радио; звук из физики A делает сам.
#pragma once
#include <cstdint>
#include <string_view>

#include "contracts/common.h"

namespace han::contracts {
enum class Bus : std::uint8_t { Master, Sfx, Voice, Music, Ambient, Ui };

struct IAudio {
    virtual void playEvent(std::string_view eventId, Vec3 position) = 0;
    virtual void playUi(std::string_view eventId) = 0;
    virtual void setBusVolume(Bus bus, float volume01) = 0;
    virtual void playMusic(std::string_view trackId, float fadeSeconds = 1.f) = 0;
    virtual ~IAudio() = default;
};
}  // namespace han::contracts
// Пример: audio.playEvent("hit_body_heavy", hit.point);
