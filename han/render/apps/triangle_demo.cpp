// Демо E2: вращающийся треугольник через Vulkan 1.3 со слоем валидации.
// Запуск: han_triangle_demo.exe [--frames N]  — с --frames выходит после N кадров и печатает число ошибок валидации (код 0 — ноль ошибок).
#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "han/platform/window.h"
#include "han/render/renderer.h"

int main(int argc, char** argv) {
    long maxFrames = -1;
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--frames")) maxFrames = std::atol(argv[i + 1]);

    auto window = han::platform::Window::create("HAN: Fists of Seoul — демо E2", 1280, 720);
    if (!window) {
        std::fprintf(stderr, "не удалось создать окно\n");
        return 1;
    }

    han::render::RendererDesc desc;
    desc.instanceExtensions = window->vulkanInstanceExtensions();
    desc.createSurface = [&](void* instance) { return window->createVulkanSurface(instance); };
    window->pixelSize(desc.width, desc.height);

    std::string error;
    auto renderer = han::render::Renderer::create(desc, error);
    if (!renderer) {
        std::fprintf(stderr, "рендерер: %s\n", error.c_str());
        return 1;
    }
    std::printf("GPU: %s; слой валидации: %s\n", renderer->deviceName().c_str(), renderer->validationEnabled() ? "включён" : "НЕ найден");

    han::platform::InputMap input;
    input.bind("quit", 41);  // Esc
    const Uint64 start = SDL_GetTicksNS();
    for (long frame = 0; maxFrames < 0 || frame < maxFrames; ++frame) {
        input.beginFrame(0);
        const auto ev = window->pump(input);
        if (ev.quitRequested || input.pressed("quit")) break;
        int w = 0, h = 0;
        window->pixelSize(w, h);
        renderer->drawFrame(static_cast<float>(static_cast<double>(SDL_GetTicksNS() - start) * 1e-9), w, h);
    }
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    return errors == 0 ? 0 : 1;
}
