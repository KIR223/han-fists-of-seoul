// Демо E10: город из данных OpenStreetMap (по умолчанию Мапо: Хондэ), ячейки подгружаются по мере полёта (E9).
// Управление: мышь — взгляд, W/A/S/D — полёт, Space/Ctrl — вверх/вниз, Shift — быстрее, Tab — захват мыши, ` — консоль, Esc — выход.
// Запуск: han_city_demo.exe [--osm файл.osm] [--origin широта долгота] [--frames N] [--no-capture] [--no-ui] [--deterministic]
//         [--size W H] [--screenshot файл.png] [--camera x y z рыскание тангаж] [--sun высота азимут кельвины] [--wet 0..1] [--traffic число машин, 0 — выключить]
// Данные OSM (ODbL) не лежат в Git: python han/tools/pipeline/han_osm_fetch.py (карта © участники OpenStreetMap).
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "city_common.h"
#include "han/ai/traffic.h"
#include "han/core/repo_root.h"
#include "han/core/console.h"
#include "han/platform/window.h"
#include "han/render/camera.h"
#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#include "han/world/city_builder.h"
#include "han/world/streaming.h"
#ifdef HAN_WITH_IMGUI
#include "han/ui/imgui_layer.h"
#endif

namespace {
using han::core::Mat4;
namespace world = han::world;

constexpr int kW = 26, kA = 4, kS = 22, kD = 7, kSpace = 44, kLCtrl = 224, kLShift = 225, kTab = 43, kEsc = 41, kGrave = 53;

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}

// Загрузчик ячеек: строит и заливает в видеопамять меши ячейки; выгрузка освобождает их.
struct CityLoader final : world::ICellLoader {
    han::render::Renderer* renderer = nullptr;
    std::vector<han::render::TextureId> textureIds;  // альбедо материалов города (0 — нет файла)
    world::StreamingManager* manager = nullptr;
    const std::map<world::CellCoord, world::CityChunk>* chunks = nullptr;
    struct Gpu {
        han::render::MeshId ground = 0, buildings = 0;
        std::size_t triangles = 0;
    };
    std::map<world::CellCoord, Gpu> gpu;
    std::size_t uploadedTriangles = 0;

    void beginLoad(world::CellCoord cell) override {
        Gpu g;
        const auto it = chunks->find(cell);
        if (it != chunks->end()) {
            if (!it->second.ground.empty()) g.ground = renderer->uploadMesh(han::render::citytex::toMesh(it->second.ground, textureIds, 0.9f), textureIds);
            if (!it->second.buildings.empty()) g.buildings = renderer->uploadMesh(han::render::citytex::toMesh(it->second.buildings, textureIds, 0.8f), textureIds);
            g.triangles = (it->second.ground.indices.size() + it->second.buildings.indices.size()) / 3;
        }
        uploadedTriangles += g.triangles;
        gpu[cell] = g;
        manager->onLoaded(cell, g.triangles * 3 * sizeof(world::GeoVertex));  // загрузка синхронная: результат сразу
    }
    void unload(world::CellCoord cell) override {
        const auto it = gpu.find(cell);
        if (it == gpu.end()) return;
        renderer->releaseMesh(it->second.ground);
        renderer->releaseMesh(it->second.buildings);
        uploadedTriangles -= it->second.triangles;
        gpu.erase(it);
    }
};
}  // namespace

int main(int argc, char** argv) {
    float wetness = 0.f;
    int trafficCars = 120;
    han::core::enterRepoRoot(argv[0]);  // запуск двойным щелчком из любой папки
    long maxFrames = -1;
    bool captured = true, noUi = false, deterministic = false;
    int windowSize[2] = {1280, 720};
    std::filesystem::path screenshotPath, osmPath = "assets_src/osm/mapo.osm";
    double origin[2] = {37.5572, 126.9230};  // Хондэ: перекрёсток у станции метро
    float cameraArgs[5] = {0.f, 70.f, 120.f, 0.f, -22.f};
    float sunArgs[3] = {38.f, 125.f, 5200.f};
    float exposureArg = 0.55f;  // город светлый: сцена с яркими стенами требует экспозиции ниже единицы
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
        if (!std::strcmp(argv[i], "--no-ui")) noUi = true;
        if (!std::strcmp(argv[i], "--deterministic")) deterministic = true;
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--osm") && i + 1 < argc) osmPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--origin") && i + 2 < argc) {
            origin[0] = std::atof(argv[i + 1]);
            origin[1] = std::atof(argv[i + 2]);
        }
        if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            windowSize[0] = std::max(64, std::atoi(argv[i + 1]));
            windowSize[1] = std::max(64, std::atoi(argv[i + 2]));
        }
        if (!std::strcmp(argv[i], "--exposure") && i + 1 < argc) exposureArg = static_cast<float>(std::atof(argv[i + 1]));
        if (!std::strcmp(argv[i], "--camera") && i + 5 < argc)
            for (int k = 0; k < 5; ++k) cameraArgs[k] = static_cast<float>(std::atof(argv[i + 1 + k]));
        if (!std::strcmp(argv[i], "--traffic") && i + 1 < argc) trafficCars = std::clamp(std::atoi(argv[i + 1]), 0, 400);
        if (!std::strcmp(argv[i], "--wet") && i + 1 < argc) wetness = std::clamp(static_cast<float>(std::atof(argv[i + 1])), 0.f, 1.f);
        if (!std::strcmp(argv[i], "--sun") && i + 3 < argc)
            for (int k = 0; k < 3; ++k) sunArgs[k] = static_cast<float>(std::atof(argv[i + 1 + k]));
    }

    // --- Данные города ---
    world::OsmData osm;
    std::string error;
    if (!world::loadOsmFile(osmPath.string(), osm, error)) {
        std::fprintf(stderr, "%s\nСкачайте данные: python han/tools/pipeline/han_osm_fetch.py\n", error.c_str());
        return 1;
    }
    world::CityStats cityStats;
    const auto chunks = world::buildCity(osm, world::GeoProjection(origin[0], origin[1]), {}, &cityStats);
    // Трафик (G7): граф дорог из тех же данных; машины едут вокруг камеры (появляются в радиусе 350 м, уходят за 550 м).
    const han::ai::RoadGraph roads = han::ai::RoadGraph::build(osm, world::GeoProjection(origin[0], origin[1]));
    han::ai::TrafficSettings trafficSettings;
    trafficSettings.maxCars = trafficCars;
    trafficSettings.spawnRadius = 350.f;
    trafficSettings.despawnRadius = 550.f;
    han::ai::TrafficSim traffic(roads, trafficSettings);
    std::printf("город: домов %d, дорог %d, парков %d, воды %d, пропущено %d, ячеек %zu, размер %.0f x %.0f м\n", cityStats.buildings, cityStats.roads,
                cityStats.parks, cityStats.waters, cityStats.skipped, chunks.size(), cityStats.maxX - cityStats.minX, cityStats.maxZ - cityStats.minZ);

    auto window = han::platform::Window::create("HAN: Fists of Seoul — демо E10 (город)", windowSize[0], windowSize[1]);
    if (!window) {
        std::fprintf(stderr, "не удалось создать окно\n");
        return 1;
    }
#ifdef HAN_WITH_IMGUI
    han::ui::ImGuiLayer ui;
    if (!noUi && !ui.init(*window)) {
        std::fprintf(stderr, "не удалось запустить ImGui\n");
        return 1;
    }
#endif
    han::render::RendererDesc desc;
    desc.instanceExtensions = window->vulkanInstanceExtensions();
    desc.createSurface = [&](void* instance) { return window->createVulkanSurface(instance); };
    window->pixelSize(desc.width, desc.height);
    auto renderer = han::render::Renderer::create(desc, error);
    if (!renderer) {
        std::fprintf(stderr, "рендерер: %s\n", error.c_str());
        return 1;
    }
#ifdef HAN_WITH_IMGUI
    if (!noUi) renderer->setOverlay(&ui);
#endif
    std::printf("GPU: %s; слой валидации: %s\n", renderer->deviceName().c_str(), renderer->validationEnabled() ? "включён" : "НЕ найден");

    // Земля вокруг: большая плоскость под городом (дороги и дома лежат выше неё).
    const auto ground = han::render::citytex::uploadGround(*renderer);
    // Машины трафика: кузов-коробка 1,8 x 1,4 x 4,4 м, шесть цветов (выдуманная «марка»: без реальных машин и логотипов).
    std::vector<han::render::MeshId> carMeshes;
    for (const han::core::Vec3 color : {han::core::Vec3{0.75f, 0.75f, 0.78f}, {0.12f, 0.12f, 0.14f}, {0.70f, 0.12f, 0.12f}, {0.15f, 0.30f, 0.62f}, {0.85f, 0.80f, 0.70f}, {0.20f, 0.45f, 0.30f}}) {
        auto body = han::render::makeBox({0.9f, 0.7f, 2.2f}, color);
        body.submeshes = {{0, static_cast<std::uint32_t>(body.indices.size()), -1, 0.3f, 0.35f}};
        carMeshes.push_back(renderer->uploadMesh(body));
    }
    if (!ground) {
        std::fprintf(stderr, "не удалось загрузить землю\n");
        return 1;
    }

    // --- Стриминг ячеек ---
    world::StreamingSettings ss;
    ss.cellSize = 256.f;
    ss.loadRadius = 900.f;
    ss.unloadRadius = 1200.f;
    ss.hlodRadius = 1200.f;
    ss.keepRadius = 300.f;
    ss.maxNewLoadsPerFrame = 3;
    ss.maxInFlight = 8;
    CityLoader loader;
    loader.renderer = renderer.get();
    loader.textureIds = han::render::citytex::load(*renderer);
    loader.chunks = &chunks;
    world::StreamingManager streaming(ss, loader);
    loader.manager = &streaming;

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
    camera.position = {cameraArgs[0], cameraArgs[1], cameraArgs[2]};
    camera.yaw = cameraArgs[3] * 0.01745329f;
    camera.pitch = cameraArgs[4] * 0.01745329f;
    camera.farZ = 4000.f;
    camera.nearZ = 0.2f;
    float exposure = exposureArg, speed = 40.f, sunElevation = sunArgs[0], sunAzimuth = sunArgs[1], sunKelvin = sunArgs[2];
    bool quit = false, consoleOpen = false;

    han::core::Console console;
    console.registerCommand("quit", "закрыть демо", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("выход");
    });
    console.registerCommand("speed", "speed число — скорость камеры, м/с", [&](const std::vector<std::string>& a) {
        if (a.size() == 1) speed = std::clamp(static_cast<float>(std::atof(a[0].c_str())), 1.f, 2000.f);
        return fmt("скорость: %.0f", speed);
    });
    console.registerCommand("tp", "tp x y z — переместить камеру", [&](const std::vector<std::string>& a) {
        if (a.size() != 3) return std::string("использование: tp x y z");
        camera.position = {static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str()))};
        return std::string("готово");
    });
    console.registerCommand("exposure", "exposure число — яркость", [&](const std::vector<std::string>& a) {
        if (a.size() == 1) exposure = std::clamp(static_cast<float>(std::atof(a[0].c_str())), 0.05f, 20.f);
        return fmt("экспозиция: %.2f", exposure);
    });
    console.registerCommand("sun", "sun высота азимут [кельвины]", [&](const std::vector<std::string>& a) {
        if (a.size() >= 2) {
            sunElevation = std::clamp(static_cast<float>(std::atof(a[0].c_str())), -90.f, 90.f);
            sunAzimuth = static_cast<float>(std::atof(a[1].c_str()));
            if (a.size() == 3) sunKelvin = std::clamp(static_cast<float>(std::atof(a[2].c_str())), 1000.f, 40000.f);
        }
        return fmt("солнце: высота %.1f, азимут %.1f", sunElevation, sunAzimuth);
    });

    window->setCaptureMouse(captured);
    const Uint64 start = SDL_GetTicksNS();
    Uint64 last = start;
    double fps = 0;
    bool screenshotFailed = false;
    world::StreamingStats stats;
    for (long frame = 0; (maxFrames < 0 || frame < maxFrames) && !quit; ++frame) {
        const Uint64 now = deterministic ? start + static_cast<Uint64>(frame) * 16666667ull : SDL_GetTicksNS();
        const float dt = deterministic ? 1.0f / 60.0f : static_cast<float>(static_cast<double>(now - last) * 1e-9);
        last = now;
        if (dt > 0) fps = fps * 0.95 + (1.0 / dt) * 0.05;

        input.beginFrame(static_cast<double>(now) * 1e-9);
        const auto ev = window->pump(input);
        if (ev.quitRequested) break;
#ifdef HAN_WITH_IMGUI
        if (!noUi) ui.beginFrame();
        const bool uiKeys = !noUi && ui.wantsKeyboard();
        const bool uiMouse = !noUi && ui.wantsMouse();
#else
        const bool uiKeys = false, uiMouse = false;
#endif
        if (input.pressed("console")) {
            consoleOpen = !consoleOpen;
            window->setCaptureMouse(!consoleOpen && captured);
        }
        if (!uiKeys) {
            if (input.pressed("quit")) break;
            if (input.pressed("capture") && !consoleOpen) window->setCaptureMouse(captured = !captured);
        }
        if (captured && !consoleOpen && !uiMouse) camera.look(ev.mouseDx, ev.mouseDy);
        const auto axis = [&](const char* pos, const char* neg) { return uiKeys ? 0.f : (input.held(pos) ? 1.f : 0.f) - (input.held(neg) ? 1.f : 0.f); };
        camera.move(axis("forward", "back"), axis("right", "left"), axis("up", "down"), input.held("fast") && !uiKeys ? speed * 4.f : speed, dt);

        const auto fwd = camera.forward();
        stats = streaming.update(camera.position.x, camera.position.z, fwd.x, fwd.z);

        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        const Mat4 viewProjection = camera.viewProjection(aspect);

#ifdef HAN_WITH_IMGUI
        if (!noUi) {
            ui.drawStats({fmt("FPS: %.0f", fps), "GPU: " + renderer->deviceName(), fmt("ошибок валидации: %.0f", renderer->validationErrors()),
                          fmt("камера: %.0f %.0f %.0f", camera.position.x, camera.position.y, camera.position.z),
                          fmt("ячеек загружено: %.0f, треугольников: %.0f", stats.loaded, static_cast<double>(loader.uploadedTriangles)),
                          "© участники OpenStreetMap (ODbL)"});
            ui.drawConsole(console, consoleOpen);
        }
#endif

        if (trafficCars > 0) {
            traffic.setFocus({camera.position.x, camera.position.z});
            traffic.update(std::min(dt, 0.1f));
        }
        std::vector<han::render::DrawItem> items;
        for (const han::ai::Car& car : traffic.cars()) {
            const std::size_t color = car.id % carMeshes.size();
            items.push_back({carMeshes[color], Mat4::translation({car.pos.x, 0.85f, car.pos.z}) * Mat4::rotationY(car.heading)});
        }
        items.push_back({ground, Mat4::translation({0.f, -0.25f, 0.f})});  // ниже дорог: иначе z-борьба на расстоянии
        for (const auto& [cell, g] : loader.gpu) {
            if (g.ground) items.push_back({g.ground, Mat4::identity()});
            if (g.buildings) items.push_back({g.buildings, Mat4::identity()});
        }

        han::render::FrameParams params;
        params.viewProjection = viewProjection;
        params.cameraPosition = camera.position;
        params.sun = han::render::makeSun(sunElevation, sunAzimuth, sunKelvin);
        params.sky = han::render::skyFromSun(params.sun);
        params.wetness = wetness;
        if (sunElevation < 6.f) {  // ночью и в сумерки горят ближайшие 32 фонаря (натриевый тёплый свет)
            struct Near {
                float d2;
                const world::Vec3* p;
            };
            std::vector<Near> near;
            for (const auto& [cell, chunk] : chunks)
                for (const world::Vec3& lamp : chunk.lamps) {
                    const float dx = lamp.x - camera.position.x, dz = lamp.z - camera.position.z;
                    if (dx * dx + dz * dz < 250.f * 250.f) near.push_back({dx * dx + dz * dz + (lamp.y - camera.position.y) * (lamp.y - camera.position.y), &lamp});
                }
            const std::size_t keep = std::min<std::size_t>(near.size(), 32);
            std::partial_sort(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(keep), near.end(), [](const Near& a, const Near& b) { return a.d2 < b.d2; });
            const float k = std::clamp((6.f - sunElevation) / 8.f, 0.f, 1.f);  // плавное включение на закате
            for (std::size_t i = 0; i < keep; ++i)
                params.pointLights.push_back({{near[i].p->x, near[i].p->y, near[i].p->z}, {k * 60.f, k * 42.f, k * 22.f}, 24.f});
        }
        params.exposure = exposure;
        params.fogDensity = 0.0006f;
        params.nearPlane = camera.nearZ;
        params.farPlane = camera.farZ;
        params.shadows = true;
        params.taa = false;
        params.bloomIntensity = 0.05f;
        const bool shootNow = !screenshotPath.empty() && maxFrames > 0 && frame == maxFrames - 1;
        if (shootNow && !renderer->requestScreenshot(screenshotPath)) {
            std::fprintf(stderr, "снимок экрана невозможен\n");
            screenshotFailed = true;
        }
        renderer->drawFrame(params, items, w, h);
        if (shootNow && !screenshotFailed && (renderer->screenshotPending() || !renderer->screenshotError().empty())) {
            std::fprintf(stderr, "снимок экрана не сохранён: %s\n", renderer->screenshotError().c_str());
            screenshotFailed = true;
        }
    }
    std::printf("ячеек загружено: %d, треугольников: %zu\n", stats.loaded, loader.uploadedTriangles);
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    if (screenshotFailed) return 2;
    return errors == 0 ? 0 : 1;
}
