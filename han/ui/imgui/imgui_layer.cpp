#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "han/ui/imgui_layer.h"

namespace han::ui {
namespace {

void checkVk(VkResult r) {
    if (r < 0) std::fprintf(stderr, "[imgui] ошибка Vulkan %d\n", static_cast<int>(r));
}

// Шрифт для разработки: системный, с кириллицей и хангылем. В игру не входит (игровой шрифт — этап E8, свободная лицензия).
void loadDevFont(ImGuiIO& io) {
    for (const char* path : {"C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/segoeui.ttf"}) {
        if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path, 17.0f)) return;
    }
}

}  // namespace

struct ImGuiLayer::Impl {
    bool contextCreated = false;
    bool sdlReady = false;
    bool vulkanReady = false;
    bool frameStarted = false;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;  // должен жить, пока жив конвейер ImGui

    // Состояние консоли.
    std::vector<std::string> log;
    char input[256] = {};
    bool scrollToBottom = false;
};

ImGuiLayer::ImGuiLayer() : impl_(std::make_unique<Impl>()) {}

ImGuiLayer::~ImGuiLayer() {
    if (impl_->vulkanReady) ImGui_ImplVulkan_Shutdown();
    if (impl_->sdlReady) ImGui_ImplSDL3_Shutdown();
    if (impl_->contextCreated) ImGui::DestroyContext();
}

bool ImGuiLayer::init(platform::Window& window) {
    Impl& s = *impl_;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    s.contextCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // положение окон не пишем на диск
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    loadDevFont(io);
    ImGui::StyleColorsDark();

    s.sdlReady = ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(window.nativeHandle()));
    if (!s.sdlReady) return false;
    window.setEventHook([](const void* e) { ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(e)); });
    return true;
}

void ImGuiLayer::onRendererReady(const render::VulkanHandles& h) {
    Impl& s = *impl_;
    s.colorFormat = static_cast<VkFormat>(h.colorFormat);

    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = h.apiVersion;
    info.Instance = static_cast<VkInstance>(h.instance);
    info.PhysicalDevice = static_cast<VkPhysicalDevice>(h.physicalDevice);
    info.Device = static_cast<VkDevice>(h.device);
    info.QueueFamily = h.queueFamily;
    info.Queue = static_cast<VkQueue>(h.queue);
    info.DescriptorPoolSize = 64;  // запас под текстуры RmlUi (атласы шрифтов, картинки)
    info.MinImageCount = 2;
    info.ImageCount = h.imageCount < 2 ? 2 : h.imageCount;
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &s.colorFormat;
    info.CheckVkResultFn = checkVk;
    s.vulkanReady = ImGui_ImplVulkan_Init(&info);
}

void ImGuiLayer::onRendererShutdown() {
    Impl& s = *impl_;
    if (s.vulkanReady) ImGui_ImplVulkan_Shutdown();
    s.vulkanReady = false;
    s.frameStarted = false;
}

void ImGuiLayer::beginFrame() {
    Impl& s = *impl_;
    if (!s.vulkanReady) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    s.frameStarted = true;
}

void ImGuiLayer::record(void* vkCommandBuffer) {
    Impl& s = *impl_;
    if (!s.vulkanReady || !s.frameStarted) return;
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(vkCommandBuffer));
    s.frameStarted = false;
}

bool ImGuiLayer::wantsKeyboard() const { return impl_->contextCreated && ImGui::GetIO().WantCaptureKeyboard; }
bool ImGuiLayer::wantsMouse() const { return impl_->contextCreated && ImGui::GetIO().WantCaptureMouse; }

void ImGuiLayer::drawConsole(core::Console& console, bool& open) {
    Impl& s = *impl_;
    if (!s.frameStarted || !open) return;
    ImGui::SetNextWindowSize(ImVec2(640, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Консоль", &open)) {
        ImGui::End();
        return;
    }
    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("лог", ImVec2(0, -footer), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const std::string& line : s.log) ImGui::TextUnformatted(line.c_str());
        if (s.scrollToBottom) ImGui::SetScrollHereY(1.0f);
        s.scrollToBottom = false;
    }
    ImGui::EndChild();

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##ввод", s.input, sizeof(s.input), ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string line = s.input;
        s.input[0] = '\0';
        if (!line.empty()) {
            s.log.push_back("> " + line);
            const std::string out = console.execute(line);
            std::size_t pos = 0;
            while (pos < out.size()) {
                auto end = out.find('\n', pos);
                if (end == std::string::npos) end = out.size();
                if (end > pos) s.log.push_back(out.substr(pos, end - pos));
                pos = end + 1;
            }
            s.scrollToBottom = true;
        }
        ImGui::SetKeyboardFocusHere(-1);  // оставить фокус в поле ввода
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

void ImGuiLayer::drawStats(const std::vector<std::string>& lines) {
    if (!impl_->frameStarted) return;
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.35f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##статистика", nullptr, flags))
        for (const std::string& line : lines) ImGui::TextUnformatted(line.c_str());
    ImGui::End();
}

void ImGuiLayer::drawLines(const std::vector<Line3D>& lines, const core::Mat4& viewProjection, int width, int height) {
    if (!impl_->frameStarted || width <= 0 || height <= 0) return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    constexpr float kNearW = 0.05f;
    for (const Line3D& line : lines) {
        core::Vec4 p = core::transform(viewProjection, line.a);
        core::Vec4 q = core::transform(viewProjection, line.b);
        if (p.w < kNearW && q.w < kNearW) continue;
        // Обрезка по ближней плоскости: отрезок укорачивается до w = kNearW.
        if (p.w < kNearW) {
            const float k = (kNearW - p.w) / (q.w - p.w);
            p = {p.x + (q.x - p.x) * k, p.y + (q.y - p.y) * k, p.z + (q.z - p.z) * k, kNearW};
        } else if (q.w < kNearW) {
            const float k = (kNearW - q.w) / (p.w - q.w);
            q = {q.x + (p.x - q.x) * k, q.y + (p.y - q.y) * k, q.z + (p.z - q.z) * k, kNearW};
        }
        const auto toScreen = [&](const core::Vec4& v) {
            return ImVec2((v.x / v.w * 0.5f + 0.5f) * static_cast<float>(width), (v.y / v.w * 0.5f + 0.5f) * static_cast<float>(height));
        };
        draw->AddLine(toScreen(p), toScreen(q), line.color, 1.5f);
    }
}

}  // namespace han::ui
