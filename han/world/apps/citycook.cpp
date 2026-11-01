// han_citycook (E9/E16): запекает город из OSM в ячейки .hcell, чтобы игра не разбирала OSM и не строила геометрию при каждом запуске.
//   han_citycook --osm файл.osm --out папка [--origin широта долгота] [--terrain файл.hht] [--bounds minX minZ maxX maxZ] [--shop-tile метры]
//   --bounds: в папку попадают только ячейки, центр которых внутри прямоугольника (метры карты): так несколько выгрузок OSM складываются в одну папку без дублей.
// Коды выхода: 0 — готово, 1 — ошибка, 2 — неверный запуск.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "han/core/repo_root.h"
#include "han/world/cell_io.h"
#include "han/world/city_builder.h"
#include "han/world/osm.h"
#include "han/world/terrain.h"

using namespace han::world;

int main(int argc, char** argv) {
    han::core::enterRepoRoot(argv[0]);
    std::string osmPath, outDir, terrainPath;
    double origin[2] = {37.5570, 126.9140};
    float bounds[4] = {-1e9f, -1e9f, 1e9f, 1e9f};
    float shopTile = 76.8f;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--osm") && i + 1 < argc) osmPath = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--terrain") && i + 1 < argc) terrainPath = argv[++i];
        else if (!std::strcmp(argv[i], "--shop-tile") && i + 1 < argc) shopTile = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--origin") && i + 2 < argc) {
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
    CityBuildSettings settings;
    settings.terrain = terrain.valid() ? &terrain : nullptr;
    settings.shopTileMeters = shopTile;
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
        index << "{\n  \"format\": \"HCL1\",\n  \"cell_size\": " << settings.cellSize << ",\n  \"origin\": [" << origin[0] << ", " << origin[1] << "],\n  \"shop_tile\": " << shopTile
              << ",\n  \"terrain\": " << (terrain.valid() ? "true" : "false") << "\n}\n";
    }
    std::printf("запечено ячеек: %d из %zu, %.1f МБ; домов %d, дорог %d, парков %d\n", written, chunks.size(), static_cast<double>(bytes) / 1048576.0, stats.buildings, stats.roads, stats.parks);
    return 0;
}
