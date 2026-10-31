#pragma once
// Рендерер Vulkan 1.3 (E2): инстанс, устройство, swapchain, глубина, меши, dynamic rendering.
// Заголовок не тянет Vulkan и SDL: поверхность окна создаёт колбэк, который даёт платформенный слой.
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "han/render/mesh.h"

namespace han::render {

struct RendererDesc {
    std::vector<const char*> instanceExtensions;  // расширения для поверхности окна (от SDL)
    bool validation = true;                       // слой VK_LAYER_KHRONOS_validation, если установлен
    int width = 1280, height = 720;
    // Создаёт поверхность окна для VkInstance (void*) и возвращает VkSurfaceKHR как uint64; 0 — ошибка.
    std::function<std::uint64_t(void* vkInstance)> createSurface;
};

using MeshId = std::uint32_t;  // 0 — «нет меша»

struct DrawItem {
    MeshId mesh = 0;
    core::Mat4 model;
};

class Renderer {
public:
    // nullptr при ошибке; причина пишется в error.
    static std::unique_ptr<Renderer> create(const RendererDesc& desc, std::string& error);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Загружает меш в память GPU. 0 — ошибка. Меши живут до уничтожения рендерера.
    MeshId uploadMesh(const MeshData& mesh);

    // Рисует кадр. width/height — текущий размер окна в пикселях (для пересоздания swapchain).
    // false — кадр пропущен (окно свёрнуто, пересоздание swapchain) или произошла ошибка устройства.
    bool drawFrame(const core::Mat4& viewProjection, std::span<const DrawItem> items, int width, int height);

    const std::string& deviceName() const;
    bool validationEnabled() const;
    // Сколько сообщений уровня ERROR и WARNING выдал слой валидации (критерий E2: ноль).
    std::uint32_t validationErrors() const;
    std::uint32_t validationWarnings() const;

private:
    Renderer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace han::render
// Пример: auto cube = r->uploadMesh(makeBox({.5f, .5f, .5f}, {1, 0, 0})); r->drawFrame(cam.viewProjection(aspect), items, w, h);
