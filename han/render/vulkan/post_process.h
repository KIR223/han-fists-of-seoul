#pragma once
// Цепочка постобработки (E4, часть 34.4 дизайна): TAA → bloom → тонмаппинг AgX. Внутренний класс модуля han_render.
// Работает по кадру так: сцена в линейном HDR (с подпиксельным джиттером) и её глубина уже нарисованы; PostProcess накапливает историю
// (TAA), считает свечение и пишет итог в изображение swapchain.
#include <vulkan/vulkan.h>

#include <array>
#include <string>

#include "vk_util.h"

namespace han::render {

struct PostParams {
    float exposure = 1.0f;
    float contrast = 1.2f, saturation = 1.2f;  // «взгляд» AgX
    float bloomIntensity = 0.08f;  // доля свечения, добавляемая к сцене (0 — выключено)
    float bloomThreshold = 1.0f;   // яркость, с которой начинается свечение
    float bloomKnee = 0.5f;        // мягкость порога
    float bloomRadius = 1.0f;      // ширина ореола (множитель шага фильтра при подъёме)

    bool taa = false;              // временное сглаживание: сцена должна быть нарисована с джиттером
    bool historyValid = false;     // false — истории нет (первый кадр, смена размера, склейка камеры): кадр берётся как есть
    float reproject[16] = {};      // клип этого кадра → клип прошлого кадра (оба без джиттера), column-major
    float taaFeedback = 0.9f;      // доля истории в результате
};

class PostProcess {
public:
    struct Context {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;       // общий пул рендерера (с флагом освобождения наборов)
        VkDescriptorSetLayout samplerLayout = VK_NULL_HANDLE;   // один combined sampler, фрагментный шейдер
        VkSampler sampler = VK_NULL_HANDLE;                     // линейный, без повтора
    };

    // Раскладки и конвейеры; не зависит от размера окна.
    bool init(const Context& context, VkFormat swapchainFormat, std::string& err);
    // Цели и наборы дескрипторов под текущий размер окна (вызывать после каждого пересоздания swapchain).
    // depthView — глубина сцены; перед record() при включённом TAA она должна быть в DEPTH_STENCIL_READ_ONLY_OPTIMAL.
    bool resize(VkExtent2D extent, VkImage hdrImage, VkImageView hdrView, VkImageView depthView, std::string& err);
    void releaseTargets();
    void destroy();

    // Записывает TAA (если включён), bloom и тонмаппинг. Вход: изображение сцены в раскладке COLOR_ATTACHMENT_OPTIMAL (после прохода сцены).
    // swapchainView должен быть в COLOR_ATTACHMENT_OPTIMAL; по окончании он остаётся таким же (поверх рисуется наложение).
    void record(VkCommandBuffer cmd, VkImageView swapchainView, VkExtent2D extent, const PostParams& params);

private:
    Context ctx_;

    VkDescriptorSetLayout tonemapSetLayout_ = VK_NULL_HANDLE;  // две картинки: сцена и bloom
    VkDescriptorSetLayout taaSetLayout_ = VK_NULL_HANDLE;      // три картинки: сцена, история, глубина
    VkPipelineLayout bloomLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout tonemapLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout taaLayout_ = VK_NULL_HANDLE;
    VkPipeline bloomDown_ = VK_NULL_HANDLE;
    VkPipeline bloomUp_ = VK_NULL_HANDLE;
    VkPipeline tonemap_ = VK_NULL_HANDLE;
    VkPipeline taa_ = VK_NULL_HANDLE;
    VkSampler nearestSampler_ = VK_NULL_HANDLE;  // для глубины: линейная фильтрация глубины не гарантируется

    static constexpr std::uint32_t kMaxBloomMips = 6;
    VkImage hdrImage_ = VK_NULL_HANDLE;
    std::array<vk::Image, 2> resolve_{};            // история TAA: пишем в resolve_[taaCur_], историей служит другой
    std::array<VkDescriptorSet, 2> taaSet_{};       // taaSet_[k]: пишем в resolve_[k], история — resolve_[1 − k]
    int taaCur_ = 0;
    bool needsInit_ = false;                        // изображения истории ещё в раскладке UNDEFINED
    // Источник сцены для bloom и тонмаппинга: 0 — сырая сцена (без TAA), 1 и 2 — результат TAA в resolve_[0] и resolve_[1].
    std::array<VkDescriptorSet, 3> sceneSet_{};
    std::array<VkDescriptorSet, 3> tonemapSet_{};

    vk::Image bloom_;
    std::uint32_t bloomMips_ = 0;
    std::array<VkExtent2D, kMaxBloomMips> mipExtent_{};
    std::array<VkImageView, kMaxBloomMips> mipView_{};
    std::array<VkDescriptorSet, kMaxBloomMips> mipSet_{};
};

}  // namespace han::render
