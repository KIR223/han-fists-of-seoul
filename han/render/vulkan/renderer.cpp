#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "han/render/generated/mesh_frag.h"
#include "han/render/generated/mesh_vert.h"
#include "han/render/generated/shadow_vert.h"
#include "han/render/generated/sky_frag.h"
#include "han/render/generated/sky_vert.h"
#include "han/render/renderer.h"
#include "han/render/png_io.h"
#include "han/render/taa.h"
#include "post_process.h"
#include "vk_util.h"

namespace han::render {
namespace {

using vk::buildPipeline;
using vk::fail;
using vk::PipelineDesc;

constexpr int kFramesInFlight = 2;
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // сцена рисуется в линейном HDR, затем тонмаппинг в экран

// Push constants мешей: матрица модели (vertex, 0..64) и материал (fragment, 64..80): x — металличность, y — шероховатость, z — 1: есть карта нормалей, w — 1: есть карта шероховатости.
struct VertexPush {
    float model[16];
};
struct FragmentPush {
    float material[4];
};
constexpr std::uint32_t kFragmentPushOffset = sizeof(VertexPush);

// Данные кадра для шейдеров (std140). Зеркалит блок Frame в shaders/common.glsl; менять только вместе.
struct FrameUbo {
    float viewProj[16];
    float invViewProj[16];
    float cameraPos[4];
    float sunDir[4];
    float sunColor[4];   // rgb — цвет, a — освещённость
    float skyZenith[4];
    float skyHorizon[4];
    float skyGround[4];
    float params[4];     // x — экспозиция, y — плотность воздушной дымки, z — усиление неона
    float lightViewProj[3][16];  // каскады теней: мир → клип карты теней
    float cascadeSplits[4];      // xyz — дальние границы каскадов (м), w — 1, если тени включены
    float cascadeTexel[4];       // xyz — размер текселя карты каждого каскада (м)
    float shadowParams[4];       // x — 1 / размер карты, y — 1: подкрасить каскады
    float lightPos[32][4];       // точечные источники: xyz — позиция, w — радиус действия (м)
    float lightColor[32][4];     // rgb — цвет, умноженный на яркость
    float lightInfo[4];          // x — сколько источников, y — влажность поверхностей 0..1
};
static_assert(sizeof(FrameUbo) == 1520, "FrameUbo должен совпадать с std140-блоком Frame");

// Push constants теневого прохода (vertex): матрица модели и номер каскада.
struct ShadowPush {
    float model[16];
    std::uint32_t cascade;
};

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

struct GpuSubmesh {
    std::uint32_t firstIndex = 0, indexCount = 0;
    TextureId texture = 0;  // 0 — белая текстура
    float metallic = 0.0f, roughness = 0.5f;
    TextureId normalTexture = 0, roughnessTexture = 0;  // 0 — нет карты
};


// Пирамида видимости из матрицы вид-проекция (Vulkan: z клипа 0..1, столбцы). Плоскости смотрят внутрь: точка внутри, если dot(n, p) + d >= 0.
struct Frustum {
    float p[6][4];
    int count = 0;
    static Frustum from(const float* m, bool useNear) {
        Frustum f;
        const auto row = [&](int i, int k) { return m[k * 4 + i]; };  // строка i, столбец k
        const float r[4][4] = {{row(0, 0), row(0, 1), row(0, 2), row(0, 3)}, {row(1, 0), row(1, 1), row(1, 2), row(1, 3)},
                               {row(2, 0), row(2, 1), row(2, 2), row(2, 3)}, {row(3, 0), row(3, 1), row(3, 2), row(3, 3)}};
        const auto add = [&](float sign, int a) {
            for (int k = 0; k < 4; ++k) f.p[f.count][k] = r[3][k] + sign * r[a][k];
            ++f.count;
        };
        add(+1.f, 0);  // левая
        add(-1.f, 0);  // правая
        add(+1.f, 1);  // верх/низ (знак зависит от оси Y, обе плоскости нужны)
        add(-1.f, 1);
        if (useNear) {
            for (int k = 0; k < 4; ++k) f.p[f.count][k] = r[2][k];  // ближняя: z >= 0
            ++f.count;
        }
        for (int k = 0; k < 4; ++k) f.p[f.count][k] = r[3][k] - r[2][k];  // дальняя
        ++f.count;
        return f;
    }
    // false — коробка целиком за одной из плоскостей.
    bool sees(const float* model, const float bmin[3], const float bmax[3]) const {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (int c = 0; c < 8; ++c) {
            const float x = (c & 1) ? bmax[0] : bmin[0], y = (c & 2) ? bmax[1] : bmin[1], z = (c & 4) ? bmax[2] : bmin[2];
            for (int a = 0; a < 3; ++a) {
                const float v = model[a] * x + model[4 + a] * y + model[8 + a] * z + model[12 + a];
                lo[a] = std::min(lo[a], v);
                hi[a] = std::max(hi[a], v);
            }
        }
        for (int i = 0; i < count; ++i) {
            const float* q = p[i];
            const float px = q[0] >= 0.f ? hi[0] : lo[0], py = q[1] >= 0.f ? hi[1] : lo[1], pz = q[2] >= 0.f ? hi[2] : lo[2];
            if (q[0] * px + q[1] * py + q[2] * pz + q[3] < 0.f) return false;
        }
        return true;
    }
};

struct GpuMesh {
    float boundsMin[3] = {0, 0, 0}, boundsMax[3] = {0, 0, 0};
    VkBuffer vertexBuffer = VK_NULL_HANDLE, indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE, indexMemory = VK_NULL_HANDLE;
    std::uint32_t indexCount = 0;
    std::vector<GpuSubmesh> submeshes;
};

struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
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

    // HDR-цель сцены (размер окна, пересоздаётся вместе с swapchain); читает её постобработка (bloom, тонмаппинг).
    VkImage hdrImage = VK_NULL_HANDLE;
    VkDeviceMemory hdrMemory = VK_NULL_HANDLE;
    VkImageView hdrView = VK_NULL_HANDLE;
    PostProcess post;
    bool postReady = false;
    // Состояние TAA: счётчик кадров для джиттера, матрица камеры прошлого кадра (без джиттера), пригодна ли история.
    std::uint32_t frameCounter = 0;
    core::Mat4 prevViewProjection;
    bool historyValid = false;
    bool prevTaa = false;

    // Снимок экрана: путь ждёт следующего кадра; canCapture — swapchain создан с TRANSFER_SRC.
    bool canCapture = false;
    std::filesystem::path pendingShot;
    std::string shotError;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;  // меши и небо: set 0 — текстура, set 1 — данные кадра
    VkPipeline pipeline = VK_NULL_HANDLE;              // меши
    VkPipeline skyPipeline = VK_NULL_HANDLE;

    // Данные кадра: по одному постоянно отображённому буферу на кадр в полёте.
    VkDescriptorSetLayout frameLayout = VK_NULL_HANDLE;
    std::array<VkBuffer, kFramesInFlight> frameBuffers{};
    std::array<VkDeviceMemory, kFramesInFlight> frameMemory{};
    std::array<void*, kFramesInFlight> frameMapped{};
    std::array<VkDescriptorSet, kFramesInFlight> frameSets{};
    VkSampler clampSampler = VK_NULL_HANDLE;  // для чтения HDR: линейная фильтрация без повтора

    // Каскадные карты теней: одно изображение-массив из kShadowCascades слоёв, по виду на слой для записи и общий вид для чтения.
    int shadowMapSize = 2048;
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
    VkImageView shadowArrayView = VK_NULL_HANDLE;
    std::array<VkImageView, kShadowCascades> shadowLayerViews{};
    VkSampler shadowSampler = VK_NULL_HANDLE;  // сравнивающий: аппаратный PCF
    VkPipelineLayout shadowLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    VkCommandPool pool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> cmds{};
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::array<VkFence, kFramesInFlight> inFlight{};
    int frame = 0;

    std::vector<GpuMesh> meshes;  // MeshId = индекс + 1
    IOverlay* overlay = nullptr;

    // Текстуры: общий сэмплер, один набор дескрипторов на текстуру. TextureId = индекс + 1; освобождённые слоты пустые.
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::vector<GpuTexture> textures;
    TextureId whiteTexture = 0;
    bool anisotropySupported = false;
    float maxAnisotropy = 1.0f;

    bool createInstance(const RendererDesc& desc, std::string& err);
    bool pickDevice(std::string& err);
    bool createSwapchain(int width, int height, std::string& err);
    bool createDepth(std::string& err);
    bool createHdr(std::string& err);
    void destroySwapchain();
    bool createPipeline(std::string& err);
    bool createFrameData(std::string& err);
    bool createShadowMap(std::string& err);
    bool createFrameObjects(std::string& err);
    bool createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory,
                      std::string& err);
    bool findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags flags, std::uint32_t& index) const;
    bool createMesh(const MeshData& mesh, std::span<const TextureId> textureIds, GpuMesh& out, std::string& err);
    void destroyMesh(GpuMesh& m);
    bool createTextureObjects(std::string& err);
    bool createTexture(const TextureData& data, GpuTexture& out, std::string& err);
    void destroyTexture(GpuTexture& t);
    bool submitOneTime(const std::function<void(VkCommandBuffer)>& record, std::string& err);
    VkDescriptorSet descriptorFor(TextureId id) const;
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

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical, &supported);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    anisotropySupported = supported.samplerAnisotropy == VK_TRUE;
    maxAnisotropy = std::min(16.0f, props.limits.maxSamplerAnisotropy);
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.features.samplerAnisotropy = anisotropySupported ? VK_TRUE : VK_FALSE;
    f2.pNext = &f13;

    const char* devExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &f2;
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

    if (!data) return true;  // буфер под результат (например, чтение кадра): содержимое не нужно
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
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;  // SAMPLED: глубину читает TAA
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

bool Renderer::Impl::createHdr(std::string& err) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kHdrFormat;
    ii.extent = {extent.width, extent.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device, &ii, nullptr, &hdrImage);
    if (r != VK_SUCCESS) return fail(err, "vkCreateImage (HDR)", r);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, hdrImage, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ai.memoryTypeIndex)) {
        err = "нет памяти GPU для HDR-цели";
        return false;
    }
    r = vkAllocateMemory(device, &ai, nullptr, &hdrMemory);
    if (r != VK_SUCCESS) return fail(err, "vkAllocateMemory (HDR)", r);
    vkBindImageMemory(device, hdrImage, hdrMemory, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = hdrImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = kHdrFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r = vkCreateImageView(device, &vi, nullptr, &hdrView);
    if (r != VK_SUCCESS) return fail(err, "vkCreateImageView (HDR)", r);

    if (!postReady) {
        PostProcess::Context context;
        context.device = device;
        context.physical = physical;
        context.descriptorPool = descriptorPool;
        context.samplerLayout = setLayout;
        context.sampler = clampSampler;
        if (!post.init(context, format, err)) return false;
        postReady = true;
    }
    historyValid = false;  // размер изменился: накопленная история непригодна
    return post.resize(extent, hdrImage, hdrView, depthView, err);
}

void Renderer::Impl::destroySwapchain() {
    if (postReady) post.releaseTargets();
    if (hdrView) vkDestroyImageView(device, hdrView, nullptr);
    if (hdrImage) vkDestroyImage(device, hdrImage, nullptr);
    if (hdrMemory) vkFreeMemory(device, hdrMemory, nullptr);
    hdrView = VK_NULL_HANDLE;
    hdrImage = VK_NULL_HANDLE;
    hdrMemory = VK_NULL_HANDLE;
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

    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {  // окно свёрнуто: Windows сообщает поверхность 0x0
        err = "окно свёрнуто: размер поверхности нулевой";
        return false;
    }
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
    canCapture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (canCapture ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
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
    return createDepth(err) && createHdr(err);
}

bool Renderer::Impl::createPipeline(std::string& err) {
    // Меши и небо: set 0 — текстура материала, set 1 — данные кадра; push constants — матрица модели и материал.
    // set 2 — карта нормалей, set 3 — карта шероховатости (тот же layout: одна комбинированная текстура).
    const std::array<VkDescriptorSetLayout, 4> sceneSets{setLayout, frameLayout, setLayout, setLayout};
    const std::array<VkPushConstantRange, 2> scenePush{{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VertexPush)},
        {VK_SHADER_STAGE_FRAGMENT_BIT, kFragmentPushOffset, sizeof(FragmentPush)},
    }};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = static_cast<std::uint32_t>(sceneSets.size());
    lci.pSetLayouts = sceneSets.data();
    lci.pushConstantRangeCount = static_cast<std::uint32_t>(scenePush.size());
    lci.pPushConstantRanges = scenePush.data();
    VkResult r = vkCreatePipelineLayout(device, &lci, nullptr, &pipelineLayout);
    if (r != VK_SUCCESS) return fail(err, "vkCreatePipelineLayout (сцена)", r);

    PipelineDesc mesh;
    mesh.vert = shaders::k_mesh_vert;
    mesh.vertBytes = sizeof(shaders::k_mesh_vert);
    mesh.frag = shaders::k_mesh_frag;
    mesh.fragBytes = sizeof(shaders::k_mesh_frag);
    mesh.layout = pipelineLayout;
    mesh.vertexInput = true;
    mesh.colorFormat = kHdrFormat;
    mesh.depthFormat = kDepthFormat;
    mesh.depthTest = mesh.depthWrite = true;
    mesh.depthOp = VK_COMPARE_OP_LESS;
    mesh.cull = VK_CULL_MODE_BACK_BIT;
    r = buildPipeline(device, mesh, pipeline);
    if (r != VK_SUCCESS) return fail(err, "конвейер мешей", r);

    // Тени: тот же набор дескрипторов (set 1 — данные кадра), push constants — модель и номер каскада; только вершинный шейдер.
    const VkPushConstantRange shadowPush{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush)};
    VkPipelineLayoutCreateInfo sli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    sli.setLayoutCount = static_cast<std::uint32_t>(sceneSets.size());
    sli.pSetLayouts = sceneSets.data();
    sli.pushConstantRangeCount = 1;
    sli.pPushConstantRanges = &shadowPush;
    r = vkCreatePipelineLayout(device, &sli, nullptr, &shadowLayout);
    if (r != VK_SUCCESS) return fail(err, "vkCreatePipelineLayout (тени)", r);

    PipelineDesc shadow;
    shadow.vert = shaders::k_shadow_vert;
    shadow.vertBytes = sizeof(shaders::k_shadow_vert);
    shadow.layout = shadowLayout;
    shadow.vertexInput = true;
    shadow.positionOnly = true;
    shadow.depthFormat = kDepthFormat;
    shadow.depthTest = shadow.depthWrite = true;
    shadow.depthOp = VK_COMPARE_OP_LESS;
    shadow.cull = VK_CULL_MODE_BACK_BIT;
    shadow.depthBias = true;
    r = buildPipeline(device, shadow, shadowPipeline);
    if (r != VK_SUCCESS) return fail(err, "конвейер теней", r);

    PipelineDesc sky;  // небо — на дальней плоскости, поэтому «меньше или равно» и без записи глубины
    sky.vert = shaders::k_sky_vert;
    sky.vertBytes = sizeof(shaders::k_sky_vert);
    sky.frag = shaders::k_sky_frag;
    sky.fragBytes = sizeof(shaders::k_sky_frag);
    sky.layout = pipelineLayout;
    sky.colorFormat = kHdrFormat;
    sky.depthFormat = kDepthFormat;
    sky.depthTest = true;
    sky.depthWrite = false;
    sky.depthOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    r = buildPipeline(device, sky, skyPipeline);
    if (r != VK_SUCCESS) return fail(err, "конвейер неба", r);

    return true;
}

// Карта теней: массив глубины kShadowCascades слоёв размером shadowMapSize; сэмплер со сравнением и белой рамкой (вне карты — свет).
bool Renderer::Impl::createShadowMap(std::string& err) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kDepthFormat;
    ii.extent = {static_cast<std::uint32_t>(shadowMapSize), static_cast<std::uint32_t>(shadowMapSize), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = kShadowCascades;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device, &ii, nullptr, &shadowImage);
    if (r != VK_SUCCESS) return fail(err, "vkCreateImage (тени)", r);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, shadowImage, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ai.memoryTypeIndex)) {
        err = "нет памяти GPU для карт теней";
        return false;
    }
    r = vkAllocateMemory(device, &ai, nullptr, &shadowMemory);
    if (r != VK_SUCCESS) return fail(err, "vkAllocateMemory (тени)", r);
    vkBindImageMemory(device, shadowImage, shadowMemory, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = shadowImage;
    vi.format = kDepthFormat;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
    r = vkCreateImageView(device, &vi, nullptr, &shadowArrayView);
    if (r != VK_SUCCESS) return fail(err, "vkCreateImageView (тени, массив)", r);
    for (std::uint32_t layer = 0; layer < kShadowCascades; ++layer) {
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer, 1};
        r = vkCreateImageView(device, &vi, nullptr, &shadowLayerViews[layer]);
        if (r != VK_SUCCESS) return fail(err, "vkCreateImageView (тени, слой)", r);
    }

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // за пределами карты глубина 1 — освещено
    sci.compareEnable = VK_TRUE;
    sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;  // результат 1, если глубина точки не больше записанной (то есть точка не в тени)
    r = vkCreateSampler(device, &sci, nullptr, &shadowSampler);
    return r == VK_SUCCESS ? true : fail(err, "vkCreateSampler (тени)", r);
}

// Буферы и наборы дескрипторов данных кадра (UBO, постоянно отображённый, по одному на кадр в полёте).
bool Renderer::Impl::createFrameData(std::string& err) {
    for (int i = 0; i < kFramesInFlight; ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(FrameUbo);
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult r = vkCreateBuffer(device, &bi, nullptr, &frameBuffers[i]);
        if (r != VK_SUCCESS) return fail(err, "vkCreateBuffer (данные кадра)", r);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, frameBuffers[i], &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ai.memoryTypeIndex)) {
            err = "нет памяти для данных кадра";
            return false;
        }
        r = vkAllocateMemory(device, &ai, nullptr, &frameMemory[i]);
        if (r != VK_SUCCESS) return fail(err, "vkAllocateMemory (данные кадра)", r);
        vkBindBufferMemory(device, frameBuffers[i], frameMemory[i], 0);
        r = vkMapMemory(device, frameMemory[i], 0, sizeof(FrameUbo), 0, &frameMapped[i]);
        if (r != VK_SUCCESS) return fail(err, "vkMapMemory (данные кадра)", r);

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descriptorPool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &frameLayout;
        r = vkAllocateDescriptorSets(device, &dai, &frameSets[i]);
        if (r != VK_SUCCESS) return fail(err, "vkAllocateDescriptorSets (данные кадра)", r);
        VkDescriptorBufferInfo info{frameBuffers[i], 0, sizeof(FrameUbo)};
        VkDescriptorImageInfo shadowInfo{shadowSampler, shadowArrayView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = frameSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &info;
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = frameSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &shadowInfo;
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    return true;
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

void Renderer::Impl::destroyMesh(GpuMesh& m) {
    if (m.vertexBuffer) vkDestroyBuffer(device, m.vertexBuffer, nullptr);
    if (m.indexBuffer) vkDestroyBuffer(device, m.indexBuffer, nullptr);
    if (m.vertexMemory) vkFreeMemory(device, m.vertexMemory, nullptr);
    if (m.indexMemory) vkFreeMemory(device, m.indexMemory, nullptr);
    m = GpuMesh{};
}

VkDescriptorSet Renderer::Impl::descriptorFor(TextureId id) const {
    if (id != 0 && id <= textures.size() && textures[id - 1].set) return textures[id - 1].set;
    return whiteTexture ? textures[whiteTexture - 1].set : VK_NULL_HANDLE;
}

bool Renderer::Impl::createTextureObjects(std::string& err) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings = &binding;
    VkResult r = vkCreateDescriptorSetLayout(device, &lci, nullptr, &setLayout);
    if (r != VK_SUCCESS) return fail(err, "vkCreateDescriptorSetLayout", r);

    // Данные кадра (set 1): один буфер uniform, виден вершинному и фрагментному шейдерам.
    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding = 0;
    frameBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutBinding shadowBinding{};  // карта теней: массив глубины и сравнивающий сэмплер, только фрагментный шейдер
    shadowBinding.binding = 1;
    shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    const std::array<VkDescriptorSetLayoutBinding, 2> frameBindings{frameBinding, shadowBinding};
    VkDescriptorSetLayoutCreateInfo fli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    fli.bindingCount = static_cast<std::uint32_t>(frameBindings.size());
    fli.pBindings = frameBindings.data();
    r = vkCreateDescriptorSetLayout(device, &fli, nullptr, &frameLayout);
    if (r != VK_SUCCESS) return fail(err, "vkCreateDescriptorSetLayout (данные кадра)", r);

    // 1024 текстуры, плюс запас на HDR-цель; отдельные места под данные кадра.
    constexpr std::uint32_t kMaxTextures = 1024 + 16;
    const std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures},
                                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight}}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = kMaxTextures + kFramesInFlight;
    pci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    pci.pPoolSizes = sizes.data();
    r = vkCreateDescriptorPool(device, &pci, nullptr, &descriptorPool);
    if (r != VK_SUCCESS) return fail(err, "vkCreateDescriptorPool", r);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.anisotropyEnable = anisotropySupported ? VK_TRUE : VK_FALSE;
    sci.maxAnisotropy = anisotropySupported ? maxAnisotropy : 1.0f;
    sci.minLod = 0.0f;
    sci.maxLod = VK_LOD_CLAMP_NONE;
    r = vkCreateSampler(device, &sci, nullptr, &sampler);
    if (r != VK_SUCCESS) return fail(err, "vkCreateSampler", r);

    if (!createShadowMap(err)) return false;

    VkSamplerCreateInfo csi{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};  // для HDR-цели: без повтора и без мипов
    csi.magFilter = VK_FILTER_LINEAR;
    csi.minFilter = VK_FILTER_LINEAR;
    csi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    csi.addressModeU = csi.addressModeV = csi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    r = vkCreateSampler(device, &csi, nullptr, &clampSampler);
    if (r != VK_SUCCESS) return fail(err, "vkCreateSampler (HDR)", r);
    return createFrameData(err);
}

bool Renderer::Impl::submitOneTime(const std::function<void(VkCommandBuffer)>& record, std::string& err) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult r = vkAllocateCommandBuffers(device, &ai, &cmd);
    if (r != VK_SUCCESS) return fail(err, "vkAllocateCommandBuffers (загрузка)", r);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    r = vkCreateFence(device, &fi, nullptr, &fence);
    if (r == VK_SUCCESS) {
        VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cbi.commandBuffer = cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cbi;
        r = vkQueueSubmit2(queue, 1, &submit, fence);
        if (r == VK_SUCCESS) r = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
    }
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    return r == VK_SUCCESS ? true : fail(err, "отправка команд загрузки", r);
}

void Renderer::Impl::destroyTexture(GpuTexture& t) {
    if (t.set) vkFreeDescriptorSets(device, descriptorPool, 1, &t.set);
    if (t.view) vkDestroyImageView(device, t.view, nullptr);
    if (t.image) vkDestroyImage(device, t.image, nullptr);
    if (t.memory) vkFreeMemory(device, t.memory, nullptr);
    t = GpuTexture{};
}

bool Renderer::Impl::createTexture(const TextureData& data, GpuTexture& out, std::string& err) {
    // Все уровни подряд в одном промежуточном буфере.
    std::vector<std::uint8_t> packed;
    std::vector<VkBufferImageCopy> regions;
    std::uint32_t w = data.width, h = data.height;
    for (std::uint32_t level = 0; level < data.mips.size(); ++level) {
        VkBufferImageCopy region{};
        region.bufferOffset = packed.size();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        region.imageExtent = {w, h, 1};
        regions.push_back(region);
        packed.insert(packed.end(), data.mips[level].begin(), data.mips[level].end());
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    const auto levels = static_cast<std::uint32_t>(data.mips.size());

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    auto cleanupStaging = [&] {
        if (staging) vkDestroyBuffer(device, staging, nullptr);
        if (stagingMemory) vkFreeMemory(device, stagingMemory, nullptr);
    };
    if (!createBuffer(packed.data(), packed.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, stagingMemory, err)) {
        cleanupStaging();
        return false;
    }

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = data.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {data.width, data.height, 1};
    ii.mipLevels = levels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device, &ii, nullptr, &out.image);
    if (r != VK_SUCCESS) {
        cleanupStaging();
        return fail(err, "vkCreateImage (текстура)", r);
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, out.image, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    if (!findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mai.memoryTypeIndex)) {
        cleanupStaging();
        destroyTexture(out);
        err = "нет памяти GPU для текстуры";
        return false;
    }
    r = vkAllocateMemory(device, &mai, nullptr, &out.memory);
    if (r != VK_SUCCESS) {
        cleanupStaging();
        destroyTexture(out);
        return fail(err, "vkAllocateMemory (текстура)", r);
    }
    vkBindImageMemory(device, out.image, out.memory, 0);

    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    const bool uploaded = submitOneTime(
        [&](VkCommandBuffer cmd) {
            auto transition = [&](VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
                VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                b.srcStageMask = srcStage;
                b.srcAccessMask = srcAccess;
                b.dstStageMask = dstStage;
                b.dstAccessMask = dstAccess;
                b.oldLayout = from;
                b.newLayout = to;
                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = out.image;
                b.subresourceRange = all;
                VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dep.imageMemoryBarrierCount = 1;
                dep.pImageMemoryBarriers = &b;
                vkCmdPipelineBarrier2(cmd, &dep);
            };
            transition(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                       VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            vkCmdCopyBufferToImage(cmd, staging, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(regions.size()),
                                   regions.data());
            transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        },
        err);
    cleanupStaging();
    if (!uploaded) {
        destroyTexture(out);
        return false;
    }

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange = all;
    r = vkCreateImageView(device, &vi, nullptr, &out.view);
    if (r != VK_SUCCESS) {
        destroyTexture(out);
        return fail(err, "vkCreateImageView (текстура)", r);
    }

    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptorPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &setLayout;
    r = vkAllocateDescriptorSets(device, &dai, &out.set);
    if (r != VK_SUCCESS) {
        destroyTexture(out);
        return fail(err, "vkAllocateDescriptorSets (слишком много текстур?)", r);
    }
    VkDescriptorImageInfo imageInfo{sampler, out.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = out.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return true;
}

bool Renderer::Impl::createMesh(const MeshData& mesh, std::span<const TextureId> textureIds, GpuMesh& out, std::string& err) {
    out.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    if (!mesh.vertices.empty()) {
        for (int a = 0; a < 3; ++a) out.boundsMin[a] = out.boundsMax[a] = mesh.vertices[0].position[a];
        for (const Vertex& v : mesh.vertices)
            for (int a = 0; a < 3; ++a) {
                out.boundsMin[a] = std::min(out.boundsMin[a], v.position[a]);
                out.boundsMax[a] = std::max(out.boundsMax[a], v.position[a]);
            }
    }
    for (const Submesh& s : effectiveSubmeshes(mesh)) {
        TextureId tex = 0;
        if (s.texture >= 0 && static_cast<std::size_t>(s.texture) < textureIds.size()) tex = textureIds[static_cast<std::size_t>(s.texture)];
        const auto pick = [&](std::int32_t index) -> TextureId {
            return index >= 0 && static_cast<std::size_t>(index) < textureIds.size() ? textureIds[static_cast<std::size_t>(index)] : 0;
        };
        out.submeshes.push_back({s.firstIndex, s.indexCount, tex, s.metallic, s.roughness, pick(s.normalTexture), pick(s.roughnessTexture)});
    }
    const bool ok = createBuffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 out.vertexBuffer, out.vertexMemory, err) &&
                    createBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 out.indexBuffer, out.indexMemory, err);
    if (!ok) destroyMesh(out);
    return ok;
}

void Renderer::Impl::destroyAll() {
    if (device) {
        vkDeviceWaitIdle(device);
        for (GpuMesh& m : meshes) destroyMesh(m);
        for (int i = 0; i < kFramesInFlight; ++i) {
            if (imageAvailable[i]) vkDestroySemaphore(device, imageAvailable[i], nullptr);
            if (inFlight[i]) vkDestroyFence(device, inFlight[i], nullptr);
        }
        destroySwapchain();  // освобождает наборы дескрипторов постобработки из пула, поэтому раньше пула
        if (postReady) post.destroy();
        for (GpuTexture& t : textures) destroyTexture(t);
        if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        for (int i = 0; i < kFramesInFlight; ++i) {
            if (frameMapped[i]) vkUnmapMemory(device, frameMemory[i]);
            if (frameBuffers[i]) vkDestroyBuffer(device, frameBuffers[i], nullptr);
            if (frameMemory[i]) vkFreeMemory(device, frameMemory[i], nullptr);
        }
        if (frameLayout) vkDestroyDescriptorSetLayout(device, frameLayout, nullptr);
        if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        if (clampSampler) vkDestroySampler(device, clampSampler, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (shadowPipeline) vkDestroyPipeline(device, shadowPipeline, nullptr);
        if (shadowLayout) vkDestroyPipelineLayout(device, shadowLayout, nullptr);
        if (shadowSampler) vkDestroySampler(device, shadowSampler, nullptr);
        for (const VkImageView view : shadowLayerViews)
            if (view) vkDestroyImageView(device, view, nullptr);
        if (shadowArrayView) vkDestroyImageView(device, shadowArrayView, nullptr);
        if (shadowImage) vkDestroyImage(device, shadowImage, nullptr);
        if (shadowMemory) vkFreeMemory(device, shadowMemory, nullptr);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (skyPipeline) vkDestroyPipeline(device, skyPipeline, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
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
    s.shadowMapSize = std::clamp(desc.shadowMapSize, 256, 8192);
    if (!s.createInstance(desc, error)) return nullptr;
    s.surface = reinterpret_cast<VkSurfaceKHR>(desc.createSurface ? desc.createSurface(s.instance) : 0);
    if (!s.surface) {
        error = "не удалось создать поверхность окна";
        return nullptr;
    }
    if (!s.pickDevice(error) || !s.createTextureObjects(error) || !s.createSwapchain(desc.width, desc.height, error) ||
        !s.createPipeline(error) || !s.createFrameObjects(error))
        return nullptr;

    // Белая текстура 1x1 для частей меша без текстуры.
    GpuTexture white;
    if (!s.createTexture(makeTexture(1, 1, {255, 255, 255, 255}, false), white, error)) return nullptr;
    s.textures.push_back(white);
    s.whiteTexture = static_cast<TextureId>(s.textures.size());
    return r;
}

TextureId Renderer::uploadTexture(const TextureData& texture) {
    if (!texture.valid()) return 0;
    Impl& s = *impl_;
    GpuTexture gpu;
    std::string err;
    if (!s.createTexture(texture, gpu, err)) {
        std::fprintf(stderr, "uploadTexture: %s\n", err.c_str());
        return 0;
    }
    // Свободный слот (после releaseTexture) используем повторно, иначе добавляем в конец.
    for (std::size_t i = 0; i < s.textures.size(); ++i) {
        if (!s.textures[i].image && static_cast<TextureId>(i + 1) != s.whiteTexture) {
            s.textures[i] = gpu;
            return static_cast<TextureId>(i + 1);
        }
    }
    s.textures.push_back(gpu);
    return static_cast<TextureId>(s.textures.size());
}

void Renderer::releaseTexture(TextureId id) {
    Impl& s = *impl_;
    if (id == 0 || id > s.textures.size() || id == s.whiteTexture || !s.textures[id - 1].image) return;
    vkDeviceWaitIdle(s.device);
    // Меши, ссылавшиеся на текстуру, дальше берут белую: descriptorFor видит пустой слот.
    s.destroyTexture(s.textures[id - 1]);
}

MeshId Renderer::uploadMesh(const MeshData& mesh, std::span<const TextureId> textures) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return 0;
    Impl& s = *impl_;
    GpuMesh gpu;
    std::string err;
    if (!s.createMesh(mesh, textures, gpu, err)) {
        std::fprintf(stderr, "uploadMesh: %s\n", err.c_str());
        return 0;
    }
    // Свободный слот (после releaseMesh) используем повторно, иначе добавляем в конец.
    for (std::size_t i = 0; i < s.meshes.size(); ++i) {
        if (s.meshes[i].indexCount == 0) {
            s.meshes[i] = gpu;
            return static_cast<MeshId>(i + 1);
        }
    }
    s.meshes.push_back(gpu);
    return static_cast<MeshId>(s.meshes.size());
}

void Renderer::releaseMesh(MeshId id) {
    Impl& s = *impl_;
    if (id == 0 || id > s.meshes.size() || s.meshes[id - 1].indexCount == 0) return;
    vkDeviceWaitIdle(s.device);  // буферы могут ещё использоваться предыдущими кадрами
    s.destroyMesh(s.meshes[id - 1]);
}

bool Renderer::replaceMesh(MeshId id, const MeshData& mesh, std::span<const TextureId> textures) {
    Impl& s = *impl_;
    if (id == 0 || id > s.meshes.size() || mesh.vertices.empty() || mesh.indices.empty()) return false;
    GpuMesh fresh;
    std::string err;
    if (!s.createMesh(mesh, textures, fresh, err)) {
        std::fprintf(stderr, "replaceMesh: %s\n", err.c_str());
        return false;
    }
    vkDeviceWaitIdle(s.device);  // старые буферы могут ещё использоваться предыдущими кадрами
    s.destroyMesh(s.meshes[id - 1]);
    s.meshes[id - 1] = fresh;
    return true;
}

bool Renderer::drawFrame(const FrameParams& frame, std::span<const DrawItem> items, int width, int height) {
    Impl& s = *impl_;
    if (width <= 0 || height <= 0) return false;  // окно свёрнуто

    if (s.swapchain == VK_NULL_HANDLE) {  // после сворачивания окна swapchain нет: создаём заново, когда у окна снова есть размер
        std::string recreateError;
        vkDeviceWaitIdle(s.device);
        s.destroySwapchain();  // убирает остатки неудачной попытки
        if (!s.createSwapchain(width, height, recreateError)) {
            s.destroySwapchain();
            return false;
        }
    }

    const int f = s.frame;
    vkWaitForFences(s.device, 1, &s.inFlight[f], VK_TRUE, UINT64_MAX);

    std::uint32_t index = 0;
    VkResult r = vkAcquireNextImageKHR(s.device, s.swapchain, UINT64_MAX, s.imageAvailable[f], VK_NULL_HANDLE, &index);
    std::string err;
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(s.device);
        s.destroySwapchain();
        if (!s.createSwapchain(width, height, err)) s.destroySwapchain();
        return false;  // кадр пропускаем, следующий нарисуется
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    vkResetFences(s.device, 1, &s.inFlight[f]);

    // Данные кадра для шейдеров. Буфер этого кадра в полёте свободен: мы дождались его забора выше.
    FrameUbo ubo{};
    const bool taaOn = frame.taa;
    const core::Mat4 sceneViewProjection =
        taaOn ? jitteredViewProjection(frame.viewProjection, haltonJitter(static_cast<int>(s.frameCounter)), static_cast<int>(s.extent.width),
                                       static_cast<int>(s.extent.height))
              : frame.viewProjection;
    std::memcpy(ubo.viewProj, sceneViewProjection.m, sizeof(ubo.viewProj));
    core::Mat4 inverse;
    if (!core::invert(sceneViewProjection, inverse)) inverse = core::Mat4::identity();
    std::memcpy(ubo.invViewProj, inverse.m, sizeof(ubo.invViewProj));
    const auto put3 = [](float* dst, core::Vec3 v, float w = 0.0f) {
        dst[0] = v.x;
        dst[1] = v.y;
        dst[2] = v.z;
        dst[3] = w;
    };
    put3(ubo.cameraPos, frame.cameraPosition);
    put3(ubo.sunDir, core::normalize(frame.sun.direction));
    put3(ubo.sunColor, frame.sun.color, frame.sun.illuminance);
    put3(ubo.skyZenith, frame.sky.zenith);
    put3(ubo.skyHorizon, frame.sky.horizon);
    put3(ubo.skyGround, frame.sky.ground);
    ubo.params[0] = frame.exposure;
    ubo.params[1] = frame.fogDensity;
    ubo.params[2] = frame.neonBoost;
    {
        const std::size_t count = std::min<std::size_t>(frame.pointLights.size(), 32);
        for (std::size_t i = 0; i < count; ++i) {
            const PointLight& pl = frame.pointLights[i];
            ubo.lightPos[i][0] = pl.position.x;
            ubo.lightPos[i][1] = pl.position.y;
            ubo.lightPos[i][2] = pl.position.z;
            ubo.lightPos[i][3] = pl.radius;
            ubo.lightColor[i][0] = pl.color.x;
            ubo.lightColor[i][1] = pl.color.y;
            ubo.lightColor[i][2] = pl.color.z;
        }
        ubo.lightInfo[0] = static_cast<float>(count);
        ubo.lightInfo[1] = std::clamp(frame.wetness, 0.f, 1.f);
    }

    // Каскады теней. Без солнечного света (ночь) или при выключенных тенях шейдер получает «тени выключены», а карты просто очищаются.
    ShadowSettings shadowSettings = frame.shadow;
    shadowSettings.mapSize = s.shadowMapSize;
    const ShadowCascades cascades =
        computeShadowCascades(frame.viewProjection, frame.nearPlane, frame.farPlane, core::normalize(frame.sun.direction), shadowSettings);
    const bool shadowsOn = frame.shadows && frame.sun.illuminance > 0.0f && cascades.cascade[0].radius > 0.0f;
    for (int i = 0; i < kShadowCascades; ++i) {
        const ShadowCascade& k = cascades.cascade[static_cast<std::size_t>(i)];
        std::memcpy(ubo.lightViewProj[i], k.viewProjection.m, sizeof(ubo.lightViewProj[i]));
        ubo.cascadeSplits[i] = k.splitDistance;
        ubo.cascadeTexel[i] = k.texelWorldSize;
    }
    ubo.cascadeSplits[3] = shadowsOn ? 1.0f : 0.0f;
    ubo.shadowParams[0] = 1.0f / static_cast<float>(s.shadowMapSize);
    ubo.shadowParams[1] = frame.debugCascades ? 1.0f : 0.0f;
    std::memcpy(s.frameMapped[f], &ubo, sizeof(ubo));

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

    // --- Проход 0: карты теней — по одному проходу глубины на каскад. Проходы выполняются всегда (при выключенных тенях — только
    // очистка), чтобы изображение карты оказывалось в правильной раскладке для чтения шейдером.
    {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        b.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // прошлое содержимое не нужно: карта перерисовывается целиком
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = s.shadowImage;
        b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }
    const auto mapExtent = static_cast<std::uint32_t>(s.shadowMapSize);
    const VkViewport shadowViewport{0, 0, static_cast<float>(mapExtent), static_cast<float>(mapExtent), 0, 1};
    const VkRect2D shadowScissor{{0, 0}, {mapExtent, mapExtent}};
    for (std::uint32_t cascade = 0; cascade < kShadowCascades; ++cascade) {
        VkRenderingAttachmentInfo shadowDepth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        shadowDepth.imageView = s.shadowLayerViews[cascade];
        shadowDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        shadowDepth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo shadowInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        shadowInfo.renderArea = shadowScissor;
        shadowInfo.layerCount = 1;
        shadowInfo.pDepthAttachment = &shadowDepth;
        vkCmdBeginRendering(cmd, &shadowInfo);
        if (shadowsOn) {
            vkCmdSetViewport(cmd, 0, 1, &shadowViewport);
            vkCmdSetScissor(cmd, 0, 1, &shadowScissor);
            vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);  // постоянное и наклонное смещение против «лесенки»
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.shadowPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.shadowLayout, 1, 1, &s.frameSets[f], 0, nullptr);
            ShadowPush shadowPush{};
            shadowPush.cascade = cascade;
            // Отсечение по пирамиде каскада без ближней плоскости: отбрасывающие тень объекты могут стоять между светом и кадром.
            const Frustum cascadeFrustum = Frustum::from(ubo.lightViewProj[cascade], false);
            for (const DrawItem& item : items) {
                if (item.mesh == 0 || item.mesh > s.meshes.size()) continue;
                const GpuMesh& mesh = s.meshes[item.mesh - 1];
                if (mesh.indexCount == 0) continue;
                if (!cascadeFrustum.sees(item.model.m, mesh.boundsMin, mesh.boundsMax)) continue;
                std::memcpy(shadowPush.model, item.model.m, sizeof(shadowPush.model));
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(cmd, s.shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(shadowPush), &shadowPush);
                vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);  // материалы тени не нужны: весь меш одним вызовом
            }
        }
        vkCmdEndRendering(cmd);
    }
    {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        b.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = s.shadowImage;
        b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    // --- Проход 1: сцена в линейный HDR (меши, затем небо на дальней плоскости).
    // Предыдущий кадр в полёте мог ещё читать HDR в тонмаппинге и писать глубину — ждём обе стадии (содержимое не нужно).
    barrier(s.hdrImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    constexpr VkPipelineStageFlags2 kDepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier(s.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            kDepthStages | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo hdr{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    hdr.imageView = s.hdrView;
    hdr.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdr.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    hdr.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    hdr.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = s.depthView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // глубину читает TAA
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo sceneInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    sceneInfo.renderArea = {{0, 0}, s.extent};
    sceneInfo.layerCount = 1;
    sceneInfo.colorAttachmentCount = 1;
    sceneInfo.pColorAttachments = &hdr;
    sceneInfo.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &sceneInfo);

    const VkViewport viewport{0, 0, static_cast<float>(s.extent.width), static_cast<float>(s.extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, s.extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 1, 1, &s.frameSets[f], 0, nullptr);

    VertexPush vertexPush{};
    VkDescriptorSet boundSet = VK_NULL_HANDLE;
    const Frustum viewFrustum = Frustum::from(frame.viewProjection.m, true);
    for (const DrawItem& item : items) {
        if (item.mesh == 0 || item.mesh > s.meshes.size()) continue;
        const GpuMesh& mesh = s.meshes[item.mesh - 1];
        if (mesh.indexCount == 0) continue;
        if (!viewFrustum.sees(item.model.m, mesh.boundsMin, mesh.boundsMax)) continue;  // вне кадра
        std::memcpy(vertexPush.model, item.model.m, sizeof(vertexPush.model));
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vertexPush), &vertexPush);
        for (const GpuSubmesh& part : mesh.submeshes) {
            if (part.indexCount == 0) continue;
            const VkDescriptorSet set = s.descriptorFor(part.texture);
            if (set != boundSet) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 0, 1, &set, 0, nullptr);
                boundSet = set;
            }
            const VkDescriptorSet normalSet = s.descriptorFor(part.normalTexture), roughSet = s.descriptorFor(part.roughnessTexture);
            const VkDescriptorSet extra[2] = {normalSet, roughSet};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 2, 2, extra, 0, nullptr);
            const FragmentPush material{{part.metallic, part.roughness, part.normalTexture != 0 ? 1.0f : 0.0f, part.roughnessTexture != 0 ? 1.0f : 0.0f}};
            vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, kFragmentPushOffset, sizeof(material), &material);
            vkCmdDrawIndexed(cmd, part.indexCount, 1, part.firstIndex, 0, 0);
        }
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.skyPipeline);  // небо поверх незакрытых пикселей
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);

    // --- Проход 2: постобработка (bloom → тонмаппинг AgX) пишет итог в изображение swapchain.
    barrier(s.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    PostParams postParams;
    postParams.taa = taaOn;
    if (taaOn) {
        // История пригодна, если прошлый кадр тоже шёл с TAA, размер не менялся и камера не «склеена».
        core::Mat4 reproject;
        const bool invertible = reprojectionMatrix(frame.viewProjection, s.prevViewProjection, reproject);
        postParams.historyValid = invertible && s.historyValid && s.prevTaa && !frame.resetHistory;
        if (invertible) std::memcpy(postParams.reproject, reproject.m, sizeof(postParams.reproject));
        postParams.taaFeedback = frame.taaFeedback;
        // TAA читает глубину сцены: из вложения — в чтение.
        barrier(s.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    postParams.exposure = frame.exposure;
    postParams.contrast = frame.contrast;
    postParams.saturation = frame.saturation;
    postParams.bloomIntensity = frame.bloomIntensity;
    postParams.bloomThreshold = frame.bloomThreshold;
    postParams.bloomKnee = frame.bloomKnee;
    postParams.bloomRadius = frame.bloomRadius;
    s.post.record(cmd, s.views[index], s.extent, postParams);
    s.prevViewProjection = frame.viewProjection;
    s.historyValid = true;
    s.prevTaa = taaOn;
    ++s.frameCounter;

    if (s.overlay) {
        // Третий проход поверх экрана без глубины: сначала дожидаемся записи цвета тонмаппингом.
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cmd, &dep);

        VkRenderingAttachmentInfo overlayColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        overlayColor.imageView = s.views[index];
        overlayColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        overlayColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        overlayColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ori{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ori.renderArea = {{0, 0}, s.extent};
        ori.layerCount = 1;
        ori.colorAttachmentCount = 1;
        ori.pColorAttachments = &overlayColor;
        vkCmdBeginRendering(cmd, &ori);
        s.overlay->record(cmd);
        vkCmdEndRendering(cmd);
    }

    // Снимок: копируем готовое изображение в буфер, который прочитаем на CPU после завершения кадра.
    VkBuffer shotBuffer = VK_NULL_HANDLE;
    VkDeviceMemory shotMemory = VK_NULL_HANDLE;
    const bool capturing = !s.pendingShot.empty() && s.canCapture;
    if (capturing) {
        const VkDeviceSize shotBytes = static_cast<VkDeviceSize>(s.extent.width) * s.extent.height * 4;
        if (!s.createBuffer(nullptr, shotBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, shotBuffer, shotMemory, s.shotError)) {
            if (shotBuffer) vkDestroyBuffer(s.device, shotBuffer, nullptr);
            if (shotMemory) vkFreeMemory(s.device, shotMemory, nullptr);
            shotBuffer = VK_NULL_HANDLE;
            shotMemory = VK_NULL_HANDLE;
        }
    }
    if (shotBuffer) {
        barrier(s.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {s.extent.width, s.extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, s.images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shotBuffer, 1, &region);
        barrier(s.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
    } else {
        barrier(s.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
    }
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
    if (shotBuffer) {
        // Ждём конца этого кадра (забор) и читаем снимок. Формат swapchain: B8G8R8A8 или R8G8B8A8; байты уже в sRGB, как на экране.
        vkWaitForFences(s.device, 1, &s.inFlight[f], VK_TRUE, UINT64_MAX);
        void* mapped = nullptr;
        if (vkMapMemory(s.device, shotMemory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS) {
            const auto* src = static_cast<const std::uint8_t*>(mapped);
            const bool bgra = s.format == VK_FORMAT_B8G8R8A8_SRGB || s.format == VK_FORMAT_B8G8R8A8_UNORM;
            const bool rgba = s.format == VK_FORMAT_R8G8B8A8_SRGB || s.format == VK_FORMAT_R8G8B8A8_UNORM;
            if (bgra || rgba) {
                std::vector<std::uint8_t> rgb(static_cast<std::size_t>(s.extent.width) * s.extent.height * 3);
                for (std::size_t i = 0, n = static_cast<std::size_t>(s.extent.width) * s.extent.height; i < n; ++i) {
                    rgb[i * 3 + 0] = src[i * 4 + (bgra ? 2 : 0)];
                    rgb[i * 3 + 1] = src[i * 4 + 1];
                    rgb[i * 3 + 2] = src[i * 4 + (bgra ? 0 : 2)];
                }
                s.shotError.clear();
                writePng(s.pendingShot, s.extent.width, s.extent.height, 3, rgb.data(), s.shotError);
            } else {
                s.shotError = "формат swapchain не поддерживается для снимка";
            }
            vkUnmapMemory(s.device, shotMemory);
        } else {
            s.shotError = "не удалось прочитать снимок из памяти GPU";
        }
        vkDestroyBuffer(s.device, shotBuffer, nullptr);
        vkFreeMemory(s.device, shotMemory, nullptr);
        s.pendingShot.clear();
    } else if (capturing) {
        s.pendingShot.clear();  // не удалось подготовить буфер: ошибка уже записана в shotError
    }
    s.frame = (f + 1) % kFramesInFlight;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(s.device);
        s.destroySwapchain();
        if (!s.createSwapchain(width, height, err)) s.destroySwapchain();
    }
    return true;
}

bool Renderer::requestScreenshot(const std::filesystem::path& path) {
    Impl& s = *impl_;
    if (!s.canCapture || !s.pendingShot.empty() || path.empty()) return false;
    s.shotError.clear();
    s.pendingShot = path;
    return true;
}
bool Renderer::screenshotPending() const { return !impl_->pendingShot.empty(); }
const std::string& Renderer::screenshotError() const { return impl_->shotError; }

const std::string& Renderer::deviceName() const { return impl_->deviceName; }
bool Renderer::validationEnabled() const { return impl_->validation; }
std::uint32_t Renderer::validationErrors() const { return impl_->counters.errors; }
std::uint32_t Renderer::validationWarnings() const { return impl_->counters.warnings; }

}  // namespace han::render
