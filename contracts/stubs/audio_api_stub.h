// Заглушка audio_api.h: звук не играет.
#pragma once
#include "contracts/audio_api.h"

namespace han::contracts::stubs {
struct AudioStub final : IAudio {
    void playEvent(std::string_view, Vec3) override {}
    void playUi(std::string_view) override {}
    void setBusVolume(Bus, float) override {}
    void playMusic(std::string_view, float) override {}
};
}  // namespace han::contracts::stubs
