// han/world/far_skyline.h — дальний силуэт города (этап 08d плана): высокие здания грубыми коробками по плиткам.
// Стриминг загружает ячейки только в радиусе ~900 м, за ним зданий нет. Силуэт строится один раз из OSM (те же высоты, что у ближнего города,
// включая профиль высот) и рисуется только для плиток вне радиуса загрузки, поэтому с ближними ячейками не пересекается.
#pragma once
#include <map>
#include <utility>

#include "han/world/city_builder.h"
#include "han/world/geometry.h"
#include "han/world/osm.h"
#include "han/world/terrain.h"

namespace han::world {

struct FarSkylineSettings {
    float minHeight = 30.f;   // ниже — в силуэт не попадает (мелкая застройка далеко не видна)
    float minArea = 120.f;    // м²: меньшие основания пропускаются
    float tileSize = 512.f;   // сторона плитки, м
    float tile = 6.4f;        // сторона плитки текстуры стены, м
};

struct FarSkylineStats {
    int buildings = 0, skippedLow = 0, skippedSmall = 0, triangles = 0, tiles = 0;
};

// Плитки силуэта: ключ — целые координаты плитки (x, z), значение — меш с частями WallConcrete и Roof.
using FarSkylineTiles = std::map<std::pair<int, int>, GeoMesh>;

FarSkylineTiles buildFarSkyline(const OsmData& data, const GeoProjection& projection, const CityBuildSettings& settings, const Heightmap* terrain,
                                const FarSkylineSettings& skyline = {}, FarSkylineStats* stats = nullptr);

}  // namespace han::world
