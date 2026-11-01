#pragma once
// Наложение поверх сцены (E2): отладочный интерфейс ImGui, позже — HUD. Рендерер вызывает его отдельным проходом после сцены.
// Заголовок не тянет Vulkan: дескрипторы передаются как void*, а наложение приводит их к своим типам.
#include <cstdint>

namespace han::render {

struct VulkanHandles {
    void* instance = nullptr;        // VkInstance
    void* physicalDevice = nullptr;  // VkPhysicalDevice
    void* device = nullptr;          // VkDevice
    void* queue = nullptr;           // VkQueue (графика)
    std::uint32_t queueFamily = 0;
    int colorFormat = 0;             // VkFormat изображений swapchain
    std::uint32_t imageCount = 0;
    std::uint32_t apiVersion = 0;
};

struct IOverlay {
    // Устройство создано; здесь наложение создаёт свои ресурсы Vulkan.
    virtual void onRendererReady(const VulkanHandles& handles) = 0;
    // Запись команд в проход, где цель — изображение swapchain (VkCommandBuffer как void*). Глубины в этом проходе нет.
    virtual void record(void* vkCommandBuffer) = 0;
    // Устройство простаивает и вот-вот будет уничтожено: освободить ресурсы.
    virtual void onRendererShutdown() = 0;
    virtual ~IOverlay() = default;
};

}  // namespace han::render
// Пример: renderer->setOverlay(&imguiLayer);  // дальше record() вызывается каждый кадр
