// Заглушка ui_api.h: экраны не показываются, обработчики кнопок не вызываются.
#pragma once
#include "contracts/ui_api.h"

namespace han::contracts::stubs {
struct UiStub final : IUi {
    void showScreen(std::string_view) override {}
    void hideScreen(std::string_view) override {}
    void setValue(std::string_view, double) override {}
    void setText(std::string_view, std::string_view) override {}
    void onButton(std::string_view, std::function<void()>) override {}
};
}  // namespace han::contracts::stubs
