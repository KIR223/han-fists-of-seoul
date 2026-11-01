#include <SDL3/SDL.h>
#include <cstdio>
#include <SDL3/SDL_vulkan.h>

#include "han/platform/window.h"

namespace han::platform {

struct Window::Impl {
    SDL_Window* window = nullptr;
    EventHook hook;
};

Window::Window() : impl_(std::make_unique<Impl>()) {}

Window::~Window() {
    if (impl_->window) SDL_DestroyWindow(impl_->window);
    SDL_Quit();
}

std::unique_ptr<Window> Window::create(const char* title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return nullptr;
    }
    std::unique_ptr<Window> w(new Window());
    w->impl_->window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!w->impl_->window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return nullptr;  // деструктор вызовет SDL_Quit
    }
    return w;
}

FrameEvents Window::pump(InputMap& input) {
    FrameEvents out;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (impl_->hook) impl_->hook(&e);
        switch (e.type) {
            case SDL_EVENT_QUIT: out.quitRequested = true; break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: input.onKey(static_cast<int>(e.key.scancode), e.key.down); break;
            case SDL_EVENT_MOUSE_MOTION:
                out.mouseDx += e.motion.xrel;
                out.mouseDy += e.motion.yrel;
                break;
            default: break;
        }
    }
    SDL_GetWindowSize(impl_->window, &out.width, &out.height);
    return out;
}

void Window::setEventHook(EventHook hook) { impl_->hook = std::move(hook); }

void* Window::nativeHandle() const { return impl_->window; }

void Window::setCaptureMouse(bool capture) { SDL_SetWindowRelativeMouseMode(impl_->window, capture); }

bool Window::mouseButtonHeld(int button) const {
    if (button < 1 || button > 5) return false;
    return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(button)) != 0;
}

std::vector<const char*> Window::vulkanInstanceExtensions() const {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    return names ? std::vector<const char*>(names, names + count) : std::vector<const char*>{};
}

std::uint64_t Window::createVulkanSurface(void* vkInstance) {
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(impl_->window, static_cast<VkInstance>(vkInstance), nullptr, &surface)) return 0;
    return reinterpret_cast<std::uint64_t>(surface);
}

void Window::pixelSize(int& width, int& height) const { SDL_GetWindowSizeInPixels(impl_->window, &width, &height); }

}  // namespace han::platform
