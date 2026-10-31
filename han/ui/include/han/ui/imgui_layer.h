#pragma once
// Отладочный интерфейс на Dear ImGui (E1/E2): консоль команд и панель статистики поверх сцены.
// Собирается, если найдены imgui, SDL3 и Vulkan (HAN_WITH_IMGUI); игровой интерфейс (RmlUi) — отдельный этап E8.
#ifdef HAN_WITH_IMGUI

#include <memory>
#include <string>
#include <vector>

#include "han/core/console.h"
#include "han/platform/window.h"
#include "han/render/overlay.h"

namespace han::ui {

// Порядок жизни: ImGuiLayer должен быть объявлен РАНЬШЕ Renderer (то есть уничтожаться позже него).
class ImGuiLayer final : public render::IOverlay {
public:
    ImGuiLayer();
    ~ImGuiLayer() override;
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // Создаёт контекст ImGui и подключает события окна. false — не удалось.
    bool init(platform::Window& window);

    // Каждый кадр: сразу после window.pump() и до построения интерфейса.
    void beginFrame();
    // Интерфейс забрал ввод — игра не должна реагировать на клавиатуру/мышь.
    bool wantsKeyboard() const;
    bool wantsMouse() const;

    // Окно консоли. open — флаг видимости (крестик окна его сбрасывает).
    void drawConsole(core::Console& console, bool& open);
    // Полупрозрачная панель в углу экрана.
    void drawStats(const std::vector<std::string>& lines);

    // IOverlay
    void onRendererReady(const render::VulkanHandles& handles) override;
    void record(void* vkCommandBuffer) override;
    void onRendererShutdown() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace han::ui
// Пример: ImGuiLayer ui; ui.init(*window); renderer->setOverlay(&ui);  // в цикле: ui.beginFrame(); ui.drawConsole(console, open);

#endif  // HAN_WITH_IMGUI
