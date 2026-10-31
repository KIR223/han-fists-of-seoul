// contracts/ui_api.h — поставщик: A (han/ui, RmlUi), потребители: B (экраны, HUD, меню, телефон).
// Зачем: B описывает экраны данными (data/ui), A показывает их и связывает с данными.
#pragma once
#include <functional>
#include <string_view>

namespace han::contracts {
struct IUi {
    virtual void showScreen(std::string_view screenId) = 0;
    virtual void hideScreen(std::string_view screenId) = 0;
    virtual void setValue(std::string_view binding, double value) = 0;            // число (здоровье, стамина)
    virtual void setText(std::string_view binding, std::string_view locKey) = 0;  // только ключ локализации, не текст
    virtual void onButton(std::string_view buttonId, std::function<void()> handler) = 0;
    virtual ~IUi() = default;
};
}  // namespace han::contracts
// Пример: ui.showScreen("pause"); ui.setValue("hud.health", 0.8);
