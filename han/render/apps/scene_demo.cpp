// Демо E2: сцена из земли и кубов, летающая камера, глубина, освещение, слой валидации, отладочный интерфейс ImGui.
// Управление: мышь — взгляд, W/A/S/D — движение, Space/Ctrl — вверх/вниз, Shift — быстрее, Tab — захват мыши,
// ` (тильда) — консоль, Esc — выход. Команды консоли: help, pos, tp x y z, fov градусы, speed число, echo текст, quit.
// Запуск: han_scene_demo.exe [--frames N] [--no-capture] [--mesh файл.hmesh]
//   --frames: выйти после N кадров (код 0 — ноль ошибок валидации); --mesh: показать меш и перезагружать его при изменении файла.
#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "han/core/asset_watcher.h"
#include "han/core/console.h"
#include "han/render/asset_loader.h"
#include "han/render/mesh_io.h"
#include "han/platform/window.h"
#include "han/render/camera.h"
#include "han/render/renderer.h"
#ifdef HAN_WITH_IMGUI
#include "han/ui/imgui_layer.h"
#endif

namespace {
// Пустое наложение: проверяет проход наложения рендерера (барьер, второй рендер) там, где ImGui не собран. Флаг --null-overlay.
struct NullOverlay final : han::render::IOverlay {
    void onRendererReady(const han::render::VulkanHandles&) override {}
    void record(void*) override {}
    void onRendererShutdown() override {}
};

// Коды SDL_Scancode.
constexpr int kW = 26, kA = 4, kS = 22, kD = 7, kSpace = 44, kLCtrl = 224, kLShift = 225, kTab = 43, kEsc = 41, kGrave = 53;

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}
}  // namespace

int main(int argc, char** argv) {
    long maxFrames = -1;
    bool captured = true, nullOverlay = false;
    std::filesystem::path meshPath;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--null-overlay")) nullOverlay = true;
        if (!std::strcmp(argv[i], "--mesh") && i + 1 < argc) meshPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
    }

    auto window = han::platform::Window::create("HAN: Fists of Seoul — демо E2", 1280, 720);
    if (!window) {
        std::fprintf(stderr, "не удалось создать окно\n");
        return 1;
    }

#ifdef HAN_WITH_IMGUI
    han::ui::ImGuiLayer ui;  // объявлен раньше рендерера: переживает его
    if (!ui.init(*window)) {
        std::fprintf(stderr, "не удалось запустить ImGui\n");
        return 1;
    }
#endif

    han::render::RendererDesc desc;
    desc.instanceExtensions = window->vulkanInstanceExtensions();
    desc.createSurface = [&](void* instance) { return window->createVulkanSurface(instance); };
    window->pixelSize(desc.width, desc.height);

    std::string error;
    auto renderer = han::render::Renderer::create(desc, error);
    if (!renderer) {
        std::fprintf(stderr, "рендерер: %s\n", error.c_str());
        return 1;
    }
    NullOverlay empty;
#ifdef HAN_WITH_IMGUI
    renderer->setOverlay(&ui);
#else
    if (nullOverlay) renderer->setOverlay(&empty);
#endif
    std::printf("GPU: %s; слой валидации: %s\n", renderer->deviceName().c_str(), renderer->validationEnabled() ? "включён" : "НЕ найден");

    using han::core::Mat4;
    using han::core::Vec3;
    // Шахматная текстура с мипами для земли и кубов; колонна остаётся без текстуры (проверка белой текстуры по умолчанию).
    auto checker = han::render::makeCheckerTexture(256, 32, 0xFFD0D0D0, 0xFF606060);
    han::render::generateMips(checker);
    const han::render::TextureId checkerId = renderer->uploadTexture(checker);
    const std::vector<han::render::TextureId> checkerSlots{checkerId};
    const auto textured = [](han::render::MeshData m) {
        m.textures = {"checker"};
        m.submeshes = {{0, static_cast<std::uint32_t>(m.indices.size()), 0}};
        return m;
    };
    const auto ground = renderer->uploadMesh(textured(han::render::makePlane(40.f, {0.55f, 0.75f, 0.55f}, 20.f)), checkerSlots);
    const auto cube = renderer->uploadMesh(textured(han::render::makeBox({0.5f, 0.5f, 0.5f}, {1.0f, 0.55f, 0.4f})), checkerSlots);
    const auto pillar = renderer->uploadMesh(han::render::makeBox({0.4f, 2.f, 0.4f}, {0.7f, 0.7f, 0.75f}));
    if (!checkerId || !ground || !cube || !pillar) {
        std::fprintf(stderr, "не удалось загрузить меши\n");
        return 1;
    }

    // Свой меш из .hmesh (варится han_cooker): стоит в центре сцены на земле, при изменении файла перезагружается.
    han::render::LoadedMesh custom;
    han::core::AssetWatcher watcher;
    const auto watchFiles = [&] {
        for (const auto& file : custom.files) watcher.watch(file);
    };
    if (!meshPath.empty()) {
        std::string loadError;
        if (han::render::loadMeshAsset(*renderer, meshPath, custom, loadError)) {
            std::printf("загружен меш: %s (текстур: %zu)\n", meshPath.string().c_str(), custom.textures.size());
            watchFiles();
        } else {
            std::fprintf(stderr, "меш не загружен: %s\n", loadError.c_str());
            watcher.watch(meshPath);
        }
    }
    double nextPoll = 0;

    han::platform::InputMap input;
    input.bind("forward", kW);
    input.bind("back", kS);
    input.bind("left", kA);
    input.bind("right", kD);
    input.bind("up", kSpace);
    input.bind("down", kLCtrl);
    input.bind("fast", kLShift);
    input.bind("capture", kTab);
    input.bind("quit", kEsc);
    input.bind("console", kGrave);

    han::render::Camera camera;
    camera.position = {0, 3, 10};
    camera.pitch = -0.2f;
    float speed = 6.f;
    bool quit = false;
    bool consoleOpen = false;

    han::core::Console console;
    console.registerCommand("quit", "закрыть демо", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("выход");
    });
    console.registerCommand("pos", "позиция камеры", [&](const std::vector<std::string>&) {
        return fmt("%.2f %.2f %.2f", camera.position.x, camera.position.y, camera.position.z);
    });
    console.registerCommand("tp", "tp x y z — переместить камеру", [&](const std::vector<std::string>& a) {
        if (a.size() != 3) return std::string("использование: tp x y z");
        camera.position = {static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())),
                           static_cast<float>(std::atof(a[2].c_str()))};
        return std::string("готово");
    });
    console.registerCommand("fov", "fov градусы (20..120)", [&](const std::vector<std::string>& a) {
        if (a.size() != 1) return fmt("угол обзора: %.0f", camera.fovY * 57.29578);
        const double deg = std::atof(a[0].c_str());
        if (deg < 20 || deg > 120) return std::string("допустимо 20..120");
        camera.fovY = static_cast<float>(deg / 57.29578);
        return std::string("готово");
    });
    console.registerCommand("speed", "speed число — скорость камеры, м/с", [&](const std::vector<std::string>& a) {
        if (a.size() != 1) return fmt("скорость: %.1f", speed);
        const double v = std::atof(a[0].c_str());
        if (v <= 0 || v > 500) return std::string("допустимо 0..500");
        speed = static_cast<float>(v);
        return std::string("готово");
    });
    console.registerCommand("echo", "echo текст", [](const std::vector<std::string>& a) {
        std::string out;
        for (const auto& s : a) out += (out.empty() ? "" : " ") + s;
        return out;
    });

    window->setCaptureMouse(captured);
    double fps = 0;
    const Uint64 start = SDL_GetTicksNS();
    Uint64 last = start;
    for (long frame = 0; (maxFrames < 0 || frame < maxFrames) && !quit; ++frame) {
        const Uint64 now = SDL_GetTicksNS();
        const float dt = static_cast<float>(static_cast<double>(now - last) * 1e-9);
        const float t = static_cast<float>(static_cast<double>(now - start) * 1e-9);
        last = now;
        if (dt > 0) fps = fps * 0.95 + (1.0 / dt) * 0.05;

        input.beginFrame(static_cast<double>(now) * 1e-9);
        const auto ev = window->pump(input);
        if (ev.quitRequested) break;
#ifdef HAN_WITH_IMGUI
        ui.beginFrame();
        const bool uiKeys = ui.wantsKeyboard();
        const bool uiMouse = ui.wantsMouse();
#else
        const bool uiKeys = false, uiMouse = false;
#endif
        if (input.pressed("console")) {
            consoleOpen = !consoleOpen;
            window->setCaptureMouse(!consoleOpen && captured);  // консоли нужен курсор
        }
        if (!uiKeys) {
            if (input.pressed("quit")) break;
            if (input.pressed("capture") && !consoleOpen) window->setCaptureMouse(captured = !captured);
        }
        if (captured && !consoleOpen && !uiMouse) camera.look(ev.mouseDx, ev.mouseDy);
        const auto axis = [&](const char* pos, const char* neg) {
            return uiKeys ? 0.f : (input.held(pos) ? 1.f : 0.f) - (input.held(neg) ? 1.f : 0.f);
        };
        camera.move(axis("forward", "back"), axis("right", "left"), axis("up", "down"), input.held("fast") && !uiKeys ? speed * 3.f : speed, dt);

#ifdef HAN_WITH_IMGUI
        ui.drawStats({fmt("FPS: %.0f", fps), "GPU: " + renderer->deviceName(),
                      fmt("ошибок валидации: %.0f", renderer->validationErrors()),
                      fmt("камера: %.1f %.1f %.1f", camera.position.x, camera.position.y, camera.position.z),
                      consoleOpen ? "` — закрыть консоль" : "` — консоль"});
        ui.drawConsole(console, consoleOpen);
#endif

        if (!meshPath.empty() && static_cast<double>(t) >= nextPoll) {
            nextPoll = static_cast<double>(t) + 0.25;
            if (!watcher.poll().empty()) {
                std::string loadError;
                if (custom.id == 0) {
                    if (han::render::loadMeshAsset(*renderer, meshPath, custom, loadError)) std::printf("загружен меш: %s\n", meshPath.string().c_str());
                } else if (han::render::reloadMeshAsset(*renderer, custom, loadError)) {
                    std::printf("перезагружен меш: %s (текстур: %zu)\n", meshPath.string().c_str(), custom.textures.size());
                }
                if (!loadError.empty()) std::fprintf(stderr, "перезагрузка не удалась: %s\n", loadError.c_str());
                watchFiles();
            }
        }

        std::vector<han::render::DrawItem> items;
        items.push_back({ground, Mat4::identity()});
        if (custom.id) items.push_back({custom.id, Mat4::translation({0, -custom.bounds.min.y, 0})});
        for (int x = -3; x <= 3; ++x)
            for (int z = -3; z <= 3; ++z) {
                if (x == 0 && z == 0) continue;
                const float spin = (x + z) % 2 == 0 ? t : 0.f;
                items.push_back({cube, Mat4::translation({x * 4.f, 0.5f, z * 4.f - 6.f}) * Mat4::rotationY(spin)});
            }
        items.push_back({pillar, Mat4::translation({0, 2.f, -6.f})});

        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        renderer->drawFrame(camera.viewProjection(aspect), items, w, h);
    }
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    return errors == 0 ? 0 : 1;
}
