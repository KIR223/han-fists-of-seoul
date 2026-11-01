#include "vk_util.h"

#include <array>
#include <cstddef>

#include "han/render/mesh.h"

namespace han::render::vk {

bool fail(std::string& err, const char* what, VkResult r) {
    err = std::string(what) + " (VkResult " + std::to_string(static_cast<int>(r)) + ")";
    return false;
}


bool findMemoryType(VkPhysicalDevice physical, std::uint32_t typeBits, VkMemoryPropertyFlags flags, std::uint32_t& index) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            index = i;
            return true;
        }
    }
    return false;
}

bool createImage2D(VkDevice device, VkPhysicalDevice physical, std::uint32_t width, std::uint32_t height, std::uint32_t mipLevels,
                   VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, Image& out, std::string& err) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {width, height, 1};
    ii.mipLevels = mipLevels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device, &ii, nullptr, &out.image);
    if (r != VK_SUCCESS) return fail(err, "vkCreateImage", r);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, out.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    if (!findMemoryType(physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ai.memoryTypeIndex)) {
        err = "нет памяти GPU для изображения";
        return false;
    }
    r = vkAllocateMemory(device, &ai, nullptr, &out.memory);
    if (r != VK_SUCCESS) return fail(err, "vkAllocateMemory", r);
    vkBindImageMemory(device, out.image, out.memory, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, mipLevels, 0, 1};
    r = vkCreateImageView(device, &vi, nullptr, &out.view);
    return r == VK_SUCCESS ? true : fail(err, "vkCreateImageView", r);
}

void destroyImage(VkDevice device, Image& image) {
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = Image{};
}

bool createMipView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect, std::uint32_t mipLevel, VkImageView& out,
                   std::string& err) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, mipLevel, 1, 0, 1};
    const VkResult r = vkCreateImageView(device, &vi, nullptr, &out);
    return r == VK_SUCCESS ? true : fail(err, "vkCreateImageView (уровень)", r);
}

VkResult buildPipeline(VkDevice device, const PipelineDesc& d, VkPipeline& out) {
    auto makeModule = [&](const std::uint32_t* code, std::size_t bytes, VkShaderModule& module) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = bytes;
        ci.pCode = code;
        return vkCreateShaderModule(device, &ci, nullptr, &module);
    };
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    const bool hasFragment = d.frag != nullptr;  // теневой конвейер пишет только глубину
    VkResult r = makeModule(d.vert, d.vertBytes, vert);
    if (r == VK_SUCCESS && hasFragment) r = makeModule(d.frag, d.fragBytes, frag);
    if (r != VK_SUCCESS) {
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        return r;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    if (hasFragment)
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 4> attrs{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    }};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (d.vertexInput) {
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = d.positionOnly ? 1 : static_cast<std::uint32_t>(attrs.size());
        vin.pVertexAttributeDescriptions = attrs.data();
    }

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.cull;
    // Меши обходятся против часовой стрелки; перевёрнутая по Y проекция сохраняет видимую ориентацию (проверено на кадре).
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = d.depthBias ? VK_TRUE : VK_FALSE;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = d.depthTest ? VK_TRUE : VK_FALSE;
    dss.depthWriteEnable = d.depthWrite ? VK_TRUE : VK_FALSE;
    dss.depthCompareOp = d.depthOp;
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (d.additiveBlend) {
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        att.colorBlendOp = VK_BLEND_OP_ADD;
        att.srcAlphaBlendFactor = att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    const bool hasColor = d.colorFormat != VK_FORMAT_UNDEFINED;
    cb.attachmentCount = hasColor ? 1 : 0;
    cb.pAttachments = hasColor ? &att : nullptr;
    std::array<VkDynamicState, 3> dyn{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = d.depthBias ? 3 : 2;
    ds.pDynamicStates = dyn.data();

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = hasColor ? 1 : 0;
    rendering.pColorAttachmentFormats = hasColor ? &d.colorFormat : nullptr;
    rendering.depthAttachmentFormat = d.depthFormat;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext = &rendering;
    pci.stageCount = hasFragment ? 2 : 1;
    pci.pStages = stages.data();
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &dss;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &ds;
    pci.layout = d.layout;
    r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &out);

    vkDestroyShaderModule(device, vert, nullptr);
    if (frag) vkDestroyShaderModule(device, frag, nullptr);
    return r;
}

}  // namespace han::render::vk
