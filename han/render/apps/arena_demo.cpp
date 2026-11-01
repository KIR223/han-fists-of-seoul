// Демо «Арена» (G12-предвестник): первая играбельная драка — игрок против ИИ-бойца на настоящей физике.
// Управление: W/S — к врагу / от него, A/D — вбок вокруг врага, Shift — рывок, ЛКМ — удары (ритм строит серию; в сторону — хуки, назад — удар с отходом;
// удержание — сильный удар; ПКМ вместе с ЛКМ — по корпусу), ПКМ — блок (нажать вовремя — парирование), Space — уклон (сам выбирается по летящему удару;
// без направления — отскок назад), R — спецприём (нужен фокус), мышь — повернуть камеру вокруг врага, Enter — новый бой, F1 — контуры физики,
// Tab — захват мыши, ` — консоль, Esc — выход.
// Запуск из корня репозитория: han_arena_demo.exe [--enemy профиль] [--foes 1..4] [--auto] [--frames N] [--no-capture] [--no-ui] [--deterministic] [--size W H] [--screenshot файл.png]
//   --enemy: id профиля из data/combat/profiles (boxer_veteran, street_brawler, tipsy_office_worker, nervous_rookie, counter_puncher, drunk_bruiser)
//   --auto:  игрока ведёт ИИ ветерана (для проверки без клавиатуры).
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "han/core/repo_root.h"
#include "han/combat/ai.h"
#include "han/combat/combos.h"
#include "han/combat/input_scheme.h"
#include "han/combat/scenario.h"
#include "han/combat/targeting.h"
#include "han/combat/world_bridge.h"
#include "han/core/console.h"
#include "han/physics/physics_world.h"
#include "han/platform/window.h"
#include "han/player/camera_rig.h"
#include "han/render/camera.h"
#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#ifdef HAN_WITH_IMGUI
#include "han/ui/imgui_layer.h"
#endif

namespace {
using han::core::Mat4;
namespace cb = han::combat;
namespace phys = han::physics;
namespace contracts = han::contracts;

constexpr int kW = 26, kA = 4, kS = 22, kD = 7, kR = 21, kQ = 20, kSpace = 44, kLShift = 225, kTab = 43, kEsc = 41, kGrave = 53, kF1 = 58, kEnter = 40;

std::string fmt(const char* format, double a = 0, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}

std::string bar(float value, float maxValue, int width = 20) {
    const int filled = std::clamp(static_cast<int>(std::lround(width * value / maxValue)), 0, width);
    return "[" + std::string(static_cast<std::size_t>(filled), '#') + std::string(static_cast<std::size_t>(width - filled), '-') + "]";
}

Mat4 rotationX(float a) {
    Mat4 r;
    const float c = std::cos(a), s = std::sin(a);
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

// Мир для моста боя: позиции и отброс берутся у физики, святых мест на арене нет.
struct ArenaWorld final : contracts::ICombatWorld {
    phys::PhysicsWorld* physics = nullptr;
    contracts::Transform transformOf(contracts::EntityId e) const override {
        contracts::Transform t;
        t.position = physics->state(e).position;
        return t;
    }
    contracts::FighterCapsule capsuleOf(contracts::EntityId e) const override {
        contracts::FighterCapsule c;
        c.base = physics->state(e).position;
        return c;
    }
    bool lineOfSight(contracts::EntityId, contracts::EntityId) const override { return true; }
    void applyImpulse(contracts::EntityId e, contracts::Vec3 i) override { physics->applyImpulse(e, i); }
    void setFacing(contracts::EntityId, contracts::Vec3) override {}  // поворот рисуется из боевой симуляции
    bool isSacredAt(contracts::Vec3) const override { return false; }
};

std::string who(int attacker, int defender) {
    const auto name = [](int i) { return i == 0 ? std::string("ты") : "враг " + std::to_string(i); };
    return attacker >= 0 ? name(attacker) + " → " + name(defender) : name(defender);
}

Mat4 modelAt(float x, float y, float z, float headingRad) { return Mat4::translation({x, y, z}) * Mat4::rotationY(headingRad); }
}  // namespace

int main(int argc, char** argv) {
    han::core::enterRepoRoot(argv[0]);  // запуск двойным щелчком из любой папки
    long maxFrames = -1;
    bool captured = true, noUi = false, deterministic = false, debugDraw = false, autoPlay = false;
    int windowSize[2] = {1280, 720};
    std::filesystem::path screenshotPath;
    std::string enemyId = "street_brawler";
    int foeCount = 1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
        if (!std::strcmp(argv[i], "--no-ui")) noUi = true;
        if (!std::strcmp(argv[i], "--deterministic")) deterministic = true;
        if (!std::strcmp(argv[i], "--auto")) autoPlay = true;
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--enemy") && i + 1 < argc) enemyId = argv[i + 1];
        if (!std::strcmp(argv[i], "--foes") && i + 1 < argc) foeCount = std::clamp(std::atoi(argv[i + 1]), 1, 4);
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            windowSize[0] = std::max(64, std::atoi(argv[i + 1]));
            windowSize[1] = std::max(64, std::atoi(argv[i + 2]));
        }
    }

    // --- Данные боя ---
    std::string error;
    cb::CombatRules rules;
    cb::MoveLibrary moves;
    cb::ProfileLibrary profiles;
    if (!rules.load("data/combat/combat_rules.json", error) || !moves.loadDirectory("data/combat/moves", error) || !profiles.loadDirectory("data/combat/profiles", error)) {
        std::fprintf(stderr, "данные боя: %s\n(запускайте из корня репозитория)\n", error.c_str());
        return 1;
    }
    cb::ComboLibrary comboLib;
    if (!comboLib.loadDirectory("data/combat/combos", error)) std::fprintf(stderr, "комбо: %s\n", error.c_str());
    std::map<std::string, std::string> ruNames;  // ключ локализации → русский текст (из contracts/loc_requests/B.csv)
    {
        std::ifstream csv("contracts/loc_requests/B.csv");
        std::string line;
        while (std::getline(csv, line)) {
            const std::size_t c1 = line.find(','), c2 = c1 == std::string::npos ? c1 : line.find(',', c1 + 1);
            if (c2 != std::string::npos) ruNames[line.substr(0, c1)] = line.substr(c1 + 1, c2 - c1 - 1);
        }
    }
    const cb::FighterProfile* enemyProfile = profiles.byId(enemyId);
    const cb::FighterProfile* heroProfile = profiles.byId("boxer_veteran");
    if (!enemyProfile || !heroProfile) {
        std::fprintf(stderr, "нет боевого профиля «%s»\n", enemyProfile ? "boxer_veteran" : enemyId.c_str());
        return 1;
    }
    std::vector<const cb::MoveData*> moveList;
    for (const cb::MoveData& m : moves.all()) moveList.push_back(&m);

    auto world = phys::PhysicsWorld::create();
    if (!world) {
        std::fprintf(stderr, "физика недоступна: модуль собран без Jolt\n");
        return 1;
    }
    phys::SurfaceTable table;
    if (table.load("data/materials/surfaces.tsv", error)) world->setSurfaceTable(table);

    auto window = han::platform::Window::create("HAN: Fists of Seoul — Арена", windowSize[0], windowSize[1]);
    if (!window) return 1;
#ifdef HAN_WITH_IMGUI
    han::ui::ImGuiLayer ui;
    if (!noUi && !ui.init(*window)) return 1;
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
    std::printf("GPU: %s; слой валидации: %s; враг: %s\n", renderer->deviceName().c_str(), renderer->validationEnabled() ? "включён" : "НЕ найден", enemyId.c_str());

    // --- Графика: ринг 14 x 14 м, канаты (стены), тела бойцов, кулаки, предплечья блока ---
    const float kHalf = 7.f;
    auto checker = han::render::makeCheckerTexture(256, 32, 0xFFD8D0C0, 0xFF807870);
    han::render::generateMips(checker);
    const han::render::TextureId checkerId = renderer->uploadTexture(checker);
    const std::vector<han::render::TextureId> checkerSlots{checkerId};
    auto floorData = han::render::makePlane(kHalf, {0.8f, 0.78f, 0.74f}, 7.f);
    floorData.textures = {"checker"};
    floorData.submeshes = {{0, static_cast<std::uint32_t>(floorData.indices.size()), 0, 0.0f, 0.85f}};
    const auto material = [](han::render::MeshData m, float metallic, float roughness) {
        m.submeshes = {{0, static_cast<std::uint32_t>(m.indices.size()), -1, metallic, roughness}};
        return m;
    };
    const auto floorMesh = renderer->uploadMesh(floorData, checkerSlots);
    auto groundData = han::render::makePlane(120.f, {0.25f, 0.27f, 0.3f});
    groundData.submeshes = {{0, static_cast<std::uint32_t>(groundData.indices.size()), -1, 0.0f, 0.95f}};
    const auto outerMesh = renderer->uploadMesh(groundData);
    const auto bodyBlue = renderer->uploadMesh(material(han::render::makeBox({0.27f, 0.9f, 0.18f}, {0.25f, 0.45f, 0.85f}), 0.0f, 0.6f));
    const auto bodyRed = renderer->uploadMesh(material(han::render::makeBox({0.27f, 0.9f, 0.18f}, {0.85f, 0.25f, 0.2f}), 0.0f, 0.6f));
    const auto bodyWhite = renderer->uploadMesh(material(han::render::makeBox({0.27f, 0.9f, 0.18f}, {1.f, 1.f, 1.f}), 0.0f, 0.4f));
    const auto head = renderer->uploadMesh(material(han::render::makeSphere(0.16f, 16, 8, {0.9f, 0.75f, 0.6f}), 0.0f, 0.5f));
    const auto fist = renderer->uploadMesh(material(han::render::makeSphere(0.11f, 12, 6, {0.95f, 0.85f, 0.2f}), 0.0f, 0.4f));
    const auto guard = renderer->uploadMesh(material(han::render::makeBox({0.22f, 0.22f, 0.05f}, {0.9f, 0.9f, 0.95f}), 0.0f, 0.5f));
    const auto rope = renderer->uploadMesh(material(han::render::makeBox({kHalf + 0.3f, 0.04f, 0.04f}, {0.8f, 0.15f, 0.15f}), 0.0f, 0.7f));
    if (!floorMesh || !outerMesh || !bodyBlue || !bodyRed || !bodyWhite || !head || !fist || !guard || !rope) {
        std::fprintf(stderr, "не удалось загрузить меши\n");
        return 1;
    }

    // Ринг в физике: пол и стены-канаты.
    world->addStaticBox({0, -0.5f, 0}, {kHalf + 5.f, 0.5f, kHalf + 5.f}, contracts::SurfaceMaterial::Wood);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side % 2 == 0) ? -1.f : 1.f;
        world->addStaticBox({alongX ? 0.f : sign * (kHalf + 0.3f), 1.f, alongX ? sign * (kHalf + 0.3f) : 0.f}, alongX ? contracts::Vec3{kHalf + 0.3f, 1.f, 0.2f} : contracts::Vec3{0.2f, 1.f, kHalf + 0.3f},
                            contracts::SurfaceMaterial::Wood);
    }

    // --- Бой: симуляция + физические персонажи + ИИ ---
    std::unique_ptr<cb::CombatSim> sim;
    std::unique_ptr<cb::SimpleScheme> scheme;
    std::unique_ptr<cb::WorldBridge> bridge;
    std::unique_ptr<cb::FightAI> heroAi;
    std::unique_ptr<cb::ComboTracker> comboTracker;
    std::string comboBanner;  // «КОМБО: …» на пару секунд
    int comboBannerFrames = 0, comboCount = 0;
    std::vector<std::unique_ptr<cb::FightAI>> foeAi;
    cb::TargetSelector targets;
    ArenaWorld arenaWorld;
    arenaWorld.physics = world.get();
    cb::AttackTokens tokens(2, 70);
    const int fighters = 1 + foeCount;
    std::vector<contracts::EntityId> phyId(static_cast<std::size_t>(fighters), 0);
    std::uint32_t round = 0;
    int hitstop = 0;
    std::vector<int> flash(static_cast<std::size_t>(fighters), 0);
    float shake = 0.f;
    std::vector<std::string> log;
    int heroWins = 0, enemyWins = 0;
    const auto startFight = [&] {
        for (const auto id : phyId)
            if (id) world->destroy(id);
        ++round;
        sim = std::make_unique<cb::CombatSim>(rules, 1000u + round * 7919u);
        targets = cb::TargetSelector();
        tokens = cb::AttackTokens(2, 70);
        foeAi.clear();
        // Бойцы стоят в физическом мире; мост читает позиции и передаёт отброс через ICombatWorld.
        phyId[0] = world->create({}, {0.f, 0.05f, 2.2f});
        for (int k = 0; k < foeCount; ++k) {
            const float off = static_cast<float>(k) - static_cast<float>(foeCount - 1) * 0.5f;
            phyId[static_cast<std::size_t>(k) + 1] = world->create({}, {off * 2.4f, 0.05f, -2.2f - std::fabs(off) * 0.6f});
        }
        bridge = std::make_unique<cb::WorldBridge>(*sim, arenaWorld);
        bridge->addFighter(phyId[0], heroProfile->stats());
        targets.setTeam(0, 0);
        for (int k = 0; k < foeCount; ++k) {
            bridge->addFighter(phyId[static_cast<std::size_t>(k) + 1], enemyProfile->stats());
            targets.setTeam(k + 1, 1);
            foeAi.push_back(std::make_unique<cb::FightAI>(*enemyProfile, moveList, rules, 77u + round * 13u + static_cast<std::uint32_t>(k) * 31u, &tokens));
        }
        targets.update(*sim);
        scheme = std::make_unique<cb::SimpleScheme>(moves);
        heroAi = std::make_unique<cb::FightAI>(*heroProfile, moveList, rules, 5u + round * 3u, &tokens);
        comboTracker = std::make_unique<cb::ComboTracker>(comboLib);
        comboBannerFrames = 0;
        comboCount = 0;
        hitstop = 0;
        std::fill(flash.begin(), flash.end(), 0);
        log.clear();
    };
    startFight();
    for (int i = 0; i < 30; ++i) world->step(1.f / 60.f);

    han::platform::InputMap input;
    input.bind("forward", kW);
    input.bind("back", kS);
    input.bind("left", kA);
    input.bind("right", kD);
    input.bind("dash", kLShift);
    input.bind("space", kSpace);
    input.bind("special", kR);
    input.bind("shoulder", kQ);
    input.bind("restart", kEnter);
    input.bind("debug", kF1);
    input.bind("capture", kTab);
    input.bind("quit", kEsc);
    input.bind("console", kGrave);

    han::player::CameraRigSettings crs;
    crs.distance = 5.8f;
    crs.pivotHeight = 1.9f;
    crs.shoulderOffset = 0.3f;
    crs.followSharpness = 9.f;
    crs.baseFov = 0.95f;
    han::player::CameraRig rig(crs);
    rig.setAngles(0.f, -0.2f);
    rig.snapTo({0.f, 0.f, 2.2f}, world.get());
    float yawOffset = 0.f, pitch = -0.27f;
    han::render::Camera camera;
    camera.farZ = 500.f;
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
    bool screenshotFailed = false, lmbWasHeld = false, rmbWasHeld = false;
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
            if (input.pressed("restart")) startFight();
        }
        if (captured && !consoleOpen && !uiMouse) {
            yawOffset = std::clamp(yawOffset + ev.mouseDx * 0.0025f, -1.2f, 1.2f);
            pitch = std::clamp(pitch - ev.mouseDy * 0.0025f, -0.9f, 0.4f);
        }
        yawOffset *= std::exp(-1.6f * dt);  // камера возвращается за спину

        // --- Ввод игрока (мышь для боя — состояние кнопок из SDL через окно; в --auto игрока ведёт ИИ) ---
        cb::RawInput raw;
        if (!uiKeys) {
            raw.forward = (input.held("forward") ? 1.f : 0.f) - (input.held("back") ? 1.f : 0.f);
            raw.right = (input.held("right") ? 1.f : 0.f) - (input.held("left") ? 1.f : 0.f);
            raw.dash = input.held("dash");
            raw.spacePressed = input.pressed("space");
            raw.specialPressed = input.pressed("special");
            raw.shoulderPressed = input.pressed("shoulder");
            const bool lmb = window->mouseButtonHeld(1), rmb = window->mouseButtonHeld(3);
            raw.lmbHeld = lmb && captured;
            raw.lmbPressed = raw.lmbHeld && !lmbWasHeld;
            raw.rmbHeld = rmb && captured;
            raw.rmbPressed = raw.rmbHeld && !rmbWasHeld;
            lmbWasHeld = raw.lmbHeld;
            rmbWasHeld = raw.rmbHeld;
        }

        // --- Фиксированный шаг боя и физики ---
        accumulator += dt;
        while (accumulator >= kStep) {
            accumulator -= kStep;
            if (hitstop > 0) {  // хитстоп: момент удара «замирает»
                --hitstop;
                continue;
            }
            {  // прицел игрока: взгляд на цель, повёрнутый мышью — так можно выбрать другого врага
                const cb::Fighter& h = sim->fighter(0);
                const float aim = std::atan2(h.facing.x, -h.facing.z) + yawOffset;
                targets.setAim(0, {std::sin(aim), -std::cos(aim)});
                targets.update(*sim);
            }
            cb::FighterInput heroIn = autoPlay ? heroAi->decide(*sim, 0) : scheme->map(raw, *sim, 0);
            raw.lmbPressed = raw.rmbPressed = raw.spacePressed = raw.specialPressed = raw.shoulderPressed = false;  // одноразовые нажатия — один шаг
            std::vector<cb::FighterInput> inputs{heroIn};
            for (int k = 0; k < foeCount; ++k) inputs.push_back(foeAi[static_cast<std::size_t>(k)]->decide(*sim, k + 1));
            bridge->step(inputs);  // позиции из физики, шаг боя, отброс обратно в физику
            for (const cb::ComboEvent& c : comboTracker->update(sim->events())) {
                const auto it = ruNames.find(c.combo->nameKey);
                const std::string name = it != ruNames.end() ? it->second : c.combo->id;
                const bool flowOpened = sim->awardRhythm(c.fighter, c.combo->flowHits);  // награда комбо — в ритм, может открыть «Поток»
                if (flowOpened) log.push_back(std::string(c.fighter == 0 ? "ты" : "враг " + std::to_string(c.fighter)) + ": ПОТОК");
                if (c.fighter == 0) {
                    comboBanner = "КОМБО: " + name;
                    comboBannerFrames = 150;
                    ++comboCount;
                }
                log.push_back(std::string(c.fighter == 0 ? "ты" : "враг " + std::to_string(c.fighter)) + ": комбо «" + name + "»");
                if (log.size() > 7) log.erase(log.begin());
            }
            if (comboBannerFrames > 0) --comboBannerFrames;
            // Физика ведёт бойцов: боевая симуляция говорит, куда они хотят сдвинуться, мир (стены, пол) решает, куда реально.
            for (int i = 0; i < fighters; ++i) {
                const contracts::Vec3 p = world->state(phyId[static_cast<std::size_t>(i)]).position;
                const cb::Fighter& f = sim->fighter(i);
                contracts::Vec3 v{(f.pos.x - p.x) / static_cast<float>(kStep), 0.f, (f.pos.z - p.z) / static_cast<float>(kStep)};
                const float len = std::sqrt(v.x * v.x + v.z * v.z);
                if (len > 14.f) v = {v.x * 14.f / len, 0.f, v.z * 14.f / len};
                world->setDesiredVelocity(phyId[static_cast<std::size_t>(i)], v);
            }
            for (const cb::CombatEvent& e : sim->events()) {
                if ((e.type == cb::CombatEventType::Hit || e.type == cb::CombatEventType::Blocked) && e.move) {
                    // Отброс от удара ушёл в физику через мост; хитстоп и тряска — по данным приёма.
                    hitstop = std::max(hitstop, std::min(12, e.move->hitstopMs * 60 / 1000));
                    shake = std::max(shake, e.move->shake * (e.type == cb::CombatEventType::Blocked ? 0.4f : 1.f));
                    flash[static_cast<std::size_t>(e.defender)] = e.type == cb::CombatEventType::Blocked ? 3 : 7;
                }
                static const char* names[] = {"удар", "блок", "уклон", "ПАРИРОВАНИЕ", "промах", "НОКДАУН", "встал", "НОКАУТ"};
                std::string line = who(e.attacker, e.defender) + ": " + names[static_cast<int>(e.type)];
                if (e.damage > 0.f) line += fmt(" %.1f", e.damage);
                if (e.counter) line += " КОНТР";
                if (e.crit) line += " КРИТ";
                log.push_back(line);
                if (log.size() > 7) log.erase(log.begin());
                if (e.type == cb::CombatEventType::KnockedOut) (e.defender == 0 ? enemyWins : heroWins)++;
            }
            world->step(static_cast<float>(kStep));
            for (int i = 0; i < fighters; ++i) {  // стены и пол имеют последнее слово: позиция боя = позиция физики
                const contracts::Vec3 p = world->state(phyId[static_cast<std::size_t>(i)]).position;
                sim->fighterMutable(i).pos = {p.x, p.z};
            }
        }
        for (int& fl : flash) fl = std::max(0, fl - 1);
        shake = std::max(0.f, shake - 2.5f * dt);

        // --- Камера: смотрит на врага из-за плеча игрока ---
        const cb::Fighter& hero = sim->fighter(0);
        const int focus = hero.target >= 0 ? hero.target : 1;
        const cb::Fighter& enemy = sim->fighter(focus);
        const float aimYaw = std::atan2(enemy.pos.x - hero.pos.x, -(enemy.pos.z - hero.pos.z));
        rig.setAngles(aimYaw + yawOffset - 0.55f, pitch);  // камера сбоку от оси боя: игрок слева, враг справа
        const contracts::Vec3 heroFeet = world->state(phyId[0]).position;
        rig.update(dt, heroFeet, 0.f, world.get());
        const float sx = shake * 0.06f * std::sin(static_cast<float>(frame) * 2.7f), sy = shake * 0.06f * std::cos(static_cast<float>(frame) * 3.3f);
        camera.position = {rig.position().x + sx, rig.position().y + sy, rig.position().z};
        // Камера за плечом игрока, но смотрит на точку между бойцами (ближе к врагу): оба в кадре, враг не прячется за спиной.
        {
            const float tx = hero.pos.x + (enemy.pos.x - hero.pos.x) * 0.7f, tz = hero.pos.z + (enemy.pos.z - hero.pos.z) * 0.7f, ty = 1.2f;
            const float dx = tx - camera.position.x, dy = ty - camera.position.y, dz = tz - camera.position.z;
            const float horiz = std::sqrt(dx * dx + dz * dz);
            camera.yaw = std::atan2(dx, -dz) + yawOffset * 0.3f;
            camera.pitch = std::atan2(dy, horiz) + (pitch + 0.27f);
        }
        camera.fovY = rig.fov();
        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        const Mat4 viewProjection = camera.viewProjection(aspect);

#ifdef HAN_WITH_IMGUI
        if (!noUi) {
            std::vector<std::string> lines;
            lines.push_back(fmt("FPS: %.0f  ошибок валидации: %.0f", fps, renderer->validationErrors()));
            lines.push_back(std::string("ТЫ       здоровье ") + bar(hero.health, 100.f) + fmt(" %.0f", hero.health));
            lines.push_back(std::string("          стамина  ") + bar(hero.stamina, 100.f) + fmt(" %.0f", hero.stamina));
            lines.push_back(std::string("          баланс   ") + bar(hero.balance, 100.f) + fmt(" %.0f   фокус %.0f", hero.balance, hero.focus));
            {
                std::string extra;
                if (hero.flowUntil > sim->frame()) extra += "ПОТОК!  ";
                else if (hero.rhythm > 0) extra += fmt("ритм %.0f/", hero.rhythm) + std::to_string(rules.rhythmHitsForFlow) + "  ";
                if (hero.headCharges > 0) extra += fmt("голова x%.0f  ", hero.headCharges);
                if (comboCount > 0) extra += fmt("комбо: %.0f", comboCount);
                if (!extra.empty()) lines.push_back("          " + extra);
                if (comboBannerFrames > 0) lines.push_back(">>> " + comboBanner);
            }
            for (int k = 1; k < fighters; ++k) {
                const cb::Fighter& foe = sim->fighter(k);
                lines.push_back("ВРАГ " + std::to_string(k) + " (" + enemyId + ")" + (k == focus ? "  <- цель" : ""));
                lines.push_back(std::string("          здоровье ") + bar(foe.health, 100.f, 12) + fmt(" %.0f  стамина ", foe.health) + bar(foe.stamina, 100.f, 8));
            }
            lines.push_back(fmt("Счёт: ты %.0f — враг %.0f", heroWins, enemyWins));
            if (targets.teamDefeated(*sim, 0) || targets.teamDefeated(*sim, 1)) lines.push_back(hero.alive() ? ">>> ПОБЕДА! Enter — новый бой" : ">>> ПОРАЖЕНИЕ. Enter — новый бой");
            for (const std::string& l : log) lines.push_back("· " + l);
            lines.push_back("ЛКМ — удары, ПКМ — блок, Space — уклон, Q + A/D — шолдер-ролл, R — спец, Shift — рывок, Enter — заново");
            ui.drawStats(lines);
            ui.drawConsole(console, consoleOpen);
            if (debugDraw) {
                std::vector<phys::PhysicsWorld::DebugLine> dl;
                world->debugLines(dl);
                std::vector<han::ui::ImGuiLayer::Line3D> out;
                for (const auto& l : dl) out.push_back({{l.a.x, l.a.y, l.a.z}, {l.b.x, l.b.y, l.b.z}, l.color});
                ui.drawLines(out, viewProjection, w, h);
            }
        }
#endif

        // --- Отрисовка бойцов: позы читаются по кадрам приёма ---
        std::vector<han::render::DrawItem> items;
        items.push_back({outerMesh, Mat4::identity()});
        items.push_back({floorMesh, Mat4::translation({0, 0.01f, 0})});
        for (int side = 0; side < 4; ++side) {
            const bool alongX = side < 2;
            const float sign = (side % 2 == 0) ? -1.f : 1.f;
            const Mat4 base = Mat4::translation({alongX ? 0.f : sign * (kHalf + 0.3f), 0.f, alongX ? sign * (kHalf + 0.3f) : 0.f}) * (alongX ? Mat4::identity() : Mat4::rotationY(1.5707963f));
            for (const float y : {0.55f, 0.95f, 1.35f}) items.push_back({rope, base * Mat4::translation({0.f, y, 0.f})});
        }
        for (int i = 0; i < fighters; ++i) {
            const cb::Fighter& f = sim->fighter(i);
            const contracts::Vec3 feet = world->state(phyId[static_cast<std::size_t>(i)]).position;
            const float heading = std::atan2(f.facing.x, -f.facing.z);
            const cb::Vec2 right{-f.facing.z, f.facing.x};
            float lean = 0.f, sway = 0.f, crouch = 1.f;
            if (f.state == cb::FighterState::Defending) {
                const float k = std::sin(std::min(1.f, static_cast<float>(f.stateFrame) / 8.f) * 3.14159f);
                if (f.action == cb::DefenseAction::SlipLeft || f.action == cb::DefenseAction::SidestepLeft) sway = -0.35f * k;
                if (f.action == cb::DefenseAction::SlipRight || f.action == cb::DefenseAction::SidestepRight) sway = 0.35f * k;
                if (f.action == cb::DefenseAction::Duck) crouch = 1.f - 0.4f * k;
                if (f.action == cb::DefenseAction::LeanBack || f.action == cb::DefenseAction::StepBack) lean = -0.3f * k;
            }
            const bool down = f.state == cb::FighterState::Knockdown || f.state == cb::FighterState::GettingUp || f.state == cb::FighterState::KnockedOut;
            const bool hurt = f.state == cb::FighterState::HitStun;
            if (hurt) lean = -0.25f;
            const han::render::MeshId bodyMesh = flash[static_cast<std::size_t>(i)] > 0 ? bodyWhite : (i == 0 ? bodyBlue : bodyRed);
            Mat4 body;
            if (down) {
                const float fall = f.state == cb::FighterState::GettingUp ? 1.f - static_cast<float>(f.stateFrame) / static_cast<float>(rules.getupFrames) : std::min(1.f, static_cast<float>(f.stateFrame) / 12.f);
                body = modelAt(feet.x, feet.y + 0.9f * (1.f - fall) + 0.3f * fall, feet.z, heading) * rotationX(-1.4f * fall);
            } else {
                body = Mat4::translation({feet.x + right.x * sway, feet.y + 0.9f * crouch, feet.z + right.z * sway}) * Mat4::rotationY(heading) * rotationX(lean) * Mat4::scale({1.f, crouch, 1.f});
            }
            items.push_back({bodyMesh, body});
            items.push_back({head, body * Mat4::translation({0.f, 1.05f, 0.f})});
            if (down) continue;
            // Кулаки: летят к врагу по кадрам приёма (замах — отвод назад, активная фаза — вытянут, восстановление — возврат).
            float reachL = 0.25f, reachR = 0.25f;
            if (f.state == cb::FighterState::Attacking && f.move) {
                const cb::MoveData& m = *f.move;
                const float t = static_cast<float>(f.stateFrame);
                float p;
                if (t < static_cast<float>(m.startup)) p = -0.25f * (t / static_cast<float>(m.startup));
                else if (t < static_cast<float>(m.startup + m.active)) p = 1.f;
                else p = 1.f - (t - static_cast<float>(m.startup + m.active)) / static_cast<float>(m.recovery);
                const float reach = 0.25f + p * (m.range - 0.1f);
                if (m.limb == "right_hand") reachR = reach;
                else reachL = reach;
            }
            const bool blocking = f.state == cb::FighterState::Blocking || (f.state == cb::FighterState::BlockStun);
            const float y = blocking ? 1.25f : 0.95f;
            items.push_back({fist, body * Mat4::translation({-0.28f, y - 0.9f, -reachL})});
            items.push_back({fist, body * Mat4::translation({0.28f, y - 0.9f, -reachR})});
            if (blocking) items.push_back({guard, body * Mat4::translation({0.f, 0.55f, -0.38f})});
        }

        han::render::FrameParams params;
        params.viewProjection = viewProjection;
        params.cameraPosition = camera.position;
        params.sun = han::render::makeSun(52.f, 150.f, 5300.f);
        params.sky = han::render::skyFromSun(params.sun);
        params.exposure = 0.9f;
        params.nearPlane = camera.nearZ;
        params.farPlane = camera.farZ;
        params.shadows = true;
        params.taa = false;
        params.bloomIntensity = 0.04f;
        const bool shootNow = !screenshotPath.empty() && maxFrames > 0 && frame == maxFrames - 1;
        if (shootNow && !renderer->requestScreenshot(screenshotPath)) screenshotFailed = true;
        renderer->drawFrame(params, items, w, h);
        if (shootNow && !screenshotFailed && (renderer->screenshotPending() || !renderer->screenshotError().empty())) {
            std::fprintf(stderr, "снимок экрана не сохранён: %s\n", renderer->screenshotError().c_str());
            screenshotFailed = true;
        }
    }
    const cb::Fighter& h0 = sim->fighter(0);
    std::printf("камера: %.2f %.2f %.2f yaw %.2f pitch %.2f; боец0 %.2f %.2f\n", camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch, h0.pos.x, h0.pos.z);
    std::printf("итог: ты — здоровье %.1f;", h0.health);
    for (int k = 1; k < fighters; ++k) std::printf(" враг %d (%s) — %.1f;", k, enemyId.c_str(), sim->fighter(k).health);
    std::printf(" событий боя %zu; побед: ты %d, враг %d\n", sim->log().size(), heroWins, enemyWins);
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    if (screenshotFailed) return 2;
    return errors == 0 ? 0 : 1;
}
