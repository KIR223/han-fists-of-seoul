#pragma once
// Окно и события (E1) на SDL3. Заголовок не тянет SDL; реализация собирается, только если SDL3 найден (HAN_WITH_SDL).
#include <cstdint>
#include <memory>
#include <vector>

#include "han/platform/input_map.h"

namespace han::platform {

struct FrameEvents {
    bool quitRequested = false;
    float mouseDx = 0, mouseDy = 0;  // сырое смещение мыши за кадр (в режиме захвата курсора — без ускорения ОС)
    int width = 0, height = 0;
};

class Window {
public:
    // Создаёт окно; nullptr, если SDL не инициализировался.
    static std::unique_ptr<Window> create(const char* title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Забирает все события ОС, передаёт клавиши в карту действий. Вызывать один раз в начале кадра.
    FrameEvents pump(InputMap& input);
    void setCaptureMouse(bool capture);  // относительный режим мыши для камеры и боя

    // Поддержка Vulkan: расширения инстанса, поверхность (VkSurfaceKHR как uint64, 0 — ошибка), размер в пикселях.
    std::vector<const char*> vulkanInstanceExtensions() const;
    std::uint64_t createVulkanSurface(void* vkInstance);
    void pixelSize(int& width, int& height) const;

private:
    Window();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace han::platform
// Пример: auto w = Window::create("HAN", 1280, 720); while (!w->pump(input).quitRequested) { ... }
