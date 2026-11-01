#include "han/world/terrain_physics.h"

#include <cmath>

namespace han::world {

int TerrainPhysics::update(float x, float z) {
    if (!map_.valid()) return 0;
    const float size = tileSize();
    int created = 0;
    const int x0 = static_cast<int>(std::floor((x - cfg_.loadRadius) / size)), x1 = static_cast<int>(std::floor((x + cfg_.loadRadius) / size));
    const int z0 = static_cast<int>(std::floor((z - cfg_.loadRadius) / size)), z1 = static_cast<int>(std::floor((z + cfg_.loadRadius) / size));
    for (int tz = z0; tz <= z1; ++tz)
        for (int tx = x0; tx <= x1; ++tx) {
            const float ox = static_cast<float>(tx) * size, oz = static_cast<float>(tz) * size;
            if (ox + size < map_.minX() || ox > map_.maxX() || oz + size < map_.minZ() || oz > map_.maxZ()) continue;  // вне карты
            const Key key{tx, tz};
            if (tiles_.count(key)) continue;
            // Ближайшая точка плитки не дальше радиуса загрузки.
            const float cx = std::fmax(ox, std::fmin(x, ox + size)), cz = std::fmax(oz, std::fmin(z, oz + size));
            if ((cx - x) * (cx - x) + (cz - z) * (cz - z) > cfg_.loadRadius * cfg_.loadRadius) continue;
            std::vector<float> heights(static_cast<std::size_t>(cfg_.samples) * static_cast<std::size_t>(cfg_.samples));
            for (int j = 0; j < cfg_.samples; ++j)
                for (int i = 0; i < cfg_.samples; ++i)
                    heights[static_cast<std::size_t>(j) * static_cast<std::size_t>(cfg_.samples) + static_cast<std::size_t>(i)] =
                        map_.heightAt(ox + static_cast<float>(i) * cfg_.cell, oz + static_cast<float>(j) * cfg_.cell);
            const contracts::EntityId id = world_.addStaticHeightfield(ox, oz, cfg_.cell, cfg_.samples, heights, cfg_.material);
            if (id != contracts::kNoEntity) {
                tiles_[key] = id;
                ++created;
            }
        }
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        const float ox = static_cast<float>(it->first.x) * size, oz = static_cast<float>(it->first.z) * size;
        const float cx = std::fmax(ox, std::fmin(x, ox + size)), cz = std::fmax(oz, std::fmin(z, oz + size));
        if ((cx - x) * (cx - x) + (cz - z) * (cz - z) > cfg_.unloadRadius * cfg_.unloadRadius) {
            world_.destroyBody(it->second);
            it = tiles_.erase(it);
        } else {
            ++it;
        }
    }
    return created;
}

void TerrainPhysics::clear() {
    for (const auto& [key, id] : tiles_) world_.destroyBody(id);
    tiles_.clear();
}

}  // namespace han::world
