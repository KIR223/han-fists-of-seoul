// Демо E8: игровой интерфейс на RmlUi поверх сцены — HUD (полоски, подсказки), меню паузы с кнопками, русский и корейский текст.
// Запуск из корня репозитория (или двойным щелчком): han_ui_demo.exe [--frames N] [--no-capture] [--size W H] [--screenshot файл.png]
//   [--doc файл.rml] [--font файл.ttf]
// --lang ru|ko|en — язык, --menu — сразу открыть меню. Меню: стрелки — фокус, Enter — выбор, Esc — назад/пауза; мышь тоже работает.
// Нужен шрифт: python han/tools/pipeline/han_fetch.py process fn_noto_sans_kr (Noto Sans KR, OFL). Esc — выход, Enter — меню паузы.
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "han/core/repo_root.h"
#include "han/platform/window.h"
#include "han/render/camera.h"
#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#if defined(HAN_WITH_IMGUI) && defined(HAN_WITH_RMLUI)
#include "han/ui/imgui_layer.h"
#include "han/ui/rml_layer.h"
#include "han/ui/localization.h"
#endif

int main(int argc, char** argv) {
    han::core::enterRepoRoot(argv[0]);
#if !(defined(HAN_WITH_IMGUI) && defined(HAN_WITH_RMLUI))
    (void)argc;
    std::fprintf(stderr, "собрано без RmlUi или ImGui (включите vcpkg-функцию ui)\n");
    return 1;
#else
    long maxFrames = -1;
    bool captured = true;
    int windowSize[2] = {1280, 720};
    std::string language = "ru";
    bool openMenu = false;
    std::filesystem::path screenshotPath, docPath = "han/ui/samples/ui_demo.rml", fontPath = "assets_bin/cooked/fn_noto_sans_kr/NotoSansKR.ttf";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--doc") && i + 1 < argc) docPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--lang") && i + 1 < argc) language = argv[i + 1];
        if (!std::strcmp(argv[i], "--menu")) openMenu = true;
        if (!std::strcmp(argv[i], "--font") && i + 1 < argc) fontPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            windowSize[0] = std::max(64, std::atoi(argv[i + 1]));
            windowSize[1] = std::max(64, std::atoi(argv[i + 2]));
        }
    }
    (void)captured;

    auto window = han::platform::Window::create("HAN: Fists of Seoul — Интерфейс", windowSize[0], windowSize[1]);
    if (!window) return 1;
    han::ui::ImGuiLayer imgui;  // раньше Renderer и RmlLayer по порядку жизни
    if (!imgui.init(*window)) return 1;
    han::ui::RmlLayer rml;
    std::string error;
    han::render::RendererDesc desc;
    desc.instanceExtensions = window->vulkanInstanceExtensions();
    desc.createSurface = [&](void* instance) { return window->createVulkanSurface(instance); };
    window->pixelSize(desc.width, desc.height);
    auto renderer = han::render::Renderer::create(desc, error);
    if (!renderer) {
        std::fprintf(stderr, "рендерер: %s\n", error.c_str());
        return 1;
    }
    renderer->setOverlay(&imgui);

    if (!rml.init(windowSize[0], windowSize[1])) return 1;
    han::ui::Localization loc;
    {
        std::string locError;
        if (!loc.loadTable("han/ui/samples/ui_demo_loc.csv", locError) || !locError.empty()) std::fprintf(stderr, "локализация: %s\n", locError.c_str());
        loc.setLanguage(language);
    }
    if (!rml.loadFont(fontPath.string())) std::fprintf(stderr, "шрифт не загружен: %s\n", fontPath.string().c_str());
    if (!rml.loadDocument(docPath.string())) {
        std::fprintf(stderr, "документ не загружен: %s\n", docPath.string().c_str());
        return 1;
    }
    rml.applyLocalization(loc);
    std::printf("GPU: %s; слой валидации: %s\n", renderer->deviceName().c_str(), renderer->validationEnabled() ? "включён" : "НЕ найден");

    // Сцена за интерфейсом: земля и несколько кубов.
    auto floorData = han::render::makePlane(60.f, {0.35f, 0.38f, 0.36f});
    floorData.submeshes = {{0, static_cast<std::uint32_t>(floorData.indices.size()), -1, 0.0f, 0.9f}};
    const auto floorMesh = renderer->uploadMesh(floorData);
    auto boxData = han::render::makeBox({1.f, 1.f, 1.f}, {0.75f, 0.45f, 0.30f});
    boxData.submeshes = {{0, static_cast<std::uint32_t>(boxData.indices.size()), -1, 0.0f, 0.6f}};
    const auto boxMesh = renderer->uploadMesh(boxData);
    han::render::Camera camera;
    camera.position = {0.f, 3.f, 9.f};
    camera.pitch = -0.2f;

    bool paused = openMenu;
    rml.setVisible("menu", paused);
    int clicks = 0;
    const Uint64 start = SDL_GetTicksNS();
    for (long frame = 0; (maxFrames < 0 || frame < maxFrames); ++frame) {
        han::platform::InputMap input;
        input.bind("back", 41);
        input.bind("accept", 40);
        input.bind("up", 82);
        input.bind("down", 81);
        input.bind("left", 80);
        input.bind("right", 79);
        input.beginFrame(static_cast<double>(SDL_GetTicksNS()) * 1e-9);
        const auto ev = window->pump(input);
        if (ev.quitRequested) break;
        int w = 0, h = 0;
        window->pixelSize(w, h);
        imgui.beginFrame();
        rml.beginFrame(w, h);
        if (openMenu && frame == 2) rml.navigate(han::ui::RmlLayer::NavKey::Down);  // раскладка уже посчитана
        if (paused) {  // меню управляется клавиатурой: стрелки — фокус, Enter — выбор, Esc — назад
            if (input.pressed("up")) rml.navigate(han::ui::RmlLayer::NavKey::Up);
            if (input.pressed("down")) rml.navigate(han::ui::RmlLayer::NavKey::Down);
            if (input.pressed("left")) rml.navigate(han::ui::RmlLayer::NavKey::Left);
            if (input.pressed("right")) rml.navigate(han::ui::RmlLayer::NavKey::Right);
            if (input.pressed("accept")) rml.navigate(han::ui::RmlLayer::NavKey::Accept);
            if (input.pressed("back")) rml.navigate(han::ui::RmlLayer::NavKey::Back);
        } else if (input.pressed("back") || input.pressed("accept")) {
            paused = true;
            rml.setVisible("menu", true);
            rml.navigate(han::ui::RmlLayer::NavKey::Down);
        }

        // Живые значения HUD: здоровье убывает и возвращается, чтобы полоски двигались.
        const double t = static_cast<double>(SDL_GetTicksNS() - start) * 1e-9;
        const int health = static_cast<int>(60.0 + 35.0 * std::sin(t * 0.8));
        rml.setText("health-value", std::to_string(health));
        const std::string id = rml.takeClicked();
        if (!id.empty()) {
            ++clicks;
            std::printf("клик: %s\n", id.c_str());
            if (id == "btn-quit") break;
            if (id == "btn-resume" || id == "ui:back") {
                paused = false;
                rml.setVisible("menu", false);
            }
            if (id.rfind("btn-lang-", 0) == 0) {  // язык переключается на лету
                loc.setLanguage(id.substr(9));
                rml.applyLocalization(loc);
            }
        }
        rml.draw();

        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        std::vector<han::render::DrawItem> items;
        items.push_back({floorMesh, han::core::Mat4::identity()});
        for (int i = 0; i < 5; ++i) items.push_back({boxMesh, han::core::Mat4::translation({-8.f + 4.f * static_cast<float>(i), 1.f, -4.f})});
        han::render::FrameParams params;
        params.viewProjection = camera.viewProjection(aspect);
        params.cameraPosition = camera.position;
        params.sun = han::render::makeSun(45.f, 140.f, 5600.f);
        params.sky = han::render::skyFromSun(params.sun);
        params.nearPlane = camera.nearZ;
        params.farPlane = camera.farZ;
        params.taa = false;
        const bool shootNow = !screenshotPath.empty() && maxFrames > 0 && frame == maxFrames - 1;
        if (shootNow) renderer->requestScreenshot(screenshotPath);
        renderer->drawFrame(params, items, w, h);
    }
    std::printf("кадров отрисовано, кликов: %d, ошибок валидации: %u, предупреждений: %u\n", clicks, renderer->validationErrors(), renderer->validationWarnings());
    return renderer->validationErrors() == 0 ? 0 : 1;
#endif
}
