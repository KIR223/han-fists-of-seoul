// Демо E6: бег от третьего лица по полигону с паркуром (перепрыгивание ящика, запрыгивание на уступ, лестница, рампа).
// Управление: мышь — камера, W/A/S/D — бег (относительно камеры), Shift — спринт, Space — прыжок / паркур у препятствия,
// R — вернуться на старт, F1 — контуры физики, Tab — захват мыши, ` — консоль, Esc — выход.
// Запуск: han_player_demo.exe [--auto] [--frames N] [--no-capture] [--no-ui] [--deterministic] [--size W H] [--screenshot файл.png]
//   --auto: персонаж сам пробегает маршрут: ящик (перепрыгнуть), уступ 1,8 м (запрыгнуть).
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "han/core/console.h"
#include "han/physics/physics_world.h"
#include "han/platform/window.h"
#include "han/player/camera_rig.h"
#include "han/player/player_controller.h"
#include "han/render/camera.h"
#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#ifdef HAN_WITH_IMGUI
#include "han/ui/imgui_layer.h"
#endif

namespace {
using han::core::Mat4;
namespace phys = han::physics;
namespace contracts = han::contracts;
namespace pl = han::player;

constexpr int kW = 26, kA = 4, kS = 22, kD = 7, kR = 21, kSpace = 44, kLShift = 225, kTab = 43, kEsc = 41, kGrave = 53, kF1 = 58;

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}

Mat4 quatMatrix(contracts::Vec3 p, contracts::Quat q) {
    Mat4 r;
    r.m[0] = 1 - 2 * (q.y * q.y + q.z * q.z);
    r.m[1] = 2 * (q.x * q.y + q.z * q.w);
    r.m[2] = 2 * (q.x * q.z - q.y * q.w);
    r.m[4] = 2 * (q.x * q.y - q.z * q.w);
    r.m[5] = 1 - 2 * (q.x * q.x + q.z * q.z);
    r.m[6] = 2 * (q.y * q.z + q.x * q.w);
    r.m[8] = 2 * (q.x * q.z + q.y * q.w);
    r.m[9] = 2 * (q.y * q.z - q.x * q.w);
    r.m[10] = 1 - 2 * (q.x * q.x + q.y * q.y);
    r.m[12] = p.x;
    r.m[13] = p.y;
    r.m[14] = p.z;
    return r;
}
}  // namespace

int main(int argc, char** argv) {
    long maxFrames = -1;
    bool captured = true, noUi = false, deterministic = false, debugDraw = false, autoRun = false;
    int windowSize[2] = {1280, 720};
    std::filesystem::path screenshotPath;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
        if (!std::strcmp(argv[i], "--no-ui")) noUi = true;
        if (!std::strcmp(argv[i], "--deterministic")) deterministic = true;
        if (!std::strcmp(argv[i], "--auto")) autoRun = true;
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            windowSize[0] = std::max(64, std::atoi(argv[i + 1]));
            windowSize[1] = std::max(64, std::atoi(argv[i + 2]));
        }
    }

    auto world = phys::PhysicsWorld::create();
    if (!world) {
        std::fprintf(stderr, "физика недоступна: модуль собран без Jolt\n");
        return 1;
    }
    auto window = han::platform::Window::create("HAN: Fists of Seoul — демо E6 (игрок и камера)", windowSize[0], windowSize[1]);
    if (!window) return 1;
#ifdef HAN_WITH_IMGUI
    han::ui::ImGuiLayer ui;
    if (!noUi && !ui.init(*window)) return 1;
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
#ifdef HAN_WITH_IMGUI
    if (!noUi) renderer->setOverlay(&ui);
#endif
    std::printf("GPU: %s; слой валидации: %s\n", renderer->deviceName().c_str(), renderer->validationEnabled() ? "включён" : "НЕ найден");

    // --- Графика ---
    auto checker = han::render::makeCheckerTexture(256, 32, 0xFFD0D0D0, 0xFF606060);
    han::render::generateMips(checker);
    const han::render::TextureId checkerId = renderer->uploadTexture(checker);
    const std::vector<han::render::TextureId> checkerSlots{checkerId};
    auto groundData = han::render::makePlane(150.f, {0.55f, 0.75f, 0.55f}, 75.f);
    groundData.textures = {"checker"};
    groundData.submeshes = {{0, static_cast<std::uint32_t>(groundData.indices.size()), 0, 0.0f, 0.9f}};
    const auto material = [](han::render::MeshData m, float metallic, float roughness) {
        m.submeshes = {{0, static_cast<std::uint32_t>(m.indices.size()), -1, metallic, roughness}};
        return m;
    };
    const auto ground = renderer->uploadMesh(groundData, checkerSlots);
    const auto heroMesh = renderer->uploadMesh(material(han::render::makeBox({0.28f, 0.9f, 0.2f}, {0.95f, 0.8f, 0.2f}), 0.0f, 0.6f));
    if (!ground || !heroMesh) return 1;

    // --- Полигон: каждый блок — тело физики и меш ---
    struct Block {
        han::render::MeshId mesh;
        Mat4 transform;
    };
    std::vector<Block> blocks;
    world->addStaticBox({0, -0.5f, 0}, {150, 0.5f, 150}, contracts::SurfaceMaterial::Asphalt);
    const auto addBlock = [&](contracts::Vec3 center, contracts::Vec3 half, han::core::Vec3 color, contracts::Quat rot = {}) {
        world->addStaticBox(center, half, contracts::SurfaceMaterial::Concrete, rot);
        blocks.push_back({renderer->uploadMesh(material(han::render::makeBox({half.x, half.y, half.z}, color), 0.0f, 0.8f)), quatMatrix(center, rot)});
    };
    addBlock({0, 0.5f, -10.f}, {3.f, 0.5f, 0.4f}, {0.85f, 0.45f, 0.35f});    // ящик 1,0 м: перепрыгнуть
    addBlock({0, 0.9f, -22.f}, {3.f, 0.9f, 2.f}, {0.6f, 0.62f, 0.7f});        // уступ 1,8 м: запрыгнуть
    addBlock({8, 1.5f, -14.f}, {3.f, 1.5f, 0.5f}, {0.8f, 0.8f, 0.82f});       // стена 3 м: не берётся
    for (int i = 0; i < 6; ++i) {                                              // лестница справа
        const float hh = 0.075f * static_cast<float>(i + 1);
        addBlock({-8.f, hh, -4.f - 0.6f * static_cast<float>(i)}, {2.f, hh, 0.3f}, {0.75f, 0.72f, 0.68f});
    }
    addBlock({-8.f, 0.45f, -12.3f}, {2.f, 0.45f, 3.f}, {0.7f, 0.68f, 0.64f});
    const float tilt = 0.5f * 15.f * 0.01745329f;
    addBlock({-16.f, 0.7f, -6.f}, {2.f, 0.1f, 4.f}, {0.7f, 0.55f, 0.4f}, {std::sin(tilt), 0.f, 0.f, std::cos(tilt)});  // рампа

    const contracts::Vec3 spawn{0.f, 0.05f, 6.f};
    contracts::EntityId hero = world->create({}, spawn);
    pl::PlayerSettings ps;
    auto player = std::make_unique<pl::PlayerController>(hero, *world, *world, ps);
    pl::CameraRig rig;
    rig.setAngles(0.f, -0.28f);
    rig.snapTo(spawn, world.get());
    for (int i = 0; i < 30; ++i) world->step(1.f / 60.f);

    han::platform::InputMap input;
    input.bind("forward", kW);
    input.bind("back", kS);
    input.bind("left", kA);
    input.bind("right", kD);
    input.bind("jump", kSpace);
    input.bind("sprint", kLShift);
    input.bind("reset", kR);
    input.bind("debug", kF1);
    input.bind("capture", kTab);
    input.bind("quit", kEsc);
    input.bind("console", kGrave);
    han::render::Camera camera;
    camera.farZ = 2000.f;
    bool quit = false, consoleOpen = false;
    han::core::Console console;
    console.registerCommand("quit", "закрыть демо", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("выход");
    });

    window->setCaptureMouse(captured);
    const Uint64 start = SDL_GetTicksNS();
    Uint64 last = start;
    double fps = 0, accumulator = 0;
    bool screenshotFailed = false;
    float maxHeight = 0.f;
    int vaults = 0, mantles = 0;
    pl::PlayerAction previousAction = pl::PlayerAction::Idle;
    constexpr double kStep = 1.0 / 60.0;
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
            if (input.pressed("debug")) debugDraw = !debugDraw;
        }
        if (captured && !consoleOpen && !uiMouse) rig.look(ev.mouseDx, ev.mouseDy);

        pl::PlayerInput in;
        if (!uiKeys) {
            in.moveZ = (input.held("forward") ? 1.f : 0.f) - (input.held("back") ? 1.f : 0.f);
            in.moveX = (input.held("right") ? 1.f : 0.f) - (input.held("left") ? 1.f : 0.f);
            in.sprint = input.held("sprint");
            in.jumpPressed = input.pressed("jump");
            in.jumpHeld = input.held("jump");
            if (input.pressed("reset")) {
                world->destroy(hero);
                hero = world->create({}, spawn);
                player = std::make_unique<pl::PlayerController>(hero, *world, *world, ps);
                rig.snapTo(spawn, world.get());
            }
        }
        const auto pos = world->state(hero).position;
        if (autoRun) {  // маршрут: вперёд, прыжок у ящика, прыжок у уступа
            in = {};
            in.moveZ = 1.f;
            if (pos.z < -8.8f && pos.z > -9.3f) in.jumpPressed = true;
            if (pos.z < -19.1f && pos.z > -19.5f && pos.y < 0.2f) in.jumpPressed = true;
            if (pos.y > 1.7f && pos.z < -20.f) in = {};
            rig.setAngles(0.f, -0.28f);
        }

        accumulator += dt;
        while (accumulator >= kStep) {
            player->update(static_cast<float>(kStep), in, rig.yaw());
            in.jumpPressed = false;
            world->step(static_cast<float>(kStep));
            accumulator -= kStep;
        }
        const auto st = world->state(hero);
        maxHeight = std::max(maxHeight, st.position.y);
        if (player->action() != previousAction) {
            if (player->action() == pl::PlayerAction::Vaulting) ++vaults;
            if (player->action() == pl::PlayerAction::Mantling) ++mantles;
            previousAction = player->action();
        }
        rig.update(dt, st.position, player->speedFraction(), world.get());

        camera.position = {rig.position().x, rig.position().y, rig.position().z};
        camera.yaw = rig.yaw();
        camera.pitch = rig.pitch();
        camera.fovY = rig.fov();
        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        const Mat4 viewProjection = camera.viewProjection(aspect);

#ifdef HAN_WITH_IMGUI
        if (!noUi) {
            static const char* names[] = {"стоит", "идёт", "бежит", "спринт", "в воздухе", "перепрыгивает", "запрыгивает"};
            ui.drawStats({fmt("FPS: %.0f", fps), "GPU: " + renderer->deviceName(), fmt("ошибок валидации: %.0f", renderer->validationErrors()),
                          std::string("действие: ") + names[static_cast<int>(player->action())],
                          fmt("скорость: %.1f м/с, высота: %.2f м", player->horizontalSpeed(), st.position.y),
                          std::string(st.grounded ? "на земле" : "в воздухе") + fmt(", перепрыгнул %.0f, запрыгнул %.0f", vaults, mantles),
                          "WASD — бег, Shift — спринт, Space — прыжок/паркур, R — старт, F1 — контуры"});
            ui.drawConsole(console, consoleOpen);
            if (debugDraw) {
                std::vector<phys::PhysicsWorld::DebugLine> lines;
                world->debugLines(lines);
                std::vector<han::ui::ImGuiLayer::Line3D> out;
                for (const auto& l : lines) out.push_back({{l.a.x, l.a.y, l.a.z}, {l.b.x, l.b.y, l.b.z}, l.color});
                ui.drawLines(out, viewProjection, w, h);
            }
        }
#endif

        std::vector<han::render::DrawItem> items;
        items.push_back({ground, Mat4::identity()});
        for (const Block& b : blocks) items.push_back({b.mesh, b.transform});
        items.push_back({heroMesh, Mat4::translation({st.position.x, st.position.y + 0.9f, st.position.z}) * Mat4::rotationY(-player->facingYaw())});

        han::render::FrameParams params;
        params.viewProjection = viewProjection;
        params.cameraPosition = camera.position;
        params.sun = han::render::makeSun(45.f, 140.f, 5500.f);
        params.sky = han::render::skyFromSun(params.sun);
        params.nearPlane = camera.nearZ;
        params.farPlane = camera.farZ;
        params.shadows = true;
        params.taa = false;
        params.bloomIntensity = 0.05f;
        const bool shootNow = !screenshotPath.empty() && maxFrames > 0 && frame == maxFrames - 1;
        if (shootNow && !renderer->requestScreenshot(screenshotPath)) screenshotFailed = true;
        renderer->drawFrame(params, items, w, h);
        if (shootNow && !screenshotFailed && (renderer->screenshotPending() || !renderer->screenshotError().empty())) {
            std::fprintf(stderr, "снимок экрана не сохранён: %s\n", renderer->screenshotError().c_str());
            screenshotFailed = true;
        }
    }
    const auto fin = world->state(hero);
    std::printf("персонаж: x %.2f, y %.2f, z %.2f; наибольшая высота %.2f м; перепрыгиваний %d, запрыгиваний %d\n", fin.position.x, fin.position.y, fin.position.z, maxHeight,
                vaults, mantles);
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    if (screenshotFailed) return 2;
    return errors == 0 ? 0 : 1;
}
