#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "han/render/generated/mesh_frag.h"
#include "han/render/generated/mesh_vert.h"
#include "han/render/renderer.h"

namespace han::render {
namespace {

constexpr int kFramesInFlight = 2;
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

struct PushConstants {
    float viewProj[16];
    float model[16];
};
static_assert(sizeof(PushConstants) == 128, "push constants: гарантированный минимум Vulkan — 128 байт");

struct DebugCounters {
    std::uint32_t errors = 0, warnings = 0;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto* counters = static_cast<DebugCounters*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++counters->errors;
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ++counters->warnings;
    else return VK_FALSE;
    std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}

bool fail(std::string& err, const char* what, VkResult r) {
    err = std::string(what) + " (VkResult " + std::to_string(static_cast<int>(r)) + ")";
    return false;
}

struct GpuMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE, indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE, indexMemory = VK_NULL_HANDLE;
    std::uint32_t indexCount = 0;
};

}  // namespace

struct Renderer::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    std::string deviceName;
    bool validation = false;
    DebugCounters counters;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> renderFinished;  // по одному на изображение swapchain

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkCommandPool pool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> cmds{};
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::array<VkFence, kFramesInFlight> inFlight{};
    int frame = 0;

    std::vector<GpuMesh> meshes;  // MeshId = индекс + 1
    IOverlay* overlay = nullptr;

    bool createInstance(const RendererDesc& desc, std::string& err);
    bool pickDevice(std::string& err);
    bool createSwapchain(int width, int height, std::string& err);
    bool createDepth(std::string& err);
    void destroySwapchain();
    bool createPipeline(std::string& err);
    bool createFrameObjects(std::string& err);
    bool createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory,
                      std::string& err);
    bool findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags flags, std::uint32_t& index) const;
    void destroyAll();
};

bool Renderer::Impl::createInstance(const RendererDesc& desc, std::string& err) {
    std::uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    validation = desc.validation && std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& l) {
                     return !std::strcmp(l.layerName, kValidationLayer);
                 });

    std::vector<const char*> extensions = desc.instanceExtensions;
    if (validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "HAN: Fists of Seoul";
    app.pEngineName = "HAN Engine";
    app.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg.pfnUserCallback = debugCallback;
    dbg.pUserData = &counters;

    const char* layer = kValidationLayer;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    if (validation) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = &layer;
        ci.pNext = &dbg;  // ловит и ошибки создания инстанса
    }
    const VkResult r = vkCreateInstance(&ci, nullptr, &instance);
    if (r != VK_SUCCESS) return fail(err, "vkCreateInstance", r);
    if (validation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create) create(instance, &dbg, nullptr, &messenger);
    }
    return true;
}

bool Renderer::Impl::pickDevice(std::string& err) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        err = "нет устройств Vulkan";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    int bestScore = -1;
    for (const VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) continue;

        std::uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, exts.data());
        const bool hasSwapchain = std::any_of(exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
            return !std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        });
        if (!hasSwapchain) continue;

        std::uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qs.data());
        for (std::uint32_t i = 0; i < qCount; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
            if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
            if (score > bestScore) {
                bestScore = score;
                physical = d;
                queueFamily = i;
                deviceName = props.deviceName;
            }
            break;
        }
    }
    if (physical == VK_NULL_HANDLE) {
        err = "нет GPU с Vulkan 1.3, swapchain и очередью графики";
        return false;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = queueFamily;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;

    const char* devExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &f13;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &q;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = &devExt;
    const VkResult r = vkCreateDevice(physical, &ci, nullptr, &device);
    if (r != VK_SUCCESS) return fail(err, "vkCreateDevice", r);
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    return true;
}

bool Renderer::Impl::findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags flags, std::uint32_t& index) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) {
            index = i;
            return true;
        }
    }
    return false;
}

bool Renderer::Impl::createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                                  VkDeviceMemory& memory, std::string& err) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(device, &bi, nullptr, &buffer);
    if (r != VK_SUCCESS) return fail(err, "vkCreateBuffer", r);

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ai.memoryTypeIndex)) {
        err = "нет подходящей памяти для буфера";
        return false;
    }
    r = vkAllocateMemory(device, &ai, nullptr, &memory);
    if (r != VK_SUCCESS) return fail(err, "vkAllocateMemory", r);
    vkBindBufferMemory(device, buffer, memory, 0);

    void* mapped = nullptr;
    r = vkMapMemory(device, memory, 0, size, 0, &mapped);
    if (r != VK_SUCCESS) return fail(err, "vkMapMemory", r);
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(device, memory);
    return true;
}

bool Renderer::Impl::createDepth(std::string& err) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kDepthFormat;
    ii.extent = {extent.width, extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device, &ii, nullptr, &depthImage);
    if (r != VK_SUCCESS) return fail(err, "vkCreateImage (глубина)", r);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, depthImage, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ai.memoryTypeIndex)) {
        err = "нет памяти GPU для буфера глубины";
        return false;
    }
    r = vkAllocateMemory(device, &ai, nullptr, &depthMemory);
    if (r != VK_SUCCESS) return fail(err, "vkAllocateMemory (глубина)", r);
    vkBindImageMemory(device, depthImage, depthMemory, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = depthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = kDepthFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    r = vkCreateImageView(device, &vi, nullptr, &depthView);
    return r == VK_SUCCESS ? true : fail(err, "vkCreateImageView (глубина)", r);
}

void Renderer::Impl::destroySwapchain() {
    if (depthView) vkDestroyImageView(device, depthView, nullptr);
    if (depthImage) vkDestroyImage(device, depthImage, nullptr);
    if (depthMemory) vkFreeMemory(device, depthMemory, nullptr);
    depthView = VK_NULL_HANDLE;
    depthImage = VK_NULL_HANDLE;
    depthMemory = VK_NULL_HANDLE;
    for (const VkSemaphore s : renderFinished) vkDestroySemaphore(device, s, nullptr);
    renderFinished.clear();
    for (const VkImageView v : views) vkDestroyImageView(device, v, nullptr);
    views.clear();
    images.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool Renderer::Impl::createSwapchain(int width, int height, std::string& err) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);

    std::uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmtCount, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f;
    format = chosen.format;

    extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // вертикальная синхронизация, поддерживается везде
    ci.clipped = VK_TRUE;
    VkResult r = vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);
    if (r != VK_SUCCESS) return fail(err, "vkCreateSwapchainKHR", r);

    std::uint32_t n = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    images.resize(n);
    vkGetSwapchainImagesKHR(device, swapchain, &n, images.data());

    for (const VkImage img : images) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view;
        r = vkCreateImageView(device, &vi, nullptr, &view);
        if (r != VK_SUCCESS) return fail(err, "vkCreateImageView", r);
        views.push_back(view);

        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore sem;
        r = vkCreateSemaphore(device, &si, nullptr, &sem);
        if (r != VK_SUCCESS) return fail(err, "vkCreateSemaphore", r);
        renderFinished.push_back(sem);
    }
    return createDepth(err);
}

bool Renderer::Impl::createPipeline(std::string& err) {
    auto makeModule = [&](const std::uint32_t* code, std::size_t bytes, VkShaderModule& out) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = bytes;
        ci.pCode = code;
        return vkCreateShaderModule(device, &ci, nullptr, &out);
    };
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkResult r = makeModule(shaders::k_mesh_vert, sizeof(shaders::k_mesh_vert), vert);
    if (r == VK_SUCCESS) r = makeModule(shaders::k_mesh_frag, sizeof(shaders::k_mesh_frag), frag);
    if (r != VK_SUCCESS) {
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        return fail(err, "vkCreateShaderModule", r);
    }

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pcr;
    r = vkCreatePipelineLayout(device, &lci, nullptr, &pipelineLayout);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> attrs{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
    }};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
    vin.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    // Меши обходятся против часовой стрелки; перевёрнутая по Y проекция сохраняет видимую ориентацию (проверено на кадре).
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    const std::array<VkDynamicState, 2> dyn{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = static_cast<std::uint32_t>(dyn.size());
    ds.pDynamicStates = dyn.data();

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    rendering.depthAttachmentFormat = kDepthFormat;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext = &rendering;
    pci.stageCount = 2;
    pci.pStages = stages.data();
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &dss;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &ds;
    pci.layout = pipelineLayout;
    if (r == VK_SUCCESS) r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return r == VK_SUCCESS ? true : fail(err, "создание конвейера", r);
}

bool Renderer::Impl::createFrameObjects(std::string& err) {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily;
    VkResult r = vkCreateCommandPool(device, &pci, nullptr, &pool);
    if (r != VK_SUCCESS) return fail(err, "vkCreateCommandPool", r);

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kFramesInFlight;
    r = vkAllocateCommandBuffers(device, &ai, cmds.data());
    if (r != VK_SUCCESS) return fail(err, "vkAllocateCommandBuffers", r);

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < kFramesInFlight; ++i) {
        if ((r = vkCreateSemaphore(device, &si, nullptr, &imageAvailable[i])) != VK_SUCCESS) return fail(err, "vkCreateSemaphore", r);
        if ((r = vkCreateFence(device, &fi, nullptr, &inFlight[i])) != VK_SUCCESS) return fail(err, "vkCreateFence", r);
    }
    return true;
}

void Renderer::Impl::destroyAll() {
    if (device) {
        vkDeviceWaitIdle(device);
        for (const GpuMesh& m : meshes) {
            if (m.vertexBuffer) vkDestroyBuffer(device, m.vertexBuffer, nullptr);
            if (m.indexBuffer) vkDestroyBuffer(device, m.indexBuffer, nullptr);
            if (m.vertexMemory) vkFreeMemory(device, m.vertexMemory, nullptr);
            if (m.indexMemory) vkFreeMemory(device, m.indexMemory, nullptr);
        }
        for (int i = 0; i < kFramesInFlight; ++i) {
            if (imageAvailable[i]) vkDestroySemaphore(device, imageAvailable[i], nullptr);
            if (inFlight[i]) vkDestroyFence(device, inFlight[i], nullptr);
        }
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        destroySwapchain();
        vkDestroyDevice(device, nullptr);
    }
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (messenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, messenger, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
}

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() {
    if (impl_->overlay && impl_->device) {
        vkDeviceWaitIdle(impl_->device);
        impl_->overlay->onRendererShutdown();
    }
    impl_->destroyAll();
}

void Renderer::setOverlay(IOverlay* overlay) {
    Impl& s = *impl_;
    if (s.overlay && s.device) {
        vkDeviceWaitIdle(s.device);
        s.overlay->onRendererShutdown();
    }
    s.overlay = overlay;
    if (!overlay) return;
    VulkanHandles h;
    h.instance = s.instance;
    h.physicalDevice = s.physical;
    h.device = s.device;
    h.queue = s.queue;
    h.queueFamily = s.queueFamily;
    h.colorFormat = static_cast<int>(s.format);
    h.imageCount = static_cast<std::uint32_t>(s.images.size());
    h.apiVersion = VK_API_VERSION_1_3;
    overlay->onRendererReady(h);
}

std::unique_ptr<Renderer> Renderer::create(const RendererDesc& desc, std::string& error) {
    std::unique_ptr<Renderer> r(new Renderer());
    Impl& s = *r->impl_;
    if (!s.createInstance(desc, error)) return nullptr;
    s.surface = reinterpret_cast<VkSurfaceKHR>(desc.createSurface ? desc.createSurface(s.instance) : 0);
    if (!s.surface) {
        error = "не удалось создать поверхность окна";
        return nullptr;
    }
    if (!s.pickDevice(error) || !s.createSwapchain(desc.width, desc.height, error) || !s.createPipeline(error) ||
        !s.createFrameObjects(error))
        return nullptr;
    return r;
}

MeshId Renderer::uploadMesh(const MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return 0;
    Impl& s = *impl_;
    GpuMesh gpu;
    gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    std::string err;
    const bool ok = s.createBuffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   gpu.vertexBuffer, gpu.vertexMemory, err) &&
                    s.createBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                   gpu.indexBuffer, gpu.indexMemory, err);
    // Частично созданные ресурсы попадают в список и освобождаются при уничтожении рендерера.
    s.meshes.push_back(gpu);
    if (!ok) {
        std::fprintf(stderr, "uploadMesh: %s\n", err.c_str());
        s.meshes.back().indexCount = 0;
        return 0;
    }
    return static_cast<MeshId>(s.meshes.size());
}

bool Renderer::drawFrame(const core::Mat4& viewProjection, std::span<const DrawItem> items, int width, int height) {
    Impl& s = *impl_;
    if (width <= 0 || height <= 0) return false;  // окно свёрнуто

    const int f = s.frame;
    vkWaitForFences(s.device, 1, &s.inFlight[f], VK_TRUE, UINT64_MAX);

    std::uint32_t index = 0;
    VkResult r = vkAcquireNextImageKHR(s.device, s.swapchain, UINT64_MAX, s.imageAvailable[f], VK_NULL_HANDLE, &index);
    std::string err;
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(s.device);
        s.destroySwapchain();
        s.createSwapchain(width, height, err);
        return false;  // кадр пропускаем, следующий нарисуется
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    vkResetFences(s.device, 1, &s.inFlight[f]);

    const VkCommandBuffer cmd = s.cmds[f];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    auto barrier = [&](VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
                       VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {aspect, 0, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    };

    barrier(s.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    // Глубина очищается каждый кадр, поэтому прежнее содержимое не нужно. Предыдущий кадр может ещё читать её на раннем/позднем
    // тесте глубины — ждём обе стадии.
    constexpr VkPipelineStageFlags2 kDepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier(s.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = s.views[index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.35f, 0.5f, 0.7f, 1.0f}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = s.depthView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, s.extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &ri);

    const VkViewport viewport{0, 0, static_cast<float>(s.extent.width), static_cast<float>(s.extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, s.extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipeline);

    PushConstants pc{};
    std::memcpy(pc.viewProj, viewProjection.m, sizeof(pc.viewProj));
    for (const DrawItem& item : items) {
        if (item.mesh == 0 || item.mesh > s.meshes.size()) continue;
        const GpuMesh& mesh = s.meshes[item.mesh - 1];
        if (mesh.indexCount == 0) continue;
        std::memcpy(pc.model, item.model.m, sizeof(pc.model));
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    }
    vkCmdEndRendering(cmd);

    if (s.overlay) {
        // Второй проход поверх сцены без глубины: сначала дожидаемся записи цвета первым проходом.
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cmd, &dep);

        VkRenderingAttachmentInfo overlayColor = color;
        overlayColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        VkRenderingInfo ori{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ori.renderArea = {{0, 0}, s.extent};
        ori.layerCount = 1;
        ori.colorAttachmentCount = 1;
        ori.pColorAttachments = &overlayColor;
        vkCmdBeginRendering(cmd, &ori);
        s.overlay->record(cmd);
        vkCmdEndRendering(cmd);
    }

    barrier(s.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
    vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = s.imageAvailable[f];
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = s.renderFinished[index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cbi;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    if (vkQueueSubmit2(s.queue, 1, &submit, s.inFlight[f]) != VK_SUCCESS) return false;

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &s.renderFinished[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &s.swapchain;
    pi.pImageIndices = &index;
    r = vkQueuePresentKHR(s.queue, &pi);
    s.frame = (f + 1) % kFramesInFlight;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(s.device);
        s.destroySwapchain();
        s.createSwapchain(width, height, err);
    }
    return true;
}

const std::string& Renderer::deviceName() const { return impl_->deviceName; }
bool Renderer::validationEnabled() const { return impl_->validation; }
std::uint32_t Renderer::validationErrors() const { return impl_->counters.errors; }
std::uint32_t Renderer::validationWarnings() const { return impl_->counters.warnings; }

}  // namespace han::render
