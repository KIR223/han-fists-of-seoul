#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>
#include "han/world/geometry.h"
#include "han/world/streaming.h"

namespace han::world {
// Только визуальная маска воды: острова исключены ещё при триангуляции OSM.
class WaterSurfaceMask {
public:
    bool load(const std::string& path, std::string& error);
    bool covers(float x, float z) const;
    GeoMesh removeCoveredTerrain(const GeoMesh& source) const;
private:
    float cellSize_ = 0;
    std::vector<std::array<Vec2, 3>> triangles_;
    std::map<CellCoord, std::vector<std::size_t>> cells_;
};
}
