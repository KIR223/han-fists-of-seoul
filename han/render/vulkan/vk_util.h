#pragma once
// Общие помощники Vulkan для рендерера и постобработки (E4): ошибки, память, образы, графические конвейеры.
// Внутренний заголовок модуля han_render: наружу (в han/render/include) не выходит.
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace han::render::vk {

// Записывает в err «что (VkResult N)» и возвращает false — для удобных цепочек `return fail(...)`.
bool fail(std::string& err, const char* what, VkResult r);

// Подходящий тип памяти по битовой маске требований и свойствам.
bool findMemoryType(VkPhysicalDevice physical, std::uint32_t typeBits, VkMemoryPropertyFlags flags, std::uint32_t& index);

// Двумерное изображение в памяти GPU с видом на все уровни и слои (для нескольких слоёв — вид массива).
struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};
bool createImage2D(VkDevice device, VkPhysicalDevice physical, std::uint32_t width, std::uint32_t height, std::uint32_t mipLevels,
                   VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, Image& out, std::string& err);
void destroyImage(VkDevice device, Image& image);
// Вид на один уровень мипов изображения (для записи в конкретный уровень или чтения из него).
bool createMipView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect, std::uint32_t mipLevel, VkImageView& out,
                   std::string& err);

// Описание графического конвейера: всё, чем конвейеры сцены, неба, теней и постобработки отличаются друг от друга.
struct PipelineDesc {
    const std::uint32_t* vert = nullptr;
    std::size_t vertBytes = 0;
    const std::uint32_t* frag = nullptr;  // nullptr — только вершинный шейдер (карта теней)
    std::size_t fragBytes = 0;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    bool vertexInput = false;   // вершины из буфера (Vertex) или генерируются в шейдере
    bool positionOnly = false;  // из буфера читается только позиция (теневой проход): остальные атрибуты шейдер не потребляет
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;  // UNDEFINED — без цветового вложения
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;  // UNDEFINED — без глубины
    bool depthTest = false, depthWrite = false;
    VkCompareOp depthOp = VK_COMPARE_OP_LESS;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    bool depthBias = false;      // динамическое смещение глубины (карта теней)
    bool additiveBlend = false;  // результат прибавляется к тому, что уже в цели (подъём уровней bloom)
};

VkResult buildPipeline(VkDevice device, const PipelineDesc& desc, VkPipeline& out);

}  // namespace han::render::vk
