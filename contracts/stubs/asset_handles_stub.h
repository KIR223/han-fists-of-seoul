// Заглушка asset_handles.h: выдаёт хэндлы по порядку, все ассеты «готовы».
#pragma once
#include "contracts/asset_handles.h"

namespace han::contracts::stubs {
struct AssetsStub final : IAssets {
    AssetHandle load(AssetKind, std::string_view) override { return {++next_, 1}; }
    bool isReady(AssetHandle h) const override { return static_cast<bool>(h); }
    void release(AssetHandle) override {}

private:
    std::uint32_t next_ = 0;
};
}  // namespace han::contracts::stubs
