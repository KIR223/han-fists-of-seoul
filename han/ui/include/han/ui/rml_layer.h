#pragma once
// Игровой интерфейс на RmlUi (E8): документы .rml/.rcss, шрифты FreeType (кириллица и хангыль), мышь.
// Геометрия RmlUi рисуется через список рисования Dear ImGui (фон окна), а текстуры RmlUi становятся текстурами ImGui: свой Vulkan-код
// интерфейсу не нужен. Поэтому RmlLayer работает только вместе с ImGuiLayer (ImGuiLayer.init → RmlLayer.init, порядок кадра ниже).
// Собирается, если найдены RmlUi, imgui, SDL3 и Vulkan (HAN_WITH_RMLUI).
#ifdef HAN_WITH_RMLUI

#include <memory>
#include <string>

#include "han/platform/window.h"

namespace han::ui {

class RmlLayer {
public:
    RmlLayer();
    ~RmlLayer();  // уничтожать до ImGuiLayer: текстуры RmlUi живут в списке текстур ImGui
    RmlLayer(const RmlLayer&) = delete;
    RmlLayer& operator=(const RmlLayer&) = delete;

    // Создаёт контекст RmlUi размером с окно. false — не удалось (причина в stderr).
    bool init(int width, int height);
    // Шрифт для текста. fallback = true — запасной (глифы, которых нет в основных: например хангыль).
    bool loadFont(const std::string& path, bool fallback = false);
    // Загружает и показывает документ (.rml). Возвращает false, если файл не найден или разобран с ошибкой.
    bool loadDocument(const std::string& path);
    void closeAll();

    // Каждый кадр после ImGuiLayer::beginFrame(): размер окна, мышь, расчёт раскладки и анимаций.
    void beginFrame(int width, int height);
    // Рисует интерфейс поверх сцены (в список рисования ImGui). Вызывать между beginFrame() и отрисовкой кадра.
    void draw();

    // Курсор над элементом интерфейса — игра не должна реагировать на клик.
    bool wantsMouse() const;
    // Нажатие на кнопку: id ближайшего элемента с id вида «btn-…» (в .rml обработчиков onclick нет: игра забирает события здесь). Забирается один раз.
    std::string takeClicked();
    // Текст элемента по id (для проверок и интерфейса-демо) и его замена; false — элемента нет.
    bool setText(const std::string& elementId, const std::string& text);
    // Число видимых элементов с классом/тегом (для тестов).
    int count(const std::string& cssSelector) const;
    // Мышь без SDL (тесты, скрипты, автодемо): позиция в пикселях и состояние левой кнопки; сразу пересчитывает раскладку.
    void injectMouse(int x, int y, bool leftDown);
    // Центр элемента по id в пикселях окна; false — элемента нет.
    bool elementCenter(const std::string& elementId, int& x, int& y) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace han::ui
// Пример: RmlLayer rml; rml.init(w, h); rml.loadFont("NotoSansKR.ttf"); rml.loadDocument("hud.rml"); // в кадре: ui.beginFrame(); rml.beginFrame(w, h); rml.draw();

#endif  // HAN_WITH_RMLUI
