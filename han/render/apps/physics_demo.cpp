// Демо E5: капсула бегает по полигону (ступени, рампа, стена, платформа), на полигон падают коробки и шары (Jolt).
// Управление: мышь — камера, W/A/S/D — бег, Shift — быстрее, Space — прыжок, F — бросить коробку, G — бросить шар,
// R — вернуть персонажа на старт, F1 — отладочные контуры физики, Tab — захват мыши, ` — консоль, Esc — выход.
// Запуск: han_physics_demo.exe [--auto] [--frames N] [--no-capture] [--no-ui] [--deterministic] [--size W H] [--screenshot файл.png]
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
#include "han/core/console.h"
#include "han/physics/physics_world.h"
#ifdef HAN_HAS_MINIAUDIO
#include "han/audio/audio_system.h"
#endif
#include "han/platform/window.h"
#include "han/render/camera.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#ifdef HAN_WITH_IMGUI
#include "han/ui/imgui_layer.h"
#endif

#ifdef HAN_HAS_MINIAUDIO
#include <fstream>
#endif

namespace {
using han::core::Mat4;
namespace phys = han::physics;
namespace contracts = han::contracts;

// Коды SDL_Scancode.
constexpr int kW = 26, kA = 4, kS = 22, kD = 7, kF = 9, kG = 10, kR = 21, kSpace = 44, kLShift = 225, kTab = 43, kEsc = 41, kGrave = 53, kF1 = 58, kRight = 79, kLeft = 80, kDown = 81, kUp = 82;

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}

// Матрица тела: поворот кватернионом и сдвиг (порядок столбцов как в Mat4: m[12..14] — перенос).
Mat4 bodyMatrix(contracts::Vec3 p, contracts::Quat q) {
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

#ifdef HAN_HAS_MINIAUDIO
// Тон «мотора» из нескольких гармоник, ровно целое число периодов — без щелчка на стыке цикла (демо; настоящие записи — позже, свободные).
void writeEngineLoop(const std::filesystem::path& path) {
    const std::uint32_t rate = 48000, frames = 24000;  // 0,5 с, основная частота 80 Гц: ровно 40 периодов
    std::ofstream out(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    u32(36 + frames * 2);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(frames * 2);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        float v = 0.f;
        for (int h = 1; h <= 6; ++h) v += std::sin(6.2831853f * 80.f * static_cast<float>(h) * t) / static_cast<float>(h);
        u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v * 0.3f * 32767.f)));
    }
}
#endif

contracts::Quat aroundZ(float degrees) {
    const float h = degrees * 0.5f * 0.01745329f;
    return {0, 0, std::sin(h), std::cos(h)};
}
}  // namespace

int main(int argc, char** argv) {
    han::core::enterRepoRoot(argv[0]);  // запуск двойным щелчком из любой папки
    long maxFrames = -1;
    bool captured = true, noUi = false, deterministic = false, debugDraw = true, autoWalk = false, carMode = false;
    int windowSize[2] = {1280, 720};
    std::filesystem::path screenshotPath;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
        if (!std::strcmp(argv[i], "--auto")) autoWalk = true;
        if (!std::strcmp(argv[i], "--car")) carMode = true;  // машина вместо пешехода: стрелки, Space — ручник; с --auto едет сама
        if (!std::strcmp(argv[i], "--no-ui")) noUi = true;
        if (!std::strcmp(argv[i], "--deterministic")) deterministic = true;
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            windowSize[0] = std::max(64, std::atoi(argv[i + 1]));
            windowSize[1] = std::max(64, std::atoi(argv[i + 2]));
        }
    }

    auto world = phys::PhysicsWorld::create();
    if (world) {  // свойства поверхностей из данных (запуск из корня репозитория); без файла остаются значения по умолчанию
        phys::SurfaceTable table;
        std::string tableError;
        if (table.load("data/materials/surfaces.tsv", tableError)) world->setSurfaceTable(table);
        else std::fprintf(stderr, "таблица поверхностей: %s\n", tableError.c_str());
    }
    if (!world) {
        std::fprintf(stderr, "физика недоступна: модуль собран без Jolt\n");
        return 1;
    }

    auto window = han::platform::Window::create("HAN: Fists of Seoul — демо E5 (физика)", windowSize[0], windowSize[1]);
    if (!window) {
        std::fprintf(stderr, "не удалось создать окно\n");
        return 1;
    }
#ifdef HAN_WITH_IMGUI
    han::ui::ImGuiLayer ui;  // объявлен раньше рендерера: переживает его
    if (!noUi && !ui.init(*window)) {
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
#ifdef HAN_WITH_IMGUI
    if (!noUi) renderer->setOverlay(&ui);
#endif
    std::printf("GPU: %s; слой валидации: %s\n", renderer->deviceName().c_str(), renderer->validationEnabled() ? "включён" : "НЕ найден");

    // --- Графика: земля с шахматной текстурой, меши полигона, коробка и шар для падающих тел ---
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
    const auto boxMesh = renderer->uploadMesh(material(han::render::makeBox({0.5f, 0.5f, 0.5f}, {1.0f, 0.55f, 0.4f}), 0.0f, 0.6f));
    const auto ballMesh = renderer->uploadMesh(material(han::render::makeSphere(0.4f, 32, 16, {0.35f, 0.6f, 0.95f}), 0.0f, 0.3f));
    const auto chassisMesh = renderer->uploadMesh(material(han::render::makeBox({0.9f, 0.2f, 2.0f}, {0.75f, 0.12f, 0.1f}), 0.3f, 0.35f));
    const auto cabinMesh = renderer->uploadMesh(material(han::render::makeBox({0.75f, 0.22f, 0.9f}, {0.15f, 0.2f, 0.28f}), 0.0f, 0.2f));
    const auto wheelMesh = renderer->uploadMesh(material(han::render::makeBox({0.05f, 0.3f, 0.3f}, {0.05f, 0.05f, 0.05f}), 0.0f, 0.9f));
    const auto bodyMesh = renderer->uploadMesh(material(han::render::makeBox({0.3f, 0.9f, 0.3f}, {0.9f, 0.85f, 0.3f}), 0.0f, 0.7f));
    if (!ground || !boxMesh || !ballMesh || !bodyMesh) {
        std::fprintf(stderr, "не удалось загрузить меши\n");
        return 1;
    }

    // --- Полигон: каждый статический блок — и физическое тело, и меш ---
    struct StaticVisual {
        han::render::MeshId mesh;
        Mat4 transform;
    };
    std::vector<StaticVisual> statics;
    const auto addStatic = [&](contracts::Vec3 center, contracts::Vec3 half, han::core::Vec3 color, contracts::Quat rot = {}) {
        world->addStaticBox(center, half, contracts::SurfaceMaterial::Concrete, rot);
        const auto mesh = renderer->uploadMesh(material(han::render::makeBox({half.x, half.y, half.z}, color), 0.0f, 0.8f));
        statics.push_back({mesh, bodyMatrix(center, rot)});
    };
    world->addStaticBox({0, -0.5f, 0}, {150, 0.5f, 150}, contracts::SurfaceMaterial::Asphalt);
    for (int i = 0; i < 6; ++i)  // лестница: ступени по 0.15 м, ниже stepHeight персонажа (0.35)
    {
        const float halfHeight = 0.075f * static_cast<float>(i + 1);  // верх ступени: 0.15 * (i + 1)
        addStatic({-8.f + static_cast<float>(i) * 0.6f, halfHeight, -6.f}, {0.3f, halfHeight, 2.f}, {0.75f, 0.72f, 0.68f});
    }
    addStatic({4.f, 0.7f, -6.f}, {4.f, 0.1f, 2.f}, {0.7f, 0.55f, 0.4f}, aroundZ(15.f));  // рампа под 15 градусов
    addStatic({10.f, 1.f, -6.f}, {1.5f, 1.f, 2.f}, {0.6f, 0.62f, 0.7f});              // платформа (2 м: на неё нужно прыгать с рампы)
    addStatic({0.f, 1.5f, -14.f}, {8.f, 1.5f, 0.4f}, {0.8f, 0.8f, 0.82f});            // стена 3 м
    addStatic({-14.f, 0.5f, 4.f}, {0.5f, 0.5f, 0.5f}, {0.85f, 0.6f, 0.5f});           // низкий блок: можно перепрыгнуть
    // Динамические тела (пирамида и брошенные игроком коробки и шары): запоминаем, чтобы рисовать.
    struct Thrown {
        contracts::EntityId id;
        bool ball;
    };
    std::vector<Thrown> thrown;
    for (int i = 0; i < 3; ++i)  // пирамида из коробок
        for (int j = 0; j < 3 - i; ++j) {
            const auto id = world->addDynamicBox({-3.f + static_cast<float>(j) * 1.05f + static_cast<float>(i) * 0.52f, 0.5f + static_cast<float>(i) * 1.01f, 6.f}, {0.5f, 0.5f, 0.5f}, 5.f);
            if (id) thrown.push_back({id, false});
        }

    const contracts::Vec3 spawn{0.f, 0.1f, 8.f};
    contracts::EntityId hero = world->create({}, spawn);

    // Автопрогулка (--auto): персонаж сам идёт по маршруту ступени → рампа → платформа с прыжком; нужна для проверки без клавиатуры.
    struct Waypoint {
        float x, z;
        bool jumpNear;
    };
    const std::vector<Waypoint> route = {{-11.f, -6.f, false}, {-3.f, -6.f, false}, {7.5f, -6.f, true}, {10.f, -6.f, false}};
    std::size_t routeIndex = 0;
    float maxHeroY = 0.f;
    contracts::EntityId car = contracts::kNoEntity;
#ifdef HAN_HAS_MINIAUDIO
    std::unique_ptr<han::audio::AudioSystem> audio;
    han::audio::AudioSystem::VoiceId engineVoice = 0;
#endif
    if (carMode) {
        car = world->createVehicle({}, {18.f, 1.0f, 4.f}, {0.f, 1.f, 0.f, 0.f});  // нос смотрит в −Z (поворот на 180° вокруг Y)
        if (!car) std::fprintf(stderr, "машину создать не удалось\n");
#ifdef HAN_HAS_MINIAUDIO
        // Звук двигателя: тон зациклен, высота растёт с оборотами, громкость — с газом. Без звуковой карты демо просто молчит.
        const auto dir = std::filesystem::temp_directory_path() / "han_physics_demo";
        std::filesystem::create_directories(dir);
        writeEngineLoop(dir / "engine.wav");
        han::audio::AudioSettings as;
        std::string audioError;
        audio = han::audio::AudioSystem::create(as, audioError);
        if (audio) {
            han::audio::SoundEvent engine;
            engine.id = "engine";
            engine.files = {dir / "engine.wav"};
            engine.loop = true;
            engine.minDistance = 4.f;
            engine.maxDistance = 120.f;
            audio->addEvent(engine);
            engineVoice = audio->playLoop("engine", {0.f, 0.f, 0.f});
        } else {
            std::fprintf(stderr, "звук недоступен: %s\n", audioError.c_str());
        }
#endif
    }

    han::platform::InputMap input;
    input.bind("forward", kW);
    input.bind("back", kS);
    input.bind("left", kA);
    input.bind("right", kD);
    input.bind("jump", kSpace);
    input.bind("fast", kLShift);
    input.bind("box", kF);
    input.bind("ball", kG);
    input.bind("reset", kR);
    input.bind("up", kUp);
    input.bind("down", kDown);
    input.bind("rleft", kLeft);
    input.bind("rright", kRight);
    input.bind("debug", kF1);
    input.bind("capture", kTab);
    input.bind("quit", kEsc);
    input.bind("console", kGrave);

    han::render::Camera camera;
    camera.yaw = 0.f;
    camera.pitch = -0.35f;
    float cameraDistance = 7.f;
    bool quit = false, consoleOpen = false;

    han::core::Console console;
    console.registerCommand("quit", "закрыть демо", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("выход");
    });
    console.registerCommand("debug", "debug on|off — контуры физики", [&](const std::vector<std::string>& a) {
        if (a.size() == 1 && (a[0] == "on" || a[0] == "off")) debugDraw = a[0] == "on";
        return std::string(debugDraw ? "включено" : "выключено");
    });
    console.registerCommand("bodies", "число тел в мире", [&](const std::vector<std::string>&) { return fmt("тел: %.0f", world->bodyCount()); });
    console.registerCommand("dist", "dist метры — расстояние камеры до персонажа", [&](const std::vector<std::string>& a) {
        if (a.size() == 1) cameraDistance = std::clamp(static_cast<float>(std::atof(a[0].c_str())), 2.f, 30.f);
        return fmt("расстояние: %.1f", cameraDistance);
    });

    window->setCaptureMouse(captured);
    const Uint64 start = SDL_GetTicksNS();
    Uint64 last = start;
    double fps = 0, accumulator = 0;
    bool screenshotFailed = false;
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
        if (captured && !consoleOpen && !uiMouse) camera.look(ev.mouseDx, ev.mouseDy);
        camera.pitch = std::clamp(camera.pitch, -1.3f, 0.6f);

        // Желаемая скорость персонажа: относительно взгляда камеры по горизонтали.
        const auto axis = [&](const char* pos, const char* neg) {
            return uiKeys ? 0.f : (input.held(pos) ? 1.f : 0.f) - (input.held(neg) ? 1.f : 0.f);
        };
        const auto fwd = camera.forward();
        float fx = fwd.x, fz = fwd.z;
        const float flen = std::sqrt(fx * fx + fz * fz);
        if (flen > 1e-4f) { fx /= flen; fz /= flen; }
        const float rx = -fz, rz = fx;  // вправо от взгляда (Y вверх, взгляд вдоль -Z при yaw = 0)
        const float forwardInput = axis("forward", "back"), rightInput = axis("right", "left");
        float mx = fx * forwardInput + rx * rightInput, mz = fz * forwardInput + rz * rightInput;
        const float mlen = std::sqrt(mx * mx + mz * mz);
        if (mlen > 1.f) { mx /= mlen; mz /= mlen; }
        const float runSpeed = input.held("fast") && !uiKeys ? 8.f : 4.5f;
        if (autoWalk && routeIndex < route.size()) {
            const auto pos = world->positionOf(hero);
            const Waypoint& wp = route[routeIndex];
            const float dx = wp.x - pos.x, dz = wp.z - pos.z, dist = std::sqrt(dx * dx + dz * dz);
            if (dist < 0.6f) {
                ++routeIndex;
            } else {
                mx = dx / dist;
                mz = dz / dist;
                if (wp.jumpNear && dist < 1.5f) world->jump(hero, 6.f);
            }
            if (routeIndex >= route.size()) mx = mz = 0.f;
        }
        if (car) {
            phys::PhysicsWorld::VehicleInput in;
            if (autoWalk) {  // едет сама: газ и плавные «змейки» рулём
                in.throttle = 0.7f;
                in.steer = 0.6f * std::sin(static_cast<float>(frame) * 0.02f);
            } else if (!uiKeys) {
                in.throttle = (input.held("up") ? 1.f : 0.f) - (input.held("down") ? 1.f : 0.f);
                in.steer = (input.held("rright") ? 1.f : 0.f) - (input.held("rleft") ? 1.f : 0.f);
                in.handbrake = input.held("jump") ? 1.f : 0.f;
            }
            world->setVehicleInput(car, in);
        }
        world->setDesiredVelocity(hero, {mx * runSpeed, 0.f, mz * runSpeed});
        maxHeroY = std::max(maxHeroY, world->positionOf(hero).y);
        if (!uiKeys && input.pressed("jump")) world->jump(hero, 6.f);
        if (!uiKeys && input.pressed("reset")) {
            world->destroy(hero);
            hero = world->create({}, spawn);
        }
        if (!uiKeys) {
            const auto heroPos = world->positionOf(hero);
            const contracts::Vec3 from{heroPos.x + fwd.x * 1.5f, heroPos.y + 1.6f, heroPos.z + fwd.z * 1.5f};
            if (input.pressed("box")) {
                const auto id = world->addDynamicBox(from, {0.5f, 0.5f, 0.5f}, 8.f, contracts::SurfaceMaterial::Wood);
                if (id) thrown.push_back({id, false});
            }
            if (input.pressed("ball")) {
                const auto id = world->addDynamicSphere(from, 0.4f, 6.f, contracts::SurfaceMaterial::Metal);
                if (id) thrown.push_back({id, true});
            }
        }

        // Фиксированный шаг физики 1/60 с.
        accumulator += dt;
        while (accumulator >= kStep) {
            world->step(static_cast<float>(kStep));
            accumulator -= kStep;
        }
        // Лимит живых тел, чтобы демо не копило нагрузку: самые старые брошенные удаляются.
        while (thrown.size() > 60) {
            world->removeBody(thrown.front().id);
            thrown.erase(thrown.begin());
        }

        const auto heroState = world->state(hero);
        const auto carState = car ? world->vehicleState(car) : phys::PhysicsWorld::VehicleState{};
        const han::core::Vec3 target = carState.valid ? han::core::Vec3{carState.position.x, carState.position.y + 1.0f, carState.position.z}
                                                      : han::core::Vec3{heroState.position.x, heroState.position.y + 1.2f, heroState.position.z};
        const auto back = camera.forward();
        camera.position = {target.x - back.x * cameraDistance, target.y - back.y * cameraDistance, target.z - back.z * cameraDistance};

        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        const Mat4 viewProjection = camera.viewProjection(aspect);

#ifdef HAN_WITH_IMGUI
        if (!noUi) {
            ui.drawStats({fmt("FPS: %.0f", fps), "GPU: " + renderer->deviceName(), fmt("ошибок валидации: %.0f", renderer->validationErrors()),
                          fmt("тел: %.0f", world->bodyCount()), fmt("персонаж: %.1f %.1f %.1f", heroState.position.x, heroState.position.y, heroState.position.z),
                          heroState.grounded ? "на земле" : "в воздухе", "F1 — контуры физики, F/G — бросить, R — старт"});
            ui.drawConsole(console, consoleOpen);
            if (debugDraw) {
                std::vector<phys::PhysicsWorld::DebugLine> lines;
                world->debugLines(lines);
                std::vector<han::ui::ImGuiLayer::Line3D> out;
                out.reserve(lines.size());
                for (const auto& l : lines)
                    out.push_back({{l.a.x, l.a.y, l.a.z}, {l.b.x, l.b.y, l.b.z}, l.color});
                ui.drawLines(out, viewProjection, w, h);
            }
        }
#endif

        std::vector<han::render::DrawItem> items;
        items.push_back({ground, Mat4::identity()});
        for (const auto& s : statics) items.push_back({s.mesh, s.transform});
        items.push_back({bodyMesh, Mat4::translation({heroState.position.x, heroState.position.y + 0.9f, heroState.position.z})});
#ifdef HAN_HAS_MINIAUDIO
        if (audio && carState.valid) {
            const float rpm01 = std::clamp((carState.rpm - 1000.f) / 5000.f, 0.f, 1.f);
            audio->setListener({camera.position.x, camera.position.y, camera.position.z}, {back.x, back.y, back.z});
            audio->setVoice(engineVoice, carState.position, 0.45f + 0.55f * rpm01, 0.7f + 1.5f * rpm01);
            audio->update(dt);
        }
#endif
        if (carState.valid) {
            const Mat4 body = bodyMatrix(carState.position, carState.rotation);
            items.push_back({chassisMesh, body});
            items.push_back({cabinMesh, body * Mat4::translation({0.f, 0.38f, -0.2f})});
            for (int i = 0; i < 4; ++i) items.push_back({wheelMesh, bodyMatrix(carState.wheelPosition[i], carState.wheelRotation[i])});
        }
        for (const auto& t : thrown)
            items.push_back({t.ball ? ballMesh : boxMesh, bodyMatrix(world->positionOf(t.id), world->rotationOf(t.id))});

        han::render::FrameParams params;
        params.viewProjection = viewProjection;
        params.cameraPosition = camera.position;
        params.sun = han::render::makeSun(45.f, 140.f, 5500.f);
        params.sky = han::render::skyFromSun(params.sun);
        params.nearPlane = camera.nearZ;
        params.farPlane = camera.farZ;
        params.shadows = true;
        params.taa = false;  // без векторов движения TAA оставляет шлейфы за падающими телами
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
    if (car) {
        const auto cs = world->vehicleState(car);
        std::printf("машина: x %.1f, y %.2f, z %.1f, скорость %.1f м/с (%.0f км/ч), передача %d, обороты %.0f\n", cs.position.x, cs.position.y, cs.position.z, cs.speed,
                    std::fabs(cs.speed) * 3.6, cs.gear, cs.rpm);
    }
    const auto finalPos = world->positionOf(hero);
    std::printf("персонаж: x %.2f, y %.2f, z %.2f; наибольшая высота %.2f м; точек маршрута пройдено %zu из %zu\n", finalPos.x, finalPos.y, finalPos.z, maxHeroY, routeIndex, route.size());
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    if (screenshotFailed) return 2;
    return errors == 0 ? 0 : 1;
}
