// Демо E10: город из данных OpenStreetMap (по умолчанию Мапо: Хондэ), ячейки подгружаются по мере полёта (E9).
// Управление: мышь — взгляд, W/A/S/D — полёт, Space/Ctrl — вверх/вниз, Shift — быстрее, Tab — захват мыши, ` — консоль, Esc — выход.
// Запуск: han_city_demo.exe [--osm файл.osm] [--origin широта долгота] [--frames N] [--no-capture] [--no-ui] [--deterministic]
//         [--size W H] [--screenshot файл.png] [--camera x y z рыскание тангаж] [--sun высота азимут кельвины] [--wet 0..1] [--traffic число машин, 0 — выключить] [--walk] [--cooked папка]
//         [--hour 0..24] — начало плавного суточного цикла из data/city/day_cycle.json; --sun задаёт свет вручную.
// Данные OSM (ODbL) не лежат в Git: python han/tools/pipeline/han_osm_fetch.py (карта © участники OpenStreetMap).
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <fstream>
#include <vector>

#include "city_common.h"
#include "han/ai/district_catalog.h"
#include "han/ai/metro.h"
#include "han/ai/traffic.h"
#include "han/ai/city_crowd.h"
#include "han/ai/life_profile.h"
#include "han/world/far_skyline.h"
#include "han/world/landmarks.h"
#include "han/ai/crowd.h"
#include "han/ai/archetype.h"
#include "han/render/street_models.h"
#include "han/render/vehicle_pose.h"
#include "han/render/asset_loader.h"
#include "han/core/repo_root.h"
#include "han/core/console.h"
#include "han/platform/window.h"
#include "han/render/camera.h"
#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/renderer.h"
#include "han/physics/physics_world.h"
#include "han/player/camera_rig.h"
#include "han/player/player_controller.h"
#include "han/world/cell_io.h"
#include "han/world/city_day_cycle.h"
#include "han/world/city_props.h"
#include "han/world/walk_layout.h"
#include "han/world/district_scene.h"
#include "han/world/water_surface.h"
#include "han/world/city_builder.h"
#include "han/world/terrain_physics.h"
#include "han/world/streaming.h"
#include "han/render/frame_metrics.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#undef near  // макросы старых заголовков Windows ломают имена полей
#undef far
#endif
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
    const world::Heightmap* terrain = nullptr;
    const world::WaterSurfaceMask* waterMask = nullptr;
    const std::map<world::CellCoord, world::CityChunk>* chunks = nullptr;
    std::string cookedDir;  // не пусто — ячейки читаются из .hcell
    struct Gpu {
        han::render::MeshId ground = 0, buildings = 0;
        std::size_t triangles = 0;
        std::vector<han::contracts::EntityId> walls;  // тела стен зданий в физике (режим --walk)
        std::vector<world::Vec3> lamps;  // фонари и свет магазинов, в том числе из запечённых ячеек
        std::vector<world::CityTree> trees;
    };
    han::physics::PhysicsWorld* physics = nullptr;
    std::map<world::CellCoord, Gpu> gpu;
    std::size_t uploadedTriangles = 0;
    struct Pending {
        world::CellCoord cell;
        std::future<world::CellReadResult> read;
    };
    std::vector<Pending> pending;

    void beginLoad(world::CellCoord cell) override {
        if (!cookedDir.empty()) {
            const std::string file = (std::filesystem::path(cookedDir) / world::cellFileName(cell)).string();
            if (std::filesystem::exists(file)) pending.push_back({cell, world::loadCellAsync(file, cell, manager->settings().cellSize)});
            else upload(cell, nullptr);
        } else {
            const auto it0 = chunks->find(cell);
            upload(cell, it0 != chunks->end() ? &it0->second : nullptr);
        }
    }
    void finishLoads(bool wait = false) {
        int finished = 0;
        for (auto it = pending.begin(); it != pending.end() && (wait || finished < manager->settings().maxNewLoadsPerFrame);) {
            if (!wait && it->read.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
            auto result = it->read.get();
            if (result.ok) upload(it->cell, &result.chunk);
            else {
                std::fprintf(stderr, "ячейка: %s\n", result.error.c_str());
                manager->onLoadFailed(it->cell);
            }
            it = pending.erase(it);
            ++finished;
        }
    }
    void upload(world::CellCoord cell, const world::CityChunk* found) {
        Gpu g;
        if (found) {
            g.lamps = found->lamps;
            g.trees = found->trees;
            if (!found->ground.empty()) {
                const auto masked=waterMask ? waterMask->removeCoveredTerrain(found->ground) : found->ground;
                g.ground = renderer->uploadMesh(han::render::citytex::toMesh(masked, textureIds, 0.9f), textureIds);
            }
            if (!found->buildings.empty()) g.buildings = renderer->uploadMesh(han::render::citytex::toMesh(found->buildings, textureIds, 0.8f), textureIds);
            g.triangles = (found->ground.indices.size() + found->buildings.indices.size()) / 3;
            if (physics)
                for (const world::WallCollider& w : found->walls) {
                    const float h = w.yaw * 0.5f;
                    g.walls.push_back(physics->addStaticBox({w.center.x, w.center.y, w.center.z}, {w.half.x, w.half.y, w.half.z}, han::contracts::SurfaceMaterial::Concrete,
                                                            {0.f, std::sin(h), 0.f, std::cos(h)}));
                }
        }
        else if (terrain) {
            world::TerrainSettings settings;
            settings.cellSize = manager->settings().cellSize;
            auto ground = world::buildTerrainCell(*terrain, cell, settings);
            if (waterMask) ground=waterMask->removeCoveredTerrain(ground);
            g.triangles = ground.indices.size() / 3;
            if (!ground.empty()) g.ground = renderer->uploadMesh(han::render::citytex::toMesh(ground, textureIds, 0.95f), textureIds);
        }
        uploadedTriangles += g.triangles;
        gpu[cell] = g;
        const auto meshBytes = [](const world::GeoMesh& m) {
            return m.vertices.size() * sizeof(han::render::Vertex) + m.indices.size() * sizeof(std::uint32_t);
        };
        manager->onLoaded(cell, found ? meshBytes(found->ground) + meshBytes(found->buildings) + found->trees.size() * sizeof(world::CityTree) : 0);
    }
    void unload(world::CellCoord cell) override {
        const auto it = gpu.find(cell);
        if (it == gpu.end()) return;
        renderer->releaseMesh(it->second.ground);
        renderer->releaseMesh(it->second.buildings);
        if (physics)
            for (const auto id : it->second.walls) physics->destroyBody(id);
        uploadedTriangles -= it->second.triangles;
        gpu.erase(it);
    }
};
}  // namespace

int main(int argc, char** argv) {
    std::string cookedDir;  // папка запечённых ячеек (han_citycook): город грузится с диска без разбора OSM
    bool walk = false;  // режим прогулки: физика, коллайдеры домов, рельеф и игрок вместо летающей камеры
    float wetness = 0.f;
    int trafficCars = 120;
    bool trafficCarsExplicit = false;
    han::core::enterRepoRoot(argv[0]);  // запуск двойным щелчком из любой папки
    long maxFrames = -1;
    bool captured = true, noUi = false, deterministic = false;
    int windowSize[2] = {1280, 720};
    std::filesystem::path screenshotPath, osmPath = std::filesystem::exists("assets_src/osm/mapo_big.osm") ? "assets_src/osm/mapo_big.osm" : "assets_src/osm/mapo.osm";
    double origin[2] = {han::render::citytex::style().originLat, han::render::citytex::style().originLon};
    bool originExplicit = false;
    bool osmExplicit = false, cookedExplicit = false;
    float cameraArgs[5] = {0.f, 70.f, 120.f, 0.f, -22.f};
    float sunArgs[3] = {38.f, 125.f, 5200.f};
    float exposureArg = 1.0f;  // нейтральный множитель; дневная и ночная экспозиция — в streetscape.json
    float hourArg = -1.f;
    bool vsync = true;  // --no-vsync: IMMEDIATE/MAILBOX для замеров
    bool legacyCrowd = false, legacyLife = false, noFarSkyline = false, validationOff = false;
    bool sysMemMeshes = false, stagedMeshes = false, noLandmarks = false;
    bool sortCellsNearToFar = false, skipGround = false, skipBuildings = false, skipTrees = false;
    bool watchSteepCar = false;
    bool carPoseEnabled = true;  // --flat-cars: без шага и крена корпуса
    bool taaEnabled = true;  // --no-taa: диагностика резкости (TAA не должен оставаться выключенным как исправление)
    float flyTarget[2] = {0.f, 0.f}, flySpeed = 0.f;  // --fly
    std::filesystem::path metricsPath;  // --metrics файл.json: сводка кадров (этап 01)
    std::size_t warmupFrames = 60;      // --warmup N: кадры прогрева, не входящие в сводку
    float shadowBatchArg = -1.f;
    bool manualSun = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--no-capture")) captured = false;
        if (!std::strcmp(argv[i], "--no-ui")) noUi = true;
        if (!std::strcmp(argv[i], "--deterministic")) deterministic = true;
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atol(argv[i + 1]);
        if (!std::strcmp(argv[i], "--no-vsync")) vsync = false;
        if (!std::strcmp(argv[i], "--watch-steep-car")) watchSteepCar = true;  // камера сбоку от машины на самом крутом уклоне (для кадров «до/после»)
        if (!std::strcmp(argv[i], "--legacy-crowd")) legacyCrowd = true;  // прежняя пересборка толпы (для сравнения)
        if (!std::strcmp(argv[i], "--skip-ground")) skipGround = true;  // диагностика: не рисовать землю ячеек
        if (!std::strcmp(argv[i], "--skip-buildings")) skipBuildings = true;  // диагностика: не рисовать здания ячеек
        if (!std::strcmp(argv[i], "--skip-trees")) skipTrees = true;  // диагностика: не рисовать деревья
        if (!std::strcmp(argv[i], "--sort-cells")) sortCellsNearToFar = true;  // диагностика: ячейки от ближних к дальним (замер этапа 10: выигрыша не дало, по умолчанию выключено)
        if (!std::strcmp(argv[i], "--staged-meshes")) stagedMeshes = true;  // диагностика: загрузка мешей через промежуточный буфер, без BAR
        if (!std::strcmp(argv[i], "--sysmem-meshes")) sysMemMeshes = true;  // диагностика: меши в системной памяти (прежнее поведение)
        if (!std::strcmp(argv[i], "--no-validation")) validationOff = true;  // без слоя валидации Vulkan: замеры скорости (слой замедляет запись команд)
        if (!std::strcmp(argv[i], "--no-landmarks")) noLandmarks = true;  // без добавленных высотных ориентиров (для сравнения)
        if (!std::strcmp(argv[i], "--no-far-skyline")) noFarSkyline = true;  // без дальнего силуэта высоких зданий (для сравнения)
        if (!std::strcmp(argv[i], "--legacy-life")) legacyLife = true;  // прежние радиусы и политика трафика и толпы без профиля (для сравнения)
        if (!std::strcmp(argv[i], "--flat-cars")) carPoseEnabled = false;  // прежняя горизонтальная поза корпуса (для сравнения)
        if (!std::strcmp(argv[i], "--no-taa")) taaEnabled = false;
        if (!std::strcmp(argv[i], "--fly") && i + 3 < argc) {  // скриптовый пролёт свободной камеры: x z скорость (м/с) — одинаковый маршрут для замеров
            flyTarget[0] = static_cast<float>(std::atof(argv[i + 1]));
            flyTarget[1] = static_cast<float>(std::atof(argv[i + 2]));
            flySpeed = std::max(0.f, static_cast<float>(std::atof(argv[i + 3])));
        }
        if (!std::strcmp(argv[i], "--metrics") && i + 1 < argc) metricsPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--warmup") && i + 1 < argc) warmupFrames = static_cast<std::size_t>(std::max(0l, std::atol(argv[i + 1])));
        if (!std::strcmp(argv[i], "--osm") && i + 1 < argc) { osmPath = argv[i + 1]; osmExplicit = true; }
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--origin") && i + 2 < argc) {
            originExplicit = true;
            origin[0] = std::atof(argv[i + 1]);
            origin[1] = std::atof(argv[i + 2]);
        }
        if (!std::strcmp(argv[i], "--size") && i + 2 < argc) {
            windowSize[0] = std::max(64, std::atoi(argv[i + 1]));
            windowSize[1] = std::max(64, std::atoi(argv[i + 2]));
        }
        if (!std::strcmp(argv[i], "--exposure") && i + 1 < argc) exposureArg = static_cast<float>(std::atof(argv[i + 1]));
        if (!std::strcmp(argv[i], "--shadow-batch") && i + 1 < argc) shadowBatchArg = static_cast<float>(std::atof(argv[i + 1]));
        if (!std::strcmp(argv[i], "--camera") && i + 5 < argc)
            for (int k = 0; k < 5; ++k) cameraArgs[k] = static_cast<float>(std::atof(argv[i + 1 + k]));
        if (!std::strcmp(argv[i], "--traffic") && i + 1 < argc) { trafficCars = std::clamp(std::atoi(argv[i + 1]), 0, 400); trafficCarsExplicit = true; }
        if (!std::strcmp(argv[i], "--walk")) walk = true;
        if (!std::strcmp(argv[i], "--hour") && i + 1 < argc) hourArg = static_cast<float>(std::atof(argv[i + 1]));
        if (!std::strcmp(argv[i], "--cooked") && i + 1 < argc) { cookedDir = argv[i + 1]; cookedExplicit = true; }
        if (!std::strcmp(argv[i], "--wet") && i + 1 < argc) wetness = std::clamp(static_cast<float>(std::atof(argv[i + 1])), 0.f, 1.f);
        if (!std::strcmp(argv[i], "--sun") && i + 3 < argc) {
            manualSun = true;
            for (int k = 0; k < 3; ++k) sunArgs[k] = static_cast<float>(std::atof(argv[i + 1 + k]));
        }
    }

    if (!std::isfinite(hourArg) || hourArg < -1.f || hourArg > 24.f) {
        std::fprintf(stderr, "--hour: требуется число от 0 до 24\n");
        return 2;
    }
    if (!osmExplicit && !cookedExplicit && std::filesystem::exists("assets_bin/city/seoul/index.json") && std::filesystem::exists("assets_src/osm/seoul_connected.osm")) {
        osmPath = "assets_src/osm/seoul_connected.osm";
        cookedDir = "assets_bin/city/seoul";
    }
    // --- Данные города ---
    world::CityBakeInfo bake;
    if (!cookedDir.empty()) {
        std::string bakeError;
        if (!world::loadCityBakeInfo((std::filesystem::path(cookedDir) / "index.json").string(), bake, bakeError)) {
            std::fprintf(stderr, "%s\n", bakeError.c_str());
            return 1;
        }
        if ((originExplicit && (std::fabs(origin[0] - bake.originLat) > 1e-7 || std::fabs(origin[1] - bake.originLon) > 1e-7)) ||
            std::fabs(bake.shopTile - han::render::citytex::shopTileMeters()) > 0.001f) {
            std::fprintf(stderr, "запечённый город не совпадает с проекцией или атласом вывесок; заново запеките han_citycook\n");
            return 1;
        }
        origin[0] = bake.originLat; origin[1] = bake.originLon;
    }
    world::OsmData osm;
    std::string error;
    const bool haveOsm = world::loadOsmFile(osmPath.string(), osm, error);
    if (!haveOsm && cookedDir.empty()) {
        std::fprintf(stderr, "%s\nСкачайте данные: python han/tools/pipeline/han_osm_fetch.py\n", error.c_str());
        return 1;
    }
    if (haveOsm && !noLandmarks && std::filesystem::exists("data/city/landmarks.json")) {  // высотные ориентиры, которых нет в выгрузке OSM (этап 08e)
        std::vector<world::Landmark> landmarks;
        if (!world::loadLandmarks("data/city/landmarks.json", landmarks, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        const auto landmarkStats = world::applyLandmarks(osm, landmarks);
        std::printf("ориентиры: добавлено %d, уже есть в данных %d\n", landmarkStats.added, landmarkStats.skippedPresent);
    }
    if (!haveOsm) trafficCars = 0;  // без OSM нет графа дорог для трафика (запечённые ячейки дорог не хранят)
    world::CityStats cityStats;
    // Рельеф (E17): assets_bin/terrain/seoul.hht (python han/tools/pipeline/han_terrain_fetch.py); нет файла — плоская земля.
    world::Heightmap terrain;
    {
        std::string terrainError;
        if (!terrain.load("assets_bin/terrain/seoul.hht", terrainError)) std::fprintf(stderr, "рельеф: %s\n", terrainError.c_str());
    }
    const bool haveTerrain = terrain.valid();
    std::vector<world::DistrictScene> districtScenes;
    if (!world::loadDistrictScenes("data/city/district_scenes.json", districtScenes, error)) {
        std::fprintf(stderr, "статические районы: %s\n", error.c_str()); return 1;
    }
    for (auto& scene : districtScenes) {
        if (haveTerrain) scene.origin.y += terrain.heightAt(scene.origin.x, scene.origin.z);
        if (haveTerrain) terrain.flattenArea(scene.origin.x - scene.halfWidth, scene.origin.z - scene.halfDepth,
            scene.origin.x + scene.halfWidth, scene.origin.z + scene.halfDepth, scene.origin.y, scene.terrainBlend);
    }
    world::CityBuildSettings citySettings;
    citySettings.instanceTrees = true;
    citySettings.style = han::render::citytex::style();
    if (!citySettings.style.enabled) return 1;
    world::CityDayCycle dayCycle;
    if (!dayCycle.load("data/city/day_cycle.json", error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    float cityHour = hourArg >= 0.f ? std::fmod(hourArg, 24.f) : (manualSun ? (sunArgs[0] < 0.f ? 21.f : 14.f) : dayCycle.startHour());
    auto dayState = dayCycle.sample(cityHour);
    if (!cookedDir.empty()) citySettings.cellSize = bake.cellSize;
    if (!world::loadHeightProfile("data/city/height_profile.json", citySettings.heights, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    citySettings.terrain = haveTerrain ? &terrain : nullptr;
    if (!world::loadRoadProfileSettings("data/city/road_profile.json", citySettings.roadProfile, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    citySettings.shopTileMeters = han::render::citytex::shopTileMeters();
    const world::GeoProjection projection(origin[0], origin[1]);
    std::map<world::CellCoord, world::CityChunk> chunks;
    // Профиль мостов один для геометрии города и трафика: машины едут по высоте полотна, а не по рельефу под мостом.
    const world::RoadProfile roadProfile = haveOsm ? world::computeRoadProfile(osm, projection, citySettings.terrain, citySettings.roadProfile) : world::RoadProfile();
    citySettings.roadProfileData = &roadProfile;
    if (!roadProfile.empty()) std::printf("мосты: %d путей, %d узлов, наибольший габарит %.1f м, уклон до %.1f%%, закопанных узлов %d, на рельефе %d, проходов %d%s, крутых участков %d, худший узел %lld\n", roadProfile.stats().bridgeWays,
                                          roadProfile.stats().bridgeNodes, static_cast<double>(roadProfile.stats().maxClearance), static_cast<double>(roadProfile.stats().maxGradeUsed) * 100.0,
                                          roadProfile.stats().buriedNodes, roadProfile.stats().terrainFollowingNodes, roadProfile.stats().solverPasses,
                                          roadProfile.stats().converged ? "" : " (НЕ сошлось)", roadProfile.stats().steepSegments, static_cast<long long>(roadProfile.stats().worstGradeNode));
    if (cookedDir.empty()) chunks = world::buildCity(osm, projection, citySettings, &cityStats);
    else std::printf("город с диска: %s\n", cookedDir.c_str());
    // Трафик (G7): граф дорог из тех же данных; машины едут вокруг камеры (появляются в радиусе 350 м, уходят за 550 м).
    han::ai::RoadGraphSettings roadGraphSettings;
    roadGraphSettings.profile = &roadProfile;
    const han::ai::RoadGraph roads = haveOsm ? han::ai::RoadGraph::build(osm, projection, roadGraphSettings) : han::ai::RoadGraph();
    // Профиль жизни города: бюджет, радиусы, политика появления и плотность по часу — из data/city/life_profile.json (--legacy-life — прежние значения).
    han::ai::LifeProfile lifeProfile;
    if (!legacyLife && !han::ai::loadLifeProfile("data/city/life_profile.json", lifeProfile, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    han::ai::TrafficSettings trafficSettings;
    if (legacyLife) {
        trafficSettings.maxCars = trafficCars;
        trafficSettings.spawnRadius = 350.f;
        trafficSettings.despawnRadius = 550.f;
    } else {
        trafficSettings = lifeProfile.traffic;
        if (trafficCarsExplicit) trafficSettings.maxCars = trafficCars;  // --traffic N переопределяет бюджет профиля
        else trafficCars = trafficSettings.maxCars;
    }
    han::ai::TrafficSim traffic(roads, trafficSettings);
    // Районы (G6): контуры и настроение из data/world/districts; цветокоррекция, неон и свет фонарей плавно меняются по ходу камеры.
    han::ai::DistrictCatalog districts;
    {
        std::string districtError;
        if (!districts.loadDirectory("data/world/districts", districtError)) std::fprintf(stderr, "районы: %s\n", districtError.c_str());
    }
    han::ai::Ambience mood;
    // Метро (G17): входы станций линии 2 рядом с камерой рисуются зелёными павильонами; в панели — ближайшая станция и время до поезда.
    han::ai::MetroLine metro;
    bool haveMetro = false;
    {
        std::string metroError;
        haveMetro = metro.load("data/world/metro/line2.json", metroError);
        if (!haveMetro) std::fprintf(stderr, "метро: %s\n", metroError.c_str());
    }
    if (cookedDir.empty()) std::printf("город: домов %d, дорог %d, парков %d, воды %d, пропущено %d, ячеек %zu, размер %.0f x %.0f м\n", cityStats.buildings, cityStats.roads,
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
    desc.vsync = vsync;
    if (validationOff) desc.validation = false;
    desc.meshInSystemMemory = sysMemMeshes;
    desc.meshStagedUpload = stagedMeshes;
    desc.shadowBatchCellSize = shadowBatchArg >= 0.f && std::isfinite(shadowBatchArg) ? shadowBatchArg : citySettings.style.shadowBatchCellSize;
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
    int logicalW = 0, logicalH = 0, pixelW = 0, pixelH = 0, extentW = 0, extentH = 0;
    window->logicalSize(logicalW, logicalH);
    window->pixelSize(pixelW, pixelH);
    renderer->renderExtent(extentW, extentH);
    std::printf("разрешение: окно %dx%d, пиксели окна %dx%d, цель рендера %dx%d, плотность пикселей %.2f, масштаб дисплея %.2f\n", logicalW, logicalH, pixelW, pixelH,
                extentW, extentH, window->pixelDensity(), window->displayScale());

    // Земля вокруг: большая плоскость под городом (дороги и дома лежат выше неё).
    const auto ground = han::render::citytex::uploadGround(*renderer);
    han::ui::Localization cityLabels;
    cityLabels.loadTable("contracts/loc_requests/A.csv", error);
    cityLabels.setLanguage("ko");
    han::render::SignBrand metroBrand;
    metroBrand.text = cityLabels.get("world.metro.line2");
    metroBrand.background = 0xff4da800u;
    metroBrand.foreground = 0xfff5f3e8u;
    const std::array<han::render::TextureId, 1> metroTextures = {renderer->uploadTexture(han::render::buildBladeSignAtlas({metroBrand}, han::render::citytex::kBrandFont, 1))};
    auto entranceData = han::render::makeCityMetroEntrance({citySettings.style.metroWidth, citySettings.style.metroHeight, citySettings.style.metroLength});
    const auto metroEntrance = renderer->uploadMesh(entranceData, metroTextures);
    auto heroData = han::render::makeAdultCityPerson(citySettings.style.personHeight, {0.32f, 0.29f, 0.23f});
    const auto heroMesh = renderer->uploadMesh(heroData);
    std::vector<han::render::MeshId> carMeshes;
    world::CityVehicleModels vehicleModels;
    std::vector<std::array<han::render::LoadedMesh, 4>> importedCars;
    if (!world::loadCityVehicleModels("data/city/traffic_models.json", vehicleModels, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    for (const auto& name : vehicleModels.meshes) {
        std::array<han::render::LoadedMesh, 4> models{};
        bool complete = true;
        for (std::size_t level = 0; level < models.size(); ++level) {
            auto file = std::filesystem::path("assets_bin/cooked") / name;
            if (level) file.replace_extension(".lod" + std::to_string(level) + ".hmesh");
            if (!han::render::loadMeshAsset(*renderer, file, models[level], error)) { complete = false; std::fprintf(stderr, "транспорт: %s\n", error.c_str()); break; }
        }
        if (complete) importedCars.push_back(std::move(models));
        else for (auto& model : models) {
            if (model.id) renderer->releaseMesh(model.id);
            for (const auto texture : model.textures) renderer->releaseTexture(texture);
        }
    }
    std::printf("импортированных моделей транспорта: %zu\n", importedCars.size());
    for (const han::core::Vec3 color : {han::core::Vec3{0.75f, 0.75f, 0.78f}, {0.12f, 0.12f, 0.14f}, {0.70f, 0.12f, 0.12f}, {0.15f, 0.30f, 0.62f}, {0.85f, 0.80f, 0.70f}, {0.20f, 0.45f, 0.30f}}) {
        auto body = han::render::makeStreetCar({citySettings.style.carWidth, citySettings.style.carHeight, citySettings.style.carLength}, color);
        carMeshes.push_back(renderer->uploadMesh(body));
    }
    if (!ground) {
        std::fprintf(stderr, "не удалось загрузить землю\n");
        return 1;
    }

    // --- Стриминг ячеек ---
    world::StreamingSettings ss;
    ss.cellSize = citySettings.cellSize;
    ss.loadRadius = 900.f;
    ss.unloadRadius = 1200.f;
    ss.hlodRadius = 1200.f;
    ss.keepRadius = 300.f;
    ss.maxNewLoadsPerFrame = 3;
    ss.maxInFlight = 8;
    CityLoader loader;
    loader.renderer = renderer.get();
    loader.terrain = haveTerrain ? &terrain : nullptr;
    loader.textureIds = han::render::citytex::load(*renderer);
    world::CityPropCatalog propCatalog;
    struct PropGpu { han::render::LoadedMesh near, far; };
    std::vector<PropGpu> propMeshes;
    if (world::loadCityProps("data/city/props.json", propCatalog, error)) {
        for (const auto& spec : propCatalog.props) {
            PropGpu prop;
            const auto path = std::filesystem::path("assets_bin/cooked") / spec.mesh;
            if (!han::render::loadMeshAsset(*renderer, path, prop.near, error)) std::fprintf(stderr, "пропс: %s\n", error.c_str());
            auto lod = path; lod.replace_extension(".lod1.hmesh");
            if (prop.near.id && !han::render::loadMeshAsset(*renderer, lod, prop.far, error)) std::fprintf(stderr, "LOD пропса: %s\n", error.c_str());
            propMeshes.push_back(std::move(prop));
        }
    } else std::fprintf(stderr, "пропсы: %s\n", error.c_str());
    world::CityVegetation vegetation;
    std::array<han::render::LoadedMesh, 3> treeMeshes;
    if (!world::loadCityVegetation("data/city/vegetation.json", vegetation, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    for (std::size_t level = 0; level < treeMeshes.size(); ++level) {
        auto path = std::filesystem::path("assets_bin/cooked") / vegetation.mesh;
        if (level) path.replace_extension(".lod" + std::to_string(level) + ".hmesh");
        if (!han::render::loadMeshAsset(*renderer, path, treeMeshes[level], error)) { std::fprintf(stderr, "растительность: %s\n", error.c_str()); return 1; }
    }
    const float treeSourceHeight = treeMeshes[0].bounds.max.y - treeMeshes[0].bounds.min.y;
    if (!(treeSourceHeight > 0)) { std::fprintf(stderr, "растительность: нулевая высота модели\n"); return 1; }
    // Текстура светящихся поверхностей общая для фонарей и автомобильных фар.
    const auto lampSlot = han::render::citytex::slotOf(world::GeoMaterial::LampGlass);
    if (lampSlot >= 0) {
        const std::array<han::render::TextureId, 1> lights = {loader.textureIds[static_cast<std::size_t>(lampSlot)]};
        std::size_t colorIndex = 0;
        for (const auto color : {han::core::Vec3{0.75f, 0.75f, 0.78f}, {0.12f, 0.12f, 0.14f}, {0.70f, 0.12f, 0.12f}, {0.15f, 0.30f, 0.62f}, {0.85f, 0.80f, 0.70f}, {0.20f, 0.45f, 0.30f}}) {
            renderer->replaceMesh(carMeshes[colorIndex++], han::render::makeStreetCar({citySettings.style.carWidth, citySettings.style.carHeight, citySettings.style.carLength}, color), lights);
        }
    }
    loader.chunks = &chunks;
    world::WaterSurfaceMask waterMask;
    han::render::LoadedMesh riverWater;
    {
        han::core::Json water;
        std::string waterError;
        if (!osmExplicit && std::fabs(origin[0]-citySettings.style.originLat)<1e-7 &&
            std::fabs(origin[1]-citySettings.style.originLon)<1e-7 &&
            han::core::loadJsonFile("data/city/water.json",water,waterError)) {
            const auto file=water["mesh"].asString();
            const std::filesystem::path path(file);
            if (path.is_relative() && file.rfind("assets_bin/cooked/",0)==0 && file.find("..") == std::string::npos && std::filesystem::exists(path)) {
                const auto mask=water["mask"].asString();
                if (mask.rfind("assets_bin/processed/",0)==0 && mask.find("..") == std::string::npos && waterMask.load(mask,waterError)) {
                    if (!han::render::loadMeshAsset(*renderer,path,riverWater,waterError)) std::fprintf(stderr,"вода: %s\n",waterError.c_str());
                    else loader.waterMask=&waterMask;
                } else std::fprintf(stderr,"маска воды: %s\n",waterError.c_str());
            }
        }
    }
    han::render::MeshId farTerrain = 0;
    if (haveTerrain) {
        auto landscape = world::buildFarTerrain(terrain);
        if (loader.waterMask) landscape=waterMask.removeCoveredTerrain(landscape);
        for (auto& range : landscape.ranges) range.material = world::GeoMaterial::Park;
        farTerrain = renderer->uploadMesh(han::render::citytex::toMesh(landscape, loader.textureIds, 0.95f), loader.textureIds);
    }
    // Дальний силуэт (этап 08d): высокие здания за радиусом загрузки ячеек. Плитка рисуется, только если ни одна её ячейка не загружена.
    struct FarTile { han::render::MeshId mesh = 0; int cellX = 0, cellZ = 0; float minX = 0, minZ = 0, maxX = 0, maxZ = 0; };
    std::vector<FarTile> farTiles;
    constexpr float kFarSkylineRange = 2500.f;  // дальность рисования силуэта (радиус HLOD из настроек стриминга по замыслу этапа 01)
    if (haveOsm && !noFarSkyline) {
        world::FarSkylineSettings skylineSettings;
        skylineSettings.tileSize = citySettings.cellSize * 2.f;
        world::FarSkylineStats skylineStats;
        const auto tiles = world::buildFarSkyline(osm, projection, citySettings, citySettings.terrain, skylineSettings, &skylineStats);
        for (const auto& [key, mesh] : tiles) {
            FarTile tile;
            tile.mesh = renderer->uploadMesh(han::render::citytex::toMesh(mesh, loader.textureIds, 0.9f), loader.textureIds);
            tile.cellX = key.first * 2;
            tile.cellZ = key.second * 2;
            tile.minX = static_cast<float>(key.first) * skylineSettings.tileSize;
            tile.minZ = static_cast<float>(key.second) * skylineSettings.tileSize;
            tile.maxX = tile.minX + skylineSettings.tileSize;
            tile.maxZ = tile.minZ + skylineSettings.tileSize;
            if (tile.mesh) farTiles.push_back(tile);
        }
        std::printf("дальний силуэт: зданий %d, плиток %zu, треугольников %d\n", skylineStats.buildings, farTiles.size(), skylineStats.triangles);
    }
    loader.cookedDir = cookedDir;
    world::StreamingManager streaming(ss, loader);
    loader.manager = &streaming;
    struct SceneGpu {
        han::render::LoadedMesh exterior, interior, shadow;
        han::contracts::EntityId exteriorBody = 0, interiorBody = 0;
        bool failed = false, interiorFailed = false;
    };
    std::vector<SceneGpu> districtGpu(districtScenes.size());
    const auto releaseSceneMesh = [&](han::render::LoadedMesh& mesh) {
        if (mesh.id) renderer->releaseMesh(mesh.id);
        for (const auto texture : mesh.textures) renderer->releaseTexture(texture);
        mesh = {};
    };
    // Режим прогулки (--walk): физика мира, коллайдеры стен (по ячейкам вместе с загрузкой), плитки рельефа и игрок с камерой за спиной.
    std::unique_ptr<han::physics::PhysicsWorld> physics;
    std::unique_ptr<world::TerrainPhysics> terrainPhysics;
    std::unique_ptr<han::player::PlayerController> hero;
    han::player::CameraRig rig;
    han::contracts::EntityId heroId = 0;
    if (walk) {
        physics = han::physics::PhysicsWorld::create();
        if (!physics) {
            std::fprintf(stderr, "физика недоступна: режим --walk выключен\n");
            walk = false;
        } else {
            loader.physics = physics.get();
            if (haveTerrain) terrainPhysics = std::make_unique<world::TerrainPhysics>(*physics, terrain);
            else physics->addStaticBox({0.f, -0.5f, 0.f}, {6000.f, 0.5f, 6000.f}, han::contracts::SurfaceMaterial::Asphalt);
            const float sx = cameraArgs[0], sz = cameraArgs[2];
            if (terrainPhysics) terrainPhysics->update(sx, sz);
            streaming.update(sx, sz, 0.f, -1.f);  // загрузить ячейки (и коллайдеры) вокруг старта до появления героя
            loader.finishLoads(true);
            const float y = (haveTerrain ? terrain.heightAt(sx, sz) : 0.f) + 1.0f;
            heroId = physics->create({}, {sx, y, sz});
            hero = std::make_unique<han::player::PlayerController>(heroId, *physics, *physics);
            rig.setAngles(cameraArgs[3] * 0.01745329f, cameraArgs[4] * 0.01745329f);
            rig.snapTo({sx, y, sz}, physics.get());
            for (int i = 0; i < 30; ++i) physics->step(1.f / 60.f);  // персонаж садится на землю
        }
    }
    const auto districtCollider = [&](const world::DistrictScene& scene, const std::string& name) {
        if (!physics) return han::contracts::kNoEntity;
        han::render::MeshData mesh;
        if (!han::render::loadMeshFile(std::filesystem::path("assets_bin/cooked") / name, mesh, error)) {
            std::fprintf(stderr, "коллизия района: %s\n", error.c_str()); return han::contracts::kNoEntity;
        }
        std::vector<han::contracts::Vec3> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const auto& v : mesh.vertices) vertices.push_back({v.position[0], v.position[1], v.position[2]});
        return physics->addStaticMesh({scene.origin.x, scene.origin.y, scene.origin.z}, vertices, mesh.indices);
    };

    // Толпа и трафик живут в том же городе: локальная навигация строится вокруг камеры,
    // а не на всей карте сразу. Внутрь зданий прохожие не появляются.
    han::ai::ArchetypeLibrary people;
    std::unique_ptr<han::ai::CityCrowd> cityCrowd;  // толпа следует за камерой, пересборка без пересоздания прохожих
    auto footprints = haveOsm ? world::buildingFootprints(osm, projection) : std::vector<std::vector<world::Vec2>>{};
    for (const auto& scene : districtScenes) for (const auto& footprint : scene.footprints) {
        std::vector<world::Vec2> polygon;
        for (const auto& p : footprint) polygon.push_back({scene.origin.x + p.x, scene.origin.z + p.y});
        footprints.push_back(std::move(polygon));
    }
    // Где можно ходить: здания и вода — стены, проезжая часть улиц от 8 м закрыта, переходы (зебры строителя) — проёмы.
    world::WalkLayout walkLayout;
    if (haveOsm) {
        walkLayout = world::buildWalkLayout(osm, projection, citySettings.style);
        walkLayout.buildings.clear();  // здания берутся из footprints (вместе с контурами районов)
        std::string waterError;
        const std::string waterFile = "assets_bin/city/seoul_water_areas.json";
        if (std::filesystem::exists(waterFile) && !walkLayout.addWaterAreas(waterFile, projection, waterError)) std::fprintf(stderr, "%s\n", waterError.c_str());
    }
    std::vector<std::array<han::render::MeshId, 4>> pedestrianMeshes;
    if (haveOsm && people.loadDirectory("data/people/archetypes", error)) {
        for (const auto color : {han::core::Vec3{0.15f, 0.18f, 0.23f}, {0.36f, 0.29f, 0.23f}, {0.28f, 0.32f, 0.26f}, {0.46f, 0.4f, 0.34f}, {0.42f, 0.21f, 0.2f}, {0.21f, 0.25f, 0.37f}}) {
            std::array<han::render::MeshId, 4> poses{};
            const float strides[] = {0.f, 0.9f, 0.f, -0.9f};
            for (std::size_t p = 0; p < poses.size(); ++p) poses[p] = renderer->uploadMesh(han::render::makeAdultCityPerson(citySettings.style.personHeight, color, strides[p]));
            pedestrianMeshes.push_back(poses);
        }
        han::ai::CityCrowdSettings crowdSettings;
        crowdSettings.navRadius = citySettings.style.crowdNavRadius;
        crowdSettings.spawnRadius = citySettings.style.crowdRadius;
        crowdSettings.navCell = citySettings.style.crowdNavCell;
        crowdSettings.pathExpansions = static_cast<std::size_t>(citySettings.style.crowdPathExpansions);
        crowdSettings.pathQueriesPerFrame = citySettings.style.crowdPathQueries;
        crowdSettings.agentCount = static_cast<std::size_t>(citySettings.style.crowdCount);
        crowdSettings.keepAgents = !legacyCrowd && !legacyLife;
        if (!legacyLife) {  // данные профиля: радиусы и число прохожих; сетку (шаг, поиск пути) задаёт оформление города
            const auto fromProfile = lifeProfile.crowd;
            crowdSettings.navRadius = fromProfile.navRadius;
            crowdSettings.spawnRadius = fromProfile.spawnRadius;
            crowdSettings.recenterFraction = fromProfile.recenterFraction;
            crowdSettings.agentCount = fromProfile.agentCount;
            crowdSettings.minRecycleDistance = fromProfile.minRecycleDistance;
            crowdSettings.farVisible = fromProfile.farVisible;
            crowdSettings.viewHalfAngleDegrees = fromProfile.viewHalfAngleDegrees;
            crowdSettings.treadmillInterval = fromProfile.treadmillInterval;
            crowdSettings.treadmillBatch = fromProfile.treadmillBatch;
            crowdSettings.treadmillMinDistance = fromProfile.treadmillMinDistance;
        }
        cityCrowd = std::make_unique<han::ai::CityCrowd>(footprints, walkLayout, people.pointers(), crowdSettings);
        cityCrowd->update(0.f, {cameraArgs[0], cameraArgs[2]}, {0.f, 0.f}, cityHour);
        std::printf("городская толпа: %zu взрослых прохожих\n", cityCrowd->crowd() ? cityCrowd->crowd()->agents().size() : 0);
    }

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
    camera.position = {cameraArgs[0], cameraArgs[1] + (haveTerrain ? terrain.heightAt(cameraArgs[0], cameraArgs[2]) : 0.f), cameraArgs[2]};  // высота — над землёй
    camera.yaw = cameraArgs[3] * 0.01745329f;
    camera.pitch = cameraArgs[4] * 0.01745329f;
    camera.farZ = 4000.f;
    camera.nearZ = 0.2f;
    traffic.setFocus({camera.position.x, camera.position.z});
    for (float remaining = vehicleModels.warmupSeconds; trafficCars > 0 && remaining > 0; remaining -= 0.1f)
        traffic.update(std::min(remaining, 0.1f));
    std::printf("подготовлен трафик: %zu машин\n", traffic.cars().size());
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
            manualSun = true;
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
    Uint64 lastWall = start;
    std::vector<double> frameTimes;
    han::render::MetricsRecorder metrics(warmupFrames);
    // Замер начинается, когда стриминг успокоился (нет загрузок и запросов 30 кадров подряд); у пролёта (--fly) стриминг входит в замер.
    long settledStreak = 0, settledAtFrame = -1;
    std::size_t drawnPeople = 0, carsNear = 0, carsMid = 0;
    std::unordered_map<std::uint32_t, han::render::VehiclePoseFilter> carPoseFilters;  // подвеска каждой машины
    std::unordered_map<std::uint32_t, long> carPoseSeen;
    std::size_t activeLights = 0;
    std::size_t visibleProps = 0;
    std::size_t visibleTrees = 0;
    han::render::Renderer::VideoMemory lastVideoMemory;  // последний опрос VK_EXT_memory_budget
    bool screenshotFailed = false;
    world::StreamingStats stats;
    for (long frame = 0; (maxFrames < 0 || frame < maxFrames) && !quit; ++frame) {
        const Uint64 wallBegin = SDL_GetTicksNS();
        const Uint64 now = deterministic ? start + static_cast<Uint64>(frame) * 16666667ull : SDL_GetTicksNS();
        const float dt = deterministic ? 1.0f / 60.0f : static_cast<float>(static_cast<double>(now - last) * 1e-9);
        last = now;
        cityHour = std::fmod(cityHour + dt * 24.f / dayCycle.secondsPerDay(), 24.f);
        dayState = dayCycle.sample(cityHour);
        if (!manualSun) { sunElevation = dayState.elevation; sunAzimuth = dayState.azimuth; sunKelvin = dayState.kelvin; }
        const double wallDt = static_cast<double>(wallBegin - lastWall) * 1e-9;
        lastWall = wallBegin;
        if (wallDt > 0) fps = fps * 0.95 + (1.0 / wallDt) * 0.05;

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
        const auto axis = [&](const char* pos, const char* neg) { return uiKeys ? 0.f : (input.held(pos) ? 1.f : 0.f) - (input.held(neg) ? 1.f : 0.f); };
        if (walk) {
            if (captured && !consoleOpen && !uiMouse) rig.look(ev.mouseDx, ev.mouseDy);
            han::player::PlayerInput pi;
            pi.moveX = axis("right", "left");
            pi.moveZ = axis("forward", "back");
            pi.sprint = input.held("fast") && !uiKeys;
            pi.jumpPressed = !uiKeys && input.pressed("up");
            pi.jumpHeld = !uiKeys && input.held("up");
            hero->update(dt, pi, rig.yaw());
            physics->step(std::min(dt, 1.f / 30.f));
            const auto feet = hero->state().position;
            if (terrainPhysics) terrainPhysics->update(feet.x, feet.z);
            rig.update(dt, feet, hero->speedFraction(), physics.get());
            camera.position = {rig.position().x, rig.position().y, rig.position().z};
            camera.yaw = rig.yaw();
            camera.pitch = rig.pitch();
            camera.fovY = rig.fov();
        } else {
            if (captured && !consoleOpen && !uiMouse) camera.look(ev.mouseDx, ev.mouseDy);
            camera.move(axis("forward", "back"), axis("right", "left"), axis("up", "down"), input.held("fast") && !uiKeys ? speed * 4.f : speed, dt);
            if (flySpeed > 0.f) {  // движение к цели по прямой на постоянной высоте над рельефом, взгляд не меняется
                const float dx = flyTarget[0] - camera.position.x, dz = flyTarget[1] - camera.position.z, left = std::hypot(dx, dz);
                if (left > 0.01f) {
                    const float step = std::min(left, flySpeed * dt);
                    camera.position.x += dx / left * step;
                    camera.position.z += dz / left * step;
                    camera.position.y = cameraArgs[1] + (haveTerrain ? terrain.heightAt(camera.position.x, camera.position.z) : 0.f);
                }
            }
        }

        if (watchSteepCar && haveTerrain && !walk) {  // по состоянию машин прошлого кадра
            const han::ai::Car* steepest = nullptr;
            float steepestSlope = 0.f;
            for (const auto& c : traffic.cars()) {
                if (std::hypot(c.pos.x - cameraArgs[0], c.pos.z - cameraArgs[2]) > 250.f) continue;
                const float fx = std::sin(c.heading), fz = -std::cos(c.heading);
                const float slope = std::fabs(terrain.heightAt(c.pos.x + fx * 1.3f, c.pos.z + fz * 1.3f) - terrain.heightAt(c.pos.x - fx * 1.3f, c.pos.z - fz * 1.3f)) / 2.6f;
                if (slope > steepestSlope) { steepestSlope = slope; steepest = &c; }
            }
            if (steepest) {  // слева от машины на 7 м, взгляд на неё (направление вправо от курса: (cos h, sin h))
                const float rx = std::cos(steepest->heading), rz = std::sin(steepest->heading);
                const float ground = std::isfinite(steepest->surfaceY) ? steepest->surfaceY : terrain.heightAt(steepest->pos.x, steepest->pos.z);
                camera.position = {steepest->pos.x - rx * 7.f, ground + 1.3f, steepest->pos.z - rz * 7.f};
                camera.yaw = steepest->heading + 1.5707963f;
                camera.pitch = -0.02f;
            }
        }
        const auto fwd = camera.forward();
        loader.finishLoads();
        stats = streaming.update(camera.position.x, camera.position.z, fwd.x, fwd.z);

        int w = 0, h = 0;
        window->pixelSize(w, h);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
        const Mat4 viewProjection = camera.viewProjection(aspect);

#ifdef HAN_WITH_IMGUI
        if (!noUi) {
            ui.drawStats({fmt("FPS: %.0f", fps), "GPU: " + renderer->deviceName(), fmt("ошибок валидации: %.0f", renderer->validationErrors()),
                          fmt("камера: %.0f %.0f %.0f", camera.position.x, camera.position.y, camera.position.z),
                          [&]() -> std::string {
                              if (!haveMetro) return "метро: нет данных";
                              float d = 0.f;
                              const int st = metro.nearestStation({camera.position.x, camera.position.z}, &d);
                              const double now = 8.0 * 3600.0 + static_cast<double>(frame) / 60.0;  // демо: утренний час пик
                              const auto next = metro.nextArrival(st, now);
                              return "метро: " + metro.stations()[static_cast<std::size_t>(st)].id + fmt(" %.0f м, поезд через %.0f с", d, next ? *next - now : -1.0);
                          }(),
                          std::string("район: ") + (districts.infoAt({camera.position.x, camera.position.z}) ? districts.infoAt({camera.position.x, camera.position.z})->id : "—") + " (" + mood.lut + ")",
                          fmt("ячеек загружено: %.0f, треугольников: %.0f", stats.loaded, static_cast<double>(loader.uploadedTriangles)),
                          "© участники OpenStreetMap (ODbL)"});
            ui.drawConsole(console, consoleOpen);
        }
#endif

        if (trafficCars > 0) {
            traffic.setFocus({camera.position.x, camera.position.z});
            {
                const auto look = camera.forward();
                const float lookLength = std::hypot(look.x, look.z);
                traffic.setView(lookLength > 1e-3f ? han::ai::Vec2{look.x / lookLength, look.z / lookLength} : han::ai::Vec2{});
            }
            if (!legacyLife) traffic.setDensityScale(lifeProfile.trafficDensityAt(cityHour));
            traffic.update(std::min(dt, 0.1f));
        }
        if (!pedestrianMeshes.empty()) {
            const auto look = camera.forward();
            const float lookLength = std::hypot(look.x, look.z);
            if (cityCrowd) cityCrowd->update(dt, {camera.position.x, camera.position.z}, lookLength > 1e-3f ? han::ai::Vec2{look.x / lookLength, look.z / lookLength} : han::ai::Vec2{}, cityHour);
        }
        std::vector<han::render::DrawItem> items;
        for (std::size_t i = 0; i < districtScenes.size(); ++i) {
            const auto& scene = districtScenes[i];
            auto& gpu = districtGpu[i];
            const float distance = world::districtDistance(scene, camera.position.x, camera.position.z);
            if (distance > scene.unloadDistance) {
                releaseSceneMesh(gpu.exterior); releaseSceneMesh(gpu.interior); releaseSceneMesh(gpu.shadow);
                if (physics) { physics->destroyBody(gpu.exteriorBody); physics->destroyBody(gpu.interiorBody); }
                gpu.exteriorBody = gpu.interiorBody = 0;
                gpu.failed = gpu.interiorFailed = false;
                continue;
            }
            if (distance < scene.loadDistance && !gpu.exterior.id && !gpu.failed) {
                gpu.failed = !han::render::loadMeshAsset(*renderer, std::filesystem::path("assets_bin/cooked") / scene.mesh, gpu.exterior, error);
                if (gpu.failed) std::fprintf(stderr, "%s: %s\n", scene.id.c_str(), error.c_str());
                else {
                    std::printf("район %s загружен: %.1f %.1f %.1f\n", scene.id.c_str(), scene.origin.x, scene.origin.y, scene.origin.z);
                    if (!scene.shadow.empty() && !han::render::loadMeshAsset(*renderer, std::filesystem::path("assets_bin/cooked") / scene.shadow, gpu.shadow, error)) {
                        std::fprintf(stderr,"теневой LOD района %s: %s; используется полный меш\n",scene.id.c_str(),error.c_str());
                    }
                    if (physics) {
                        gpu.exteriorBody = districtCollider(scene, scene.collision);
                        if (!gpu.exteriorBody) { std::fprintf(stderr, "нет физической геометрии района %s\n", scene.id.c_str()); return 1; }
                    }
                }
            }
            const float centerDistance = std::hypot(camera.position.x - scene.origin.x, camera.position.z - scene.origin.z);
            if (gpu.exterior.id && centerDistance < scene.interiorDistance && !gpu.interior.id && !gpu.interiorFailed) {
                gpu.interiorFailed = !han::render::loadMeshAsset(*renderer, std::filesystem::path("assets_bin/cooked") / scene.interiors, gpu.interior, error);
                if (gpu.interiorFailed) std::fprintf(stderr, "%s: %s\n", scene.id.c_str(), error.c_str());
                else if (physics) {
                    gpu.interiorBody = districtCollider(scene, scene.interiors);
                    if (!gpu.interiorBody) { std::fprintf(stderr, "нет коллизии интерьеров %s\n", scene.id.c_str()); return 1; }
                }
            }
            if (centerDistance > scene.interiorDistance * 1.25f) {
                releaseSceneMesh(gpu.interior); gpu.interiorFailed = false;
                if (physics && gpu.interiorBody) physics->destroyBody(gpu.interiorBody);
                gpu.interiorBody = 0;
            }
            const auto model = Mat4::translation({scene.origin.x, scene.origin.y, scene.origin.z});
            if (gpu.exterior.id) items.push_back({gpu.exterior.id, model, true, gpu.shadow.id});
            if (gpu.interior.id) items.push_back({gpu.interior.id, model});
        }
        drawnPeople = 0;
        if (cityCrowd && cityCrowd->crowd()) {
            for (const auto& person : cityCrowd->crowd()->agents()) {
                const float dx = person.pos.x - camera.position.x, dz = person.pos.z - camera.position.z;
                if (dx * dx + dz * dz > citySettings.style.crowdDrawRadius * citySettings.style.crowdDrawRadius) continue;
                ++drawnPeople;
                const bool moving = person.vel.x * person.vel.x + person.vel.z * person.vel.z > 0.04f;
                const auto phase = moving ? static_cast<std::size_t>((frame / 8 + person.id) % 4) : 0;
                const auto mesh = pedestrianMeshes[person.id % pedestrianMeshes.size()][phase];
                const float y = haveTerrain ? terrain.heightAt(person.pos.x, person.pos.z) : 0.f;
                items.push_back({mesh, han::render::streetModelTransform({person.pos.x, y + 0.12f, person.pos.z}, person.heading)});
            }
        }
        if (walk) {
            const auto feet = hero->state().position;
            items.push_back({heroMesh, han::render::streetModelTransform({feet.x, feet.y, feet.z}, hero->facingYaw())});
        }
        carsNear = carsMid = 0;
        for (const han::ai::Car& car : traffic.cars()) {
            const std::size_t color = car.id % carMeshes.size();
            const float dx = car.pos.x - camera.position.x, dz = car.pos.z - camera.position.z, distance = std::hypot(dx, dz);
            carsNear += distance < 150.f;
            carsMid += distance < 400.f;
            // Поза корпуса: высота и наклон по четырём колёсам. На мосту высоты задаёт полотно (по осям), иначе — рельеф под каждым колесом.
            han::render::VehiclePose carPose;
            const float baseLength = citySettings.style.carLength * 0.6f, trackWidth = citySettings.style.carWidth * 0.85f;
            if (std::isfinite(car.surfaceYFront) && std::isfinite(car.surfaceYRear)) {
                carPose = han::render::solveVehiclePose({car.surfaceYFront, car.surfaceYFront, car.surfaceYRear, car.surfaceYRear}, baseLength, trackWidth);
            } else {
                const float fx = std::sin(car.heading), fz = -std::cos(car.heading), rx = std::cos(car.heading), rz = std::sin(car.heading);  // вперёд и вправо
                const auto wheelGround = [&](float along, float across) {
                    const float x = car.pos.x + fx * along + rx * across, z = car.pos.z + fz * along + rz * across;
                    return haveTerrain ? terrain.heightAt(x, z) : 0.f;
                };
                const float hb = baseLength * 0.5f, ht = trackWidth * 0.5f;
                carPose = han::render::solveVehiclePose({wheelGround(hb, -ht), wheelGround(hb, ht), wheelGround(-hb, -ht), wheelGround(-hb, ht)}, baseLength, trackWidth);
            }
            if (!carPoseEnabled) {
                carPose.pitch = carPose.roll = 0.f;
                carPose.y = std::isfinite(car.surfaceY) ? car.surfaceY : (haveTerrain ? terrain.heightAt(car.pos.x, car.pos.z) : 0.f);
            } else {
                carPose = carPoseFilters[car.id].update(carPose, dt);
            }
            carPoseSeen[car.id] = frame;
            const auto model = han::render::vehicleTransform(car.pos.x, car.pos.z, car.heading, carPose, 0.10f);
            if (!importedCars.empty()) {
                const std::size_t level = distance < vehicleModels.lodNear ? 0 : (distance < vehicleModels.lodMid ? 1 : (distance < vehicleModels.lodFar ? 2 : 3));
                const auto& meshes = importedCars[car.id % importedCars.size()];
                items.push_back({meshes[level].id, model, distance < vehicleModels.shadowRadius, meshes[3].id});
            } else items.push_back({carMeshes[color], model});
        }
        if (frame % 600 == 0) {  // убрать подвески машин, которых давно нет
            for (auto it = carPoseSeen.begin(); it != carPoseSeen.end();) {
                if (it->second < frame - 600) {
                    carPoseFilters.erase(it->first);
                    it = carPoseSeen.erase(it);
                } else ++it;
            }
        }
        if (haveMetro) {
            for (const auto& st : metro.stations()) {
                const float dx = st.pos.x - camera.position.x, dz = st.pos.z - camera.position.z;
                if (dx * dx + dz * dz < 600.f * 600.f) items.push_back({metroEntrance, Mat4::translation({st.pos.x, 0.12f + (haveTerrain ? terrain.heightAt(st.pos.x, st.pos.z) : 0.f), st.pos.z})});
            }
        }
        if (farTerrain) items.push_back({farTerrain, Mat4::identity()});
        for (const auto& tile : farTiles) {
            const float nx = std::clamp(camera.position.x, tile.minX, tile.maxX) - camera.position.x, nz = std::clamp(camera.position.z, tile.minZ, tile.maxZ) - camera.position.z;
            if (nx * nx + nz * nz > kFarSkylineRange * kFarSkylineRange) continue;  // дальше — за дымкой, вклад в кадр мал
            bool covered = false;  // хоть одна ячейка плитки загружена — ближний город уже рисует эти здания
            for (int dx = 0; dx < 2 && !covered; ++dx)
                for (int dz = 0; dz < 2 && !covered; ++dz) covered = loader.gpu.count({tile.cellX + dx, tile.cellZ + dz}) != 0;
            if (!covered) items.push_back({tile.mesh, Mat4::identity(), false});
        }
        if (riverWater.id) items.push_back({riverWater.id,Mat4::identity(),false});
        items.push_back({ground, Mat4::translation({0.f, haveTerrain ? terrain.minHeight() - 2.f : -0.25f, 0.f})});  // ниже дорог: иначе z-борьба на расстоянии
        {   // Порядок ячеек: по умолчанию как в карте; --sort-cells — от ближних к дальним (в замере этапа 10 разницы не дало).
            struct CellDraw { float distance; const decltype(loader.gpu)::mapped_type* gpu; };
            std::vector<CellDraw> cellDraws;
            cellDraws.reserve(loader.gpu.size());
            for (const auto& [cell, g] : loader.gpu) {
                const float cx = (static_cast<float>(cell.x) + 0.5f) * citySettings.cellSize - camera.position.x, cz = (static_cast<float>(cell.z) + 0.5f) * citySettings.cellSize - camera.position.z;
                cellDraws.push_back({sortCellsNearToFar ? cx * cx + cz * cz : 0.f, &g});
            }
            if (sortCellsNearToFar) std::stable_sort(cellDraws.begin(), cellDraws.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; });
            for (const auto& draw : cellDraws) {
                if (draw.gpu->ground && !skipGround) items.push_back({draw.gpu->ground, Mat4::identity()});
                if (draw.gpu->buildings && !skipBuildings) items.push_back({draw.gpu->buildings, Mat4::identity()});
            }
        }
        struct PropDraw { float distance; std::size_t kind; han::core::Vec3 position; };
        struct TreeDraw { float distance; const world::CityTree* tree; };
        std::vector<TreeDraw> treeDraws;
        for (const auto& [cell, g] : loader.gpu)
            for (const auto& tree : g.trees) {
                const float dx = tree.position.x - camera.position.x, dz = tree.position.z - camera.position.z, distance = dx * dx + dz * dz;
                if (distance < vegetation.drawRadius * vegetation.drawRadius) treeDraws.push_back({distance, &tree});
            }
        std::sort(treeDraws.begin(), treeDraws.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; });
        visibleTrees = skipTrees ? 0 : std::min(treeDraws.size(), static_cast<std::size_t>(vegetation.maxDraws));
        for (std::size_t i = 0; i < visibleTrees; ++i) {
            const auto& tree = *treeDraws[i].tree;
            const auto distance = treeDraws[i].distance;
            const std::size_t level = distance < vegetation.lodNear * vegetation.lodNear ? 0 : (distance < vegetation.lodFar * vegetation.lodFar ? 1 : 2);
            const float scale = tree.height / treeSourceHeight;
            items.push_back({treeMeshes[level].id, Mat4::translation({tree.position.x, tree.position.y, tree.position.z}) * Mat4::rotationY(tree.yaw) * Mat4::scale({scale, scale, scale}),
                             distance < vegetation.shadowRadius * vegetation.shadowRadius, treeMeshes[2].id});
        }
        std::vector<PropDraw> propDraws;
        for (const auto& [cell, g] : loader.gpu) {
            for (std::size_t anchor = 0; anchor < g.lamps.size(); ++anchor) {
                const auto& lamp = g.lamps[anchor];
                const float base = haveTerrain ? terrain.heightAt(lamp.x, lamp.z) : 0.f;
                const bool shop = lamp.y - base < 5.f;
                const auto seed = static_cast<std::uint32_t>(cell.x) * 73856093u ^ static_cast<std::uint32_t>(cell.z) * 19349663u ^ static_cast<std::uint32_t>(anchor) * 83492791u;
                for (std::size_t kind = 0; kind < propMeshes.size(); ++kind) {
                    const auto& spec = propCatalog.props[kind];
                    if (!propMeshes[kind].near.id || shop != spec.shop || seed % static_cast<std::uint32_t>(spec.anchorStride) != 0) continue;
                    const float x = lamp.x + spec.offsetX, z = lamp.z + spec.offsetZ;
                    const float dx = x - camera.position.x, dz = z - camera.position.z, d2 = dx * dx + dz * dz;
                    if (d2 > spec.drawRadius * spec.drawRadius || (cityCrowd && cityCrowd->grid().valid() && !cityCrowd->grid().walkable(x, z))) continue;
                    propDraws.push_back({d2, kind, {x, (haveTerrain ? terrain.heightAt(x, z) : 0.f) + 0.17f, z}});
                }
            }
        }
        std::sort(propDraws.begin(), propDraws.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; });
        visibleProps = std::min(propDraws.size(), static_cast<std::size_t>(propCatalog.maxDraws));
        for (std::size_t i = 0; i < visibleProps; ++i) {
            const auto& prop = propDraws[i];
            const auto& spec = propCatalog.props[prop.kind];
            const auto& meshes = propMeshes[prop.kind];
            const auto mesh = prop.distance > spec.lodDistance * spec.lodDistance && meshes.far.id ? meshes.far.id : meshes.near.id;
            items.push_back({mesh, Mat4::translation(prop.position) * Mat4::scale({spec.scale, spec.scale, spec.scale})});
        }

        han::render::FrameParams params;
        params.viewProjection = viewProjection;
        params.cameraPosition = camera.position;
        params.sun = han::render::makeSun(sunElevation, sunAzimuth, sunKelvin);
        params.sky = han::render::skyFromSun(params.sun);
        params.wetness = wetness;
        const han::ai::Ambience target = districts.ambienceAt({camera.position.x, camera.position.z});
        han::ai::blendAmbience(mood, target, std::min(dt, 0.1f));
        params.saturation = mood.saturation;
        params.contrast = mood.contrast;
        params.neonBoost = mood.neon;
        for (std::size_t i = 0; i < districtScenes.size(); ++i) if (districtGpu[i].interior.id) {
            const auto& scene = districtScenes[i];
            for (const auto& lamp : scene.interiorLamps) {
                const auto p = han::core::Vec3{scene.origin.x + lamp.x, scene.origin.y + lamp.y, scene.origin.z + lamp.z};
                const float dx = p.x - camera.position.x, dz = p.z - camera.position.z;
                if (dx * dx + dz * dz > scene.interiorLightRadius * scene.interiorLightRadius || params.pointLights.size() == 32) continue;
                const float power = scene.interiorLightPower;
                params.pointLights.push_back({p, {power, power * .88f, power * .7f}, scene.interiorLightRadius});
            }
        }
        params.windowEmission = citySettings.style.windowEmission;
        params.signEmission = citySettings.style.signEmission;
        if (!manualSun) params.litWindowFraction = dayState.litWindows;
        if (sunElevation < 6.f) {  // ночью и в сумерки горят ближайшие 32 фонаря (натриевый тёплый свет)
            const float k = std::clamp((6.f - sunElevation) / 8.f, 0.f, 1.f);
            std::vector<std::pair<float, const han::ai::Car*>> nearCars;
            for (const auto& car : traffic.cars()) {
                const float dx = car.pos.x - camera.position.x, dz = car.pos.z - camera.position.z;
                if (dx * dx + dz * dz < citySettings.style.headlightRadius * citySettings.style.headlightRadius * 25.f)
                    nearCars.push_back({dx * dx + dz * dz, &car});
            }
            std::sort(nearCars.begin(), nearCars.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (std::size_t i = 0; i < std::min<std::size_t>(4, nearCars.size()); ++i) {
                const auto& car = *nearCars[i].second;
                const float ahead = citySettings.style.carLength * 0.55f;
                const float x = car.pos.x + std::sin(car.heading) * ahead, z = car.pos.z - std::cos(car.heading) * ahead;
                const float groundY = haveTerrain ? terrain.heightAt(x, z) : 0.f;
                const float power = k * citySettings.style.headlightPower;
                params.pointLights.push_back({{x, groundY + citySettings.style.carHeight * 0.44f, z}, {power, power * 0.95f, power * 0.85f}, citySettings.style.headlightRadius});
            }
            struct Near {
                float d2;
                const world::Vec3* p;
            };
            std::vector<Near> near;
            std::vector<world::Vec3> districtLamps;
            for (std::size_t i = 0; i < districtScenes.size(); ++i) if (districtGpu[i].exterior.id) {
                const auto& scene = districtScenes[i];
                for (const auto& p : scene.lamps) districtLamps.push_back({scene.origin.x + p.x, scene.origin.y + p.y, scene.origin.z + p.z});
            }
            for (const auto& lamp : districtLamps) {
                const float dx = lamp.x - camera.position.x, dy = lamp.y - camera.position.y, dz = lamp.z - camera.position.z;
                if (dx * dx + dz * dz < 250.f * 250.f) near.push_back({dx * dx + dy * dy + dz * dz, &lamp});
            }
            for (const auto& [cell, gpu] : loader.gpu)
                for (const world::Vec3& lamp : gpu.lamps) {
                    const float dx = lamp.x - camera.position.x, dz = lamp.z - camera.position.z;
                    if (dx * dx + dz * dz < 250.f * 250.f) near.push_back({dx * dx + dz * dz + (lamp.y - camera.position.y) * (lamp.y - camera.position.y), &lamp});
                }
            const std::size_t keep = std::min<std::size_t>(near.size(), 32 - params.pointLights.size());
            std::partial_sort(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(keep), near.end(), [](const Near& a, const Near& b) { return a.d2 < b.d2; });
            for (std::size_t i = 0; i < keep; ++i) {
                const auto& p = *near[i].p;
                const float groundY = haveTerrain ? terrain.heightAt(p.x, p.z) : 0.f;
                const bool shop = p.y - groundY < 5.f;
                const float power = k * citySettings.style.streetLightPower * (shop ? 0.35f : 1.f);
                params.pointLights.push_back({{p.x, p.y, p.z}, {power * mood.lampColor[0], power * mood.lampColor[1], power * mood.lampColor[2]}, citySettings.style.streetLightRadius * (shop ? 0.6f : 1.f)});
            }
        }
        const float night = std::clamp((6.f - sunElevation) / 12.f, 0.f, 1.f);
        params.exposure = exposure * (manualSun ? (citySettings.style.dayExposure + night * (citySettings.style.nightExposure - citySettings.style.dayExposure)) : dayState.exposure);
        activeLights = params.pointLights.size();
        params.fogDensity = citySettings.style.fogDensity;
        params.fogHeightFalloff = citySettings.style.fogHeightFalloff;
        params.specularAntiAliasing = citySettings.style.specularAntiAliasing;
        params.nearPlane = camera.nearZ;
        params.farPlane = camera.farZ;
        params.shadows = true;
        params.taa = taaEnabled;
        params.bloomIntensity = citySettings.style.bloomIntensity;
        const bool shootNow = !screenshotPath.empty() && maxFrames > 0 && frame == maxFrames - 1;
        if (shootNow && !renderer->requestScreenshot(screenshotPath)) {
            std::fprintf(stderr, "снимок экрана невозможен\n");
            screenshotFailed = true;
        }
        const Uint64 prepEnd = SDL_GetTicksNS();
        renderer->drawFrame(params, items, w, h);
        const Uint64 frameEnd = SDL_GetTicksNS();
        settledStreak = (stats.loading == 0 && stats.requestedThisFrame == 0) ? settledStreak + 1 : 0;
        const bool measuring = flySpeed > 0.f || settledStreak >= 30;
        if (measuring && settledAtFrame < 0) settledAtFrame = frame;
        if (measuring && frame >= static_cast<long>(warmupFrames)) frameTimes.push_back(static_cast<double>(frameEnd - wallBegin) * 1e-6);
        {
            const auto& gpuStats = renderer->lastFrameStats();
            han::render::FrameSample sample;
            sample.wallMs = static_cast<double>(frameEnd - wallBegin) * 1e-6;
            sample.cpuPrepMs = static_cast<double>(prepEnd - wallBegin) * 1e-6;
            sample.drawMs = static_cast<double>(frameEnd - prepEnd) * 1e-6;
            sample.gpuMs = gpuStats.gpuMs;
            sample.gpuSceneMs = gpuStats.gpuSceneMs;
            sample.gpuShadowMs = gpuStats.gpuShadowMs;
            sample.drawCalls = gpuStats.mainDrawCalls;
            sample.shadowDrawCalls = gpuStats.shadowDrawCalls;
            sample.triangles = static_cast<double>(gpuStats.mainTriangles);
            sample.shadowTriangles = static_cast<double>(gpuStats.shadowTriangles);
            sample.items = gpuStats.items;
            sample.cellsLoaded = stats.loaded;
            sample.cellsLoading = stats.loading;
            sample.cellRequests = stats.requestedThisFrame;
            sample.cellUnloads = stats.unloadedThisFrame;
            sample.cars = static_cast<double>(traffic.cars().size());
            sample.people = static_cast<double>(drawnPeople);
            sample.carsWithin150 = static_cast<double>(carsNear);
            sample.carsWithin400 = static_cast<double>(carsMid);
            sample.frame = frame;
            sample.cameraX = camera.position.x;
            sample.cameraZ = camera.position.z;
            if (frame % 30 == 0 || lastVideoMemory.usedMb < 0) lastVideoMemory = renderer->videoMemory();  // раз в полсекунды: запрос недорогой, но не нужен каждый кадр
            if (lastVideoMemory.valid) {
                sample.vramUsedMb = lastVideoMemory.usedMb;
                sample.vramBudgetMb = lastVideoMemory.budgetMb;
            }
#ifdef _WIN32
            PROCESS_MEMORY_COUNTERS counters{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) sample.workingSetMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
#endif
            if (measuring) metrics.add(sample);
        }
        if (shootNow && !screenshotFailed && (renderer->screenshotPending() || !renderer->screenshotError().empty())) {
            std::fprintf(stderr, "снимок экрана не сохранён: %s\n", renderer->screenshotError().c_str());
            screenshotFailed = true;
        }
    }
    std::printf("ячеек загружено: %d, треугольников: %zu\n", stats.loaded, loader.uploadedTriangles);
    std::printf("источников ночного света: %zu; трафик: %zu; прохожих: %zu\n", activeLights, traffic.cars().size(), cityCrowd && cityCrowd->crowd() ? cityCrowd->crowd()->agents().size() : 0);
    std::printf("импортированных пропсов в кадре: %zu\n", visibleProps);
    std::printf("деревьев в радиусе отрисовки: %zu\n", visibleTrees);
    if (!frameTimes.empty()) {
        double total = 0;
        for (const auto ms : frameTimes) total += ms;
        std::sort(frameTimes.begin(), frameTimes.end());
        const auto p95 = std::min(frameTimes.size() - 1, static_cast<std::size_t>(static_cast<double>(frameTimes.size()) * 0.95));
        std::printf("кадр после прогрева (CPU + ожидание GPU): среднее %.2f мс, p95 %.2f мс, кадров %zu\n", total / static_cast<double>(frameTimes.size()), frameTimes[p95], frameTimes.size());
    }
    if (!metricsPath.empty()) {
        char cameraText[160];
        std::snprintf(cameraText, sizeof(cameraText), "%.2f %.2f %.2f %.2f %.2f", cameraArgs[0], cameraArgs[1], cameraArgs[2], cameraArgs[3], cameraArgs[4]);
        const std::vector<std::pair<std::string, std::string>> meta = {
            {"gpu", renderer->deviceName()}, {"present_mode", renderer->presentModeName()}, {"window_logical", std::to_string(logicalW) + "x" + std::to_string(logicalH)}, {"window_pixels", std::to_string(pixelW) + "x" + std::to_string(pixelH)}, {"render_extent", std::to_string(extentW) + "x" + std::to_string(extentH)}, {"pixel_density", std::to_string(window->pixelDensity())}, {"display_scale", std::to_string(window->displayScale())}, {"validation_layer", renderer->validationEnabled() ? "yes" : "no"},
            {"window", std::to_string(windowSize[0]) + "x" + std::to_string(windowSize[1])}, {"camera", cameraText},
            {"hour", std::to_string(hourArg)}, {"frames_requested", std::to_string(maxFrames)}, {"traffic_requested", std::to_string(trafficCars)},
            {"deterministic", deterministic ? "yes" : "no"}, {"walk", walk ? "yes" : "no"}, {"taa", taaEnabled ? "yes" : "no"}, {"shadows", "yes"},
            {"streaming_settled_at_frame", std::to_string(settledAtFrame)}, {"cooked", cookedExplicit ? "explicit" : "default"}, {"timing_note", "wall = CPU подготовка + drawFrame (ожидание GPU); gpu_ms отстаёт на кадры в полёте"}};
        std::ofstream out(metricsPath, std::ios::binary);
        out << han::render::metricsToJson(metrics, meta);
        std::printf("метрики записаны: %s (%zu кадров)\n", metricsPath.string().c_str(), metrics.samples().size());
    }
    if (walk) {
        const auto feet = hero->state().position;
        std::printf("игрок: %.1f %.1f %.1f (земля там %.1f), на земле: %s\n", feet.x, feet.y, feet.z, haveTerrain ? terrain.heightAt(feet.x, feet.z) : 0.f, hero->state().grounded ? "да" : "нет");
    }
    const auto errors = renderer->validationErrors();
    std::printf("ошибок валидации: %u, предупреждений: %u\n", errors, renderer->validationWarnings());
    std::printf("буферы мешей: в памяти видеокарты с доступом процессора %u (%.0f МБ), через промежуточный буфер %u, в системной %u\n", renderer->meshBufferPlacement().first,
                static_cast<double>(renderer->meshVramBytes()) / 1048576.0, renderer->meshStagedBuffers(), renderer->meshBufferPlacement().second);
    if (screenshotFailed) return 2;
    return errors == 0 ? 0 : 1;
}
