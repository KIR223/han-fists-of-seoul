// Демо E2: сцена из земли и кубов, летающая камера, глубина, освещение, слой валидации.
// Управление: мышь — взгляд, W/A/S/D — движение, Space/Ctrl — вверх/вниз, Shift — быстрее, Tab — захват мыши, Esc — выход.
// Запуск: han_scene_demo.exe [--frames N] — с --frames выходит после N кадров; код 0 — ноль ошибок валидации.
#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "han/platform/window.h"
#include "han/render/camera.h"
#include "han/render/renderer.h"

namespace {
// Коды SDL_Scancode.
constexpr int kW = 26, kA = 4, kS = 22, kD = 7, kSpace = 44, kLCtrl = 224, kLShift = 225, kTab = 43, kEsc = 41;
}  // namespace

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

    using han::core::Mat4;
    using han::core::Vec3;
    const auto ground = renderer->uploadMesh(han::render::makePlane(40.f, {0.35f, 0.4f, 0.35f}));
    const auto cube = renderer->uploadMesh(han::render::makeBox({0.5f, 0.5f, 0.5f}, {0.85f, 0.4f, 0.3f}));
    const auto pillar = renderer->uploadMesh(han::render::makeBox({0.4f, 2.f, 0.4f}, {0.7f, 0.7f, 0.75f}));
    if (!ground || !cube || !pillar) {
        std::fprintf(stderr, "не удалось загрузить меши\n");
        return 1;
    }

    han::platform::InputMap input;
    input.bind("forward", kW);
    input.bind("back", kS);
    input.bind("left", kA);
    input.bind("right", kD);
    input.bind("up", kSpace);
    input.bind("down", kLCtrl);
    input.bind("fast", kLShift);
    input.bind("capture", kTab);
    input.bind("quit", kEsc);

    han::render::Camera camera;
    camera.position = {0, 3, 10};
    camera.pitch = -0.2f;
    bool captured = true;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
    window->setCaptureMouse(captured);

    const Uint64 start = SDL_GetTicksNS();
    Uint64 last = start;
    for (long frame = 0; maxFrames < 0 || frame < maxFrames; ++frame) {
        const Uint64 now = SDL_GetTicksNS();
        const float dt = static_cast<float>(static_cast<double>(now - last) * 1e-9);
        const float t = static_cast<float>(static_cast<double>(now - start) * 1e-9);
        last = now;

        input.beginFrame(static_cast<double>(now) * 1e-9);
        const auto ev = window->pump(input);
        if (ev.quitRequested || input.pressed("quit")) break;
        if (input.pressed("capture")) window->setCaptureMouse(captured = !captured);
        if (captured) camera.look(ev.mouseDx, ev.mouseDy);
        const auto axis = [&](const char* pos, const char* neg) { return (input.held(pos) ? 1.f : 0.f) - (input.held(neg) ? 1.f : 0.f); };
        camera.move(axis("forward", "back"), axis("right", "left"), axis("up", "down"), input.held("fast") ? 20.f : 6.f, dt);

        std::vector<han::render::DrawItem> items;
        items.push_back({ground, Mat4::identity()});
        for (int x = -3; x <= 3; ++x)
            for (int z = -3; z <= 3; ++z) {
                if (x == 0 && z == 0) continue;
                const float spin = (x + z) % 2 == 0 ? t : 0.f;
                items.push_back({cube, Mat4::translation({x * 4.f, 0.5f, z * 4.f - 6.f}) * Mat4::rotationY(spin)});
            }
        items.push_back({pillar, Mat4::translation({0, 2.f, -6.f})});

        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        renderer->drawFrame(camera.viewProjection(aspect), items, w, h);
    }
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    return errors == 0 ? 0 : 1;
}
