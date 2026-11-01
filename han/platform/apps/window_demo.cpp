// Демо E1: окно, ввод по действиям, захват мыши (Tab), выход (Esc). Запуск: build/<пресет>/bin/<конфигурация>/han_window_demo.exe
#include <SDL3/SDL_timer.h>

#include <cstdio>

#include "han/core/fixed_step.h"
#include "han/platform/window.h"

namespace {
constexpr int kScancodeEscape = 41;  // SDL_SCANCODE_ESCAPE
constexpr int kScancodeTab = 43;     // SDL_SCANCODE_TAB
constexpr int kScancodeSpace = 44;   // SDL_SCANCODE_SPACE
}  // namespace

int main() {
    auto window = han::platform::Window::create("HAN: Fists of Seoul — демо E1", 1280, 720);
    if (!window) {
        std::fprintf(stderr, "не удалось создать окно\n");
        return 1;
    }
    han::platform::InputMap input;
    input.bind("quit", kScancodeEscape);
    input.bind("toggle_mouse", kScancodeTab);
    input.bind("jump", kScancodeSpace);

    han::core::FixedStepClock clock(1.0 / 60.0);
    bool captured = false;
    Uint64 last = SDL_GetTicksNS();
    for (;;) {
        const Uint64 now = SDL_GetTicksNS();
        const double dt = static_cast<double>(now - last) * 1e-9;
        last = now;

        input.beginFrame(static_cast<double>(now) * 1e-9);
        const auto ev = window->pump(input);
        if (ev.quitRequested || input.pressed("quit")) break;
        if (input.pressed("toggle_mouse")) window->setCaptureMouse(captured = !captured);
        if (input.pressed("jump")) std::printf("прыжок\n");
        if (ev.mouseDx != 0 || ev.mouseDy != 0) std::printf("мышь %.1f %.1f\n", ev.mouseDx, ev.mouseDy);

        for (int i = clock.advance(dt); i > 0; --i) { /* шаг симуляции */ }
        SDL_Delay(1);
    }
    return 0;
}
