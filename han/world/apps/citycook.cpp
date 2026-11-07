// han_citycook (E9/E16): запекает город из OSM в ячейки .hcell, чтобы игра не разбирала OSM и не строила геометрию при каждом запуске.
//   han_citycook --osm файл.osm --out папка [--origin широта долгота] [--terrain файл.hht] [--bounds minX minZ maxX maxZ] [--shop-tile метры]
//   --bounds: в папку попадают только ячейки, центр которых внутри прямоугольника (метры карты): так несколько выгрузок OSM складываются в одну папку без дублей.
// Коды выхода: 0 — готово, 1 — ошибка, 2 — неверный запуск.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

#include "han/core/repo_root.h"
#include "han/world/cell_io.h"
#include "han/world/city_builder.h"
#include "han/world/district_scene.h"
#include "han/world/osm.h"
#include "han/world/terrain.h"

using namespace han::world;

int main(int argc, char** argv) {
    han::core::enterRepoRoot(argv[0]);
    std::string osmPath, outDir, terrainPath;
    double origin[2] = {37.5570, 126.9140};
    float bounds[4] = {-1e9f, -1e9f, 1e9f, 1e9f};
    float shopTile = 0.f;
    bool originSet = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--osm") && i + 1 < argc) osmPath = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--terrain") && i + 1 < argc) terrainPath = argv[++i];
        else if (!std::strcmp(argv[i], "--shop-tile") && i + 1 < argc) shopTile = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--origin") && i + 2 < argc) {
            originSet = true;
            origin[0] = std::atof(argv[i + 1]);
            origin[1] = std::atof(argv[i + 2]);
            i += 2;
        } else if (!std::strcmp(argv[i], "--bounds") && i + 4 < argc) {
            for (int k = 0; k < 4; ++k) bounds[k] = static_cast<float>(std::atof(argv[i + 1 + k]));
            i += 4;
        } else {
            std::fprintf(stderr, "неизвестный аргумент: %s\n", argv[i]);
            return 2;
        }
    }
    if (osmPath.empty() || outDir.empty()) {
        std::fprintf(stderr, "использование: han_citycook --osm файл.osm --out папка [--origin широта долгота] [--terrain файл.hht] [--bounds minX minZ maxX maxZ] [--shop-tile м]\n");
        return 2;
    }
    OsmData osm;
    std::string error;
    if (!loadOsmFile(osmPath, osm, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    Heightmap terrain;
    if (!terrainPath.empty() && !terrain.load(terrainPath, error)) {
        std::fprintf(stderr, "рельеф: %s\n", error.c_str());
        return 1;
    }
    if (terrain.valid()) {
        std::vector<DistrictScene> scenes;
        if (!loadDistrictScenes("data/city/district_scenes.json", scenes, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        for (const auto& scene : scenes) terrain.flattenArea(scene.origin.x - scene.halfWidth, scene.origin.z - scene.halfDepth,
            scene.origin.x + scene.halfWidth, scene.origin.z + scene.halfDepth, scene.origin.y + terrain.heightAt(scene.origin.x, scene.origin.z), scene.terrainBlend);
    }
    CityBuildSettings settings;
    settings.instanceTrees = true;
    if (!loadCityStyle("data/city/streetscape.json", settings.style, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (std::filesystem::exists("data/city/height_profile.json") && !loadHeightProfile("data/city/height_profile.json", settings.heights, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    settings.terrain = terrain.valid() ? &terrain : nullptr;
    if (!loadRoadProfileSettings("data/city/road_profile.json", settings.roadProfile, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (!originSet) { origin[0] = settings.style.originLat; origin[1] = settings.style.originLon; }
    // Размер полного атласа брендов; --shop-tile сохраняется как явное переопределение.
    settings.shopTileMeters = shopTile > 0.f ? shopTile : settings.style.shopBayWidth * static_cast<float>(settings.style.signBays);
    CityStats stats;
    const auto chunks = buildCity(osm, GeoProjection(origin[0], origin[1]), settings, &stats);
    std::filesystem::create_directories(outDir);
    int written = 0;
    std::uintmax_t bytes = 0;
    for (const auto& [cell, chunk] : chunks) {
        const float cx = (static_cast<float>(cell.x) + 0.5f) * settings.cellSize, cz = (static_cast<float>(cell.z) + 0.5f) * settings.cellSize;
        if (cx < bounds[0] || cx > bounds[2] || cz < bounds[1] || cz > bounds[3] || chunk.empty()) continue;
        const std::string file = (std::filesystem::path(outDir) / cellFileName(cell)).string();
        if (!saveCell(file, cell, settings.cellSize, chunk, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        bytes += std::filesystem::file_size(file);
        ++written;
    }
    {
        std::ofstream index((std::filesystem::path(outDir) / "index.json").string());
        index << std::setprecision(10);
        index << "{\n  \"format\": \"HCL3\",\n  \"compatible_formats\": [\"HCL1\", \"HCL2\", \"HCL3\"],\n  \"cell_size\": " << settings.cellSize << ",\n  \"origin\": [" << origin[0] << ", " << origin[1] << "],\n  \"shop_tile\": " << settings.shopTileMeters
              << ",\n  \"terrain\": " << (terrain.valid() ? "true" : "false") << "\n}\n";
    }
    std::printf("запечено ячеек: %d из %zu, %.1f МБ; домов %d, дорог %d, парков %d\n", written, chunks.size(), static_cast<double>(bytes) / 1048576.0, stats.buildings, stats.roads, stats.parks);
    std::printf("мосты: путей %d, сегментов полотна %d, коллайдеров полотна %d, опор %d\n", stats.bridgeWays, stats.bridgeSegments, stats.deckColliders, stats.piers);
    {
        const auto profile = computeRoadProfile(osm, GeoProjection(origin[0], origin[1]), settings.terrain, settings.roadProfile);
        const auto& ps = profile.stats();
        std::printf("тоннели: путей с тегом %d, узлов под землёй %d, наибольшая глубина %.1f м, наибольший уклон %.1f%%, узлов выше земли %d, участков >10 м %d, из них круче 12%% %d (по поверхности земли было бы %d)\n", ps.tunnelWays, ps.tunnelNodes,
                    static_cast<double>(ps.maxTunnelDepth), static_cast<double>(ps.tunnelMaxGrade) * 100.0, ps.tunnelAboveGround, ps.tunnelSegments, ps.tunnelSteepSegments,
                    ps.tunnelSurfaceSteepSegments);
    }
    return 0;
}
