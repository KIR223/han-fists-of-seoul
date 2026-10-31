// Заглушка skinning.h: палитра принимается и отбрасывается.
#pragma once
#include "contracts/skinning.h"

namespace han::contracts::stubs {
struct SkinningStub final : ISkinning {
    void submit(EntityId, const BonePalette&) override {}
    void setVisible(EntityId, bool) override {}
};
}  // namespace han::contracts::stubs
