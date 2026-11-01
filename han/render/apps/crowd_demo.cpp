// Демо E11: толпа в реальном районе (Мапо, данные OpenStreetMap): прохожие живут по расписанию, на драку и выстрелы реагируют.
// Управление: мышь — взгляд, W/A/S/D — полёт, Space/Ctrl — вверх/вниз, Shift — быстрее, Tab — захват мыши, ` — консоль, Esc — выход,
//   F — драка в точке, куда смотрит камера, G — выстрел там же, [ и ] — время суток ±1 ч, T — пауза / ход времени, P — показать пути.
// Запуск: han_crowd_demo.exe [--osm файл.osm] [--agents N] [--hour Ч] [--follow N] [--frames N] [--no-capture] [--no-ui] [--deterministic]
//         [--size W H] [--screenshot файл.png] [--camera x y z рыскание тангаж] [--event fight|gunshot x z кадр]
// Данные OSM (ODbL) не лежат в Git: python han/tools/pipeline/han_osm_fetch.py (карта © участники OpenStreetMap).
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "han/core/repo_root.h"
#include "han/ai/archetype.h"
#include "han/ai/crowd.h"
#include "han/ai/navgrid.h"
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
namespace ai = han::ai;

constexpr int kW = 26, kA = 4, kS = 22, kD = 7, kF = 9, kG = 10, kT = 23, kP = 19, kSpace = 44, kLCtrl = 224, kLShift = 225, kTab = 43, kEsc = 41, kGrave = 53, kLBracket = 47, kRBracket = 48;

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}

han::render::MeshData toMesh(const world::GeoMesh& g, float roughness) {
    han::render::MeshData m;
    m.vertices.reserve(g.vertices.size());
    for (const auto& v : g.vertices) {
        han::render::Vertex out{};
        for (int i = 0; i < 3; ++i) {
            out.position[i] = v.position[i];
            out.normal[i] = v.normal[i];
            out.color[i] = v.color[i];
        }
        m.vertices.push_back(out);
    }
    m.indices = g.indices;
    m.submeshes = {{0, static_cast<std::uint32_t>(m.indices.size()), -1, 0.0f, roughness}};
    return m;
}

struct CityLoader final : world::ICellLoader {
    han::render::Renderer* renderer = nullptr;
    world::StreamingManager* manager = nullptr;
    const std::map<world::CellCoord, world::CityChunk>* chunks = nullptr;
    struct Gpu {
        han::render::MeshId ground = 0, buildings = 0;
    };
    std::map<world::CellCoord, Gpu> gpu;
    void beginLoad(world::CellCoord cell) override {
        Gpu g;
        if (const auto it = chunks->find(cell); it != chunks->end()) {
            if (!it->second.ground.empty()) g.ground = renderer->uploadMesh(toMesh(it->second.ground, 0.9f));
            if (!it->second.buildings.empty()) g.buildings = renderer->uploadMesh(toMesh(it->second.buildings, 0.8f));
        }
        gpu[cell] = g;
        manager->onLoaded(cell, 0);
    }
    void unload(world::CellCoord cell) override {
        if (const auto it = gpu.find(cell); it != gpu.end()) {
            renderer->releaseMesh(it->second.ground);
            renderer->releaseMesh(it->second.buildings);
            gpu.erase(it);
        }
    }
};

// Цвет прохожего по архетипу (стабильно, различимо).
han::core::Vec3 archetypeColor(std::uint32_t numericId) {
    static const han::core::Vec3 palette[] = {{0.20f, 0.35f, 0.75f}, {0.85f, 0.30f, 0.25f}, {0.95f, 0.75f, 0.20f}, {0.25f, 0.70f, 0.40f}, {0.60f, 0.40f, 0.75f},
                                              {0.90f, 0.45f, 0.70f}, {0.25f, 0.75f, 0.80f}, {0.15f, 0.15f, 0.18f}, {0.80f, 0.55f, 0.30f}};
    return palette[numericId % (sizeof(palette) / sizeof(palette[0]))];
}
}  // namespace

int main(int argc, char** argv) {
    han::core::enterRepoRoot(argv[0]);  // запуск двойным щелчком из любой папки
    long maxFrames = -1;
    bool captured = true, noUi = false, deterministic = false;
    int windowSize[2] = {1280, 720};
    int agentCount = 400;
    float hourArg = 9.f, exposure = 0.55f;
    std::filesystem::path screenshotPath, osmPath = "assets_src/osm/mapo.osm";
    float cameraArgs[5] = {0.f, 90.f, 140.f, 0.f, -28.f};
    int followId = 0;          // --follow N: камера идёт за прохожим N (расстояние followDistance)
    float followDistance = 16.f;
    struct ScriptedEvent {
        ai::WorldEventType type = ai::WorldEventType::Fight;
        float x = 0, z = 0;
        long frame = -1;
    } scripted;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
        if (!std::strcmp(argv[i], "--no-ui")) noUi = true;
        if (!std::strcmp(argv[i], "--deterministic")) deterministic = true;
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--osm") && i + 1 < argc) osmPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--agents") && i + 1 < argc) agentCount = std::clamp(std::atoi(argv[i + 1]), 1, 5000);
        if (!std::strcmp(argv[i], "--hour") && i + 1 < argc) hourArg = static_cast<float>(std::atof(argv[i + 1]));
        if (!std::strcmp(argv[i], "--exposure") && i + 1 < argc) exposure = static_cast<float>(std::atof(argv[i + 1]));
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            windowSize[0] = std::max(64, std::atoi(argv[i + 1]));
            windowSize[1] = std::max(64, std::atoi(argv[i + 2]));
        }
        if (!std::strcmp(argv[i], "--follow") && i + 1 < argc) followId = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--follow-distance") && i + 1 < argc) followDistance = std::clamp(static_cast<float>(std::atof(argv[i + 1])), 3.f, 200.f);
        if (!std::strcmp(argv[i], "--camera") && i + 5 < argc)
            for (int k = 0; k < 5; ++k) cameraArgs[k] = static_cast<float>(std::atof(argv[i + 1 + k]));
        if (!std::strcmp(argv[i], "--event") && i + 4 < argc) {
            scripted.type = !std::strcmp(argv[i + 1], "gunshot") ? ai::WorldEventType::Gunshot : ai::WorldEventType::Fight;
            scripted.x = static_cast<float>(std::atof(argv[i + 2]));
            scripted.z = static_cast<float>(std::atof(argv[i + 3]));
            scripted.frame = std::atol(argv[i + 4]);
        }
    }

    // --- Город, навигация, архетипы ---
    world::OsmData osm;
    std::string error;
    if (!world::loadOsmFile(osmPath.string(), osm, error)) {
        std::fprintf(stderr, "%s\nСкачайте данные: python han/tools/pipeline/han_osm_fetch.py\n", error.c_str());
        return 1;
    }
    const world::GeoProjection projection(37.5572, 126.9230);
    world::CityStats cityStats;
    const auto chunks = world::buildCity(osm, projection, {}, &cityStats);
    const auto footprints = world::buildingFootprints(osm, projection);
    std::vector<std::vector<ai::Vec2>> obstacles;
    obstacles.reserve(footprints.size());
    for (const auto& f : footprints) {
        std::vector<ai::Vec2> poly;
        poly.reserve(f.size());
        for (const world::Vec2& p : f) poly.push_back({p.x, p.y});
        obstacles.push_back(std::move(poly));
    }
    const auto navStart = std::chrono::steady_clock::now();
    const ai::NavGrid grid = ai::NavGrid::build(cityStats.minX - 20.f, cityStats.minZ - 20.f, cityStats.maxX + 20.f, cityStats.maxZ + 20.f, obstacles);
    std::printf("город: домов %d, дорог %d; навигация: %dx%d клеток, проходимых %zu, построена за %.0f мс\n", cityStats.buildings, cityStats.roads, grid.width(),
                grid.height(), grid.walkableCount(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - navStart).count());
    if (!grid.valid()) {
        std::fprintf(stderr, "навигационная сетка не построена\n");
        return 1;
    }
    ai::ArchetypeLibrary library;
    if (!library.loadDirectory("data/people/archetypes", error)) {
        std::fprintf(stderr, "архетипы: %s\n", error.c_str());
        return 1;
    }
    // Места по категориям: центры случайных зданий (в демо категории назначаются по номеру, у игры — из данных мира).
    static const char* categories[] = {"work", "cafe", "shop", "eat", "park", "campus", "market", "nightlife", "sights", "delivery", "security"};
    std::vector<ai::Poi> pois;
    for (std::size_t i = 0; i < footprints.size(); i += 5) {
        float cx = 0, cz = 0;
        for (const world::Vec2& p : footprints[i]) {
            cx += p.x;
            cz += p.y;
        }
        cx /= static_cast<float>(footprints[i].size());
        cz /= static_cast<float>(footprints[i].size());
        ai::Vec2 door;
        if (grid.nearestWalkable({cx, cz}, 30.f, door)) pois.push_back({door, categories[(i / 5) % 11]});
    }
    ai::CrowdSettings cs;
    cs.maxAgents = static_cast<std::size_t>(agentCount);
    ai::Crowd crowd(grid, pois, cs);
    // Население — рядом со входами в здания (за пределами скачанных данных пусто, там ходить не к чему).
    {
        const auto archetypes = library.pointers();
        std::uint32_t rng = 987654321u;
        const auto next = [&] {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float>(rng & 0xFFFFFFu) / static_cast<float>(0x1000000u);
        };
        for (int i = 0; i < agentCount && !pois.empty(); ++i) {
            const ai::Vec2 base = pois[static_cast<std::size_t>(next() * static_cast<float>(pois.size())) % pois.size()].pos;
            crowd.spawn(*archetypes[static_cast<std::size_t>(i) % archetypes.size()], {base.x + (next() - 0.5f) * 40.f, base.z + (next() - 0.5f) * 40.f});
        }
    }
    crowd.setHour(hourArg);
    std::printf("прохожих: %zu, мест: %zu, архетипов: %zu\n", crowd.agents().size(), pois.size(), library.size());

    auto window = han::platform::Window::create("HAN: Fists of Seoul — демо E11 (толпа)", windowSize[0], windowSize[1]);
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

    auto groundData = han::render::makePlane(4000.f, {0.40f, 0.42f, 0.38f});
    groundData.submeshes = {{0, static_cast<std::uint32_t>(groundData.indices.size()), -1, 0.0f, 0.95f}};
    const auto ground = renderer->uploadMesh(groundData);
    std::map<std::uint32_t, han::render::MeshId> bodyMeshes;
    for (const ai::Archetype& a : library.all()) {
        auto body = han::render::makeBox({0.28f, 0.85f, 0.2f}, archetypeColor(a.numericId));
        body.submeshes = {{0, static_cast<std::uint32_t>(body.indices.size()), -1, 0.0f, 0.7f}};
        bodyMeshes[a.numericId] = renderer->uploadMesh(body);
    }
    auto markerData = han::render::makeSphere(1.2f, 16, 8, {1.0f, 0.15f, 0.1f});
    markerData.submeshes = {{0, static_cast<std::uint32_t>(markerData.indices.size()), -1, 0.0f, 0.4f}};
    const auto marker = renderer->uploadMesh(markerData);
    if (!ground || !marker) {
        std::fprintf(stderr, "не удалось загрузить меши\n");
        return 1;
    }

    world::StreamingSettings ss;
    ss.cellSize = 256.f;
    ss.loadRadius = 900.f;
    ss.unloadRadius = 1200.f;
    ss.hlodRadius = 1200.f;
    ss.keepRadius = 300.f;
    ss.maxNewLoadsPerFrame = 3;
    CityLoader loader;
    loader.renderer = renderer.get();
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
    input.bind("fight", kF);
    input.bind("shot", kG);
    input.bind("hour_up", kRBracket);
    input.bind("hour_down", kLBracket);
    input.bind("time", kT);
    input.bind("paths", kP);

    han::render::Camera camera;
    camera.position = {cameraArgs[0], cameraArgs[1], cameraArgs[2]};
    camera.yaw = cameraArgs[3] * 0.01745329f;
    camera.pitch = cameraArgs[4] * 0.01745329f;
    camera.farZ = 4000.f;
    float speed = 30.f;
    bool quit = false, consoleOpen = false, timeFlows = true, showPaths = false;
    float hour = hourArg, sunKelvin = 5200.f;
    std::vector<std::string> recentLines;
    ai::Vec2 lastEvent{0, 0};
    float markerTime = 0;

    han::core::Console console;
    console.registerCommand("quit", "закрыть демо", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("выход");
    });
    console.registerCommand("hour", "hour часы — время суток (0..24)", [&](const std::vector<std::string>& a) {
        if (a.size() == 1) hour = std::fmod(std::fmod(static_cast<float>(std::atof(a[0].c_str())), 24.f) + 24.f, 24.f);
        return fmt("время: %.1f ч", hour);
    });
    console.registerCommand("speed", "speed число — скорость камеры", [&](const std::vector<std::string>& a) {
        if (a.size() == 1) speed = std::clamp(static_cast<float>(std::atof(a[0].c_str())), 1.f, 1000.f);
        return fmt("скорость: %.0f", speed);
    });

    window->setCaptureMouse(captured);
    const Uint64 start = SDL_GetTicksNS();
    Uint64 last = start;
    double fps = 0, crowdMs = 0;
    bool screenshotFailed = false;
    world::StreamingStats sstats;
    int policeCalls = 0, filmed = 0;
    for (long frame = 0; (maxFrames < 0 || frame < maxFrames) && !quit; ++frame) {
        const Uint64 now = deterministic ? start + static_cast<Uint64>(frame) * 16666667ull : SDL_GetTicksNS();
        const float dt = deterministic ? 1.0f / 60.0f : std::min(static_cast<float>(static_cast<double>(now - last) * 1e-9), 0.1f);
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
            if (input.pressed("hour_up")) hour = std::fmod(hour + 1.f, 24.f);
            if (input.pressed("hour_down")) hour = std::fmod(hour + 23.f, 24.f);
            if (input.pressed("time")) timeFlows = !timeFlows;
            if (input.pressed("paths")) showPaths = !showPaths;
        }
        if (captured && !consoleOpen && !uiMouse) camera.look(ev.mouseDx, ev.mouseDy);
        const auto axis = [&](const char* pos, const char* neg) { return uiKeys ? 0.f : (input.held(pos) ? 1.f : 0.f) - (input.held(neg) ? 1.f : 0.f); };
        camera.move(axis("forward", "back"), axis("right", "left"), axis("up", "down"), input.held("fast") && !uiKeys ? speed * 4.f : speed, dt);

        // События: клавиши или сценарий из командной строки; точка — где взгляд камеры пересекает землю.
        if (followId > 0 && crowd.agent(static_cast<ai::AgentId>(followId))) {
            const ai::Agent& target = *crowd.agent(static_cast<ai::AgentId>(followId));
            const auto look = camera.forward();
            camera.position = {target.pos.x - look.x * followDistance, 1.5f - look.y * followDistance, target.pos.z - look.z * followDistance};
        }
        const auto fwd = camera.forward();
        const auto fire = [&](ai::WorldEventType type, ai::Vec2 where) {
            crowd.postEvent({type, where, type == ai::WorldEventType::Gunshot ? 90.f : 60.f});
            lastEvent = where;
            markerTime = 2.5f;
        };
        if (!uiKeys && fwd.y < -0.02f) {
            const float t = -camera.position.y / fwd.y;
            const ai::Vec2 target{camera.position.x + fwd.x * t, camera.position.z + fwd.z * t};
            if (input.pressed("fight")) fire(ai::WorldEventType::Fight, target);
            if (input.pressed("shot")) fire(ai::WorldEventType::Gunshot, target);
        }
        if (scripted.frame >= 0 && frame == scripted.frame) {
            // Координаты 0 0 — «на месте прохожего №1» (удобно для проверки без знания карты).
            const bool atAgent = scripted.x == 0.f && scripted.z == 0.f && crowd.agent(1);
            fire(scripted.type, atAgent ? crowd.agent(1)->pos : ai::Vec2{scripted.x, scripted.z});
        }
        markerTime = std::max(0.f, markerTime - dt);

        if (timeFlows) hour = std::fmod(hour + dt * (24.f / 600.f), 24.f);  // сутки за 10 минут
        crowd.setHour(hour);
        const auto c0 = std::chrono::steady_clock::now();
        crowd.update(dt, {camera.position.x, camera.position.z});
        crowdMs = crowdMs * 0.95 + std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count() * 0.05;
        for (const ai::Utterance& u : crowd.drainUtterances()) {
            recentLines.push_back(u.key);
            if (recentLines.size() > 4) recentLines.erase(recentLines.begin());
        }
        for (const ai::CrowdEvent& e : crowd.drainEvents()) (e.type == ai::CrowdEvent::Type::PoliceCalled ? policeCalls : filmed)++;
        sstats = streaming.update(camera.position.x, camera.position.z, fwd.x, fwd.z);

        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        const Mat4 viewProjection = camera.viewProjection(aspect);

#ifdef HAN_WITH_IMGUI
        if (!noUi) {
            const ai::CrowdStats& cst = crowd.stats();
            std::vector<std::string> lines = {fmt("FPS: %.0f", fps), "GPU: " + renderer->deviceName(), fmt("ошибок валидации: %.0f", renderer->validationErrors()),
                                              fmt("время: %.1f ч", hour) + (timeFlows ? "" : " (пауза)"),
                                              fmt("прохожих: %.0f (рядом %.0f, средне %.0f", cst.agents, cst.near, cst.mid) + fmt(", далеко %.0f, вдали %.0f)", cst.far, cst.dormant),
                                              fmt("в пути: %.0f, запросов пути: %.0f, ждут: %.0f", cst.moving, cst.pathQueries, cst.pendingPaths),
                                              fmt("толпа: %.2f мс", crowdMs), fmt("вызовов полиции: %.0f, снимают на камеру: %.0f", policeCalls, filmed),
                                              "F — драка, G — выстрел, [ ] — время, T — пауза времени, P — пути"};
            for (const std::string& l : recentLines) lines.push_back("«" + l + "»");
            ui.drawStats(lines);
            ui.drawConsole(console, consoleOpen);
            if (showPaths) {
                std::vector<han::ui::ImGuiLayer::Line3D> out;
                for (const ai::Agent& a : crowd.agents()) {
                    if (a.lod != ai::LodTier::Near || a.path.empty()) continue;
                    ai::Vec2 prev = a.pos;
                    for (std::size_t i = a.pathIndex; i < a.path.size(); ++i) {
                        out.push_back({{prev.x, 0.3f, prev.z}, {a.path[i].x, 0.3f, a.path[i].z}, a.state == ai::AgentState::Fleeing ? 0xFF3030FFu : 0xFF50E678u});
                        prev = a.path[i];
                    }
                }
                ui.drawLines(out, viewProjection, w, h);
            }
        }
#endif

        std::vector<han::render::DrawItem> items;
        items.push_back({ground, Mat4::identity()});
        for (const auto& [cell, g] : loader.gpu) {
            if (g.ground) items.push_back({g.ground, Mat4::identity()});
            if (g.buildings) items.push_back({g.buildings, Mat4::identity()});
        }
        for (const ai::Agent& a : crowd.agents()) {
            if (a.lod == ai::LodTier::Dormant) continue;  // дальше farRadius не рисуем
            const auto it = bodyMeshes.find(a.archetype->numericId);
            if (it == bodyMeshes.end() || !it->second) continue;
            // Бегущие и реагирующие слегка выше «ростом» (виден переполох): падение/наклон будут в анимации (A1, зона B).
            items.push_back({it->second, Mat4::translation({a.pos.x, 0.85f, a.pos.z}) * Mat4::rotationY(-a.heading)});
        }
        if (markerTime > 0.f) items.push_back({marker, Mat4::translation({lastEvent.x, 1.2f, lastEvent.z})});

        han::render::FrameParams params;
        params.viewProjection = viewProjection;
        params.cameraPosition = camera.position;
        const float sunHeight = std::sin((hour - 6.f) / 12.f * 3.1415926f) * 70.f;  // 6 ч — восход, 18 ч — закат
        params.sun = han::render::makeSun(std::max(sunHeight, -10.f), 100.f + hour * 7.5f, sunHeight > 15.f ? sunKelvin : 3200.f);
        params.sky = han::render::skyFromSun(params.sun);
        params.exposure = sunHeight < 0.f ? exposure * 4.f : exposure;
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
    const ai::CrowdStats& cst = crowd.stats();
    int fleeing = 0, watching = 0;
    for (const ai::Agent& a : crowd.agents()) {
        fleeing += a.state == ai::AgentState::Fleeing ? 1 : 0;
        watching += (a.state == ai::AgentState::Watching || a.state == ai::AgentState::Filming) ? 1 : 0;
    }
    std::printf("прохожих %d (в пути %d, бегут %d, смотрят %d), вызовов полиции %d, снимают %d, толпа %.2f мс/кадр\n", cst.agents, cst.moving, fleeing, watching,
                policeCalls, filmed, crowdMs);
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    if (screenshotFailed) return 2;
    return errors == 0 ? 0 : 1;
}
