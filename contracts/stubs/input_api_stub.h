// Заглушка input_api.h: ничего не нажато.
#pragma once
#include "contracts/input_api.h"

namespace han::contracts::stubs {
struct InputStub final : IInput {
    bool pressed(ActionId) const override { return false; }
    bool held(ActionId) const override { return false; }
    bool released(ActionId) const override { return false; }
    float axis(ActionId) const override { return 0.f; }
    float bufferedAge(ActionId) const override { return -1.f; }
    void setControlMode(ControlMode) override {}
};
}  // namespace han::contracts::stubs
