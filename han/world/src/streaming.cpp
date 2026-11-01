#include "han/world/streaming.h"

#include <algorithm>
#include <cmath>

namespace han::world {

CellCoord CellGrid::cellOf(float x, float z) const {
    return {static_cast<int>(std::floor(x / size_)), static_cast<int>(std::floor(z / size_))};
}

float CellGrid::distanceTo(CellCoord cell, float x, float z) const {
    const float minX = static_cast<float>(cell.x) * size_, minZ = static_cast<float>(cell.z) * size_;
    const float dx = std::max({minX - x, 0.f, x - (minX + size_)});
    const float dz = std::max({minZ - z, 0.f, z - (minZ + size_)});
    return std::sqrt(dx * dx + dz * dz);
}

std::vector<CellCoord> CellGrid::cellsWithin(float x, float z, float radius) const {
    std::vector<CellCoord> out;
    if (radius < 0.f) return out;
    const CellCoord lo = cellOf(x - radius, z - radius), hi = cellOf(x + radius, z + radius);
    for (int cz = lo.z; cz <= hi.z; ++cz)
        for (int cx = lo.x; cx <= hi.x; ++cx)
            if (distanceTo({cx, cz}, x, z) <= radius) out.push_back({cx, cz});
    return out;
}

StreamingManager::StreamingManager(const StreamingSettings& settings, ICellLoader& loader)
    : settings_(settings), grid_(settings.cellSize), loader_(loader) {
    // Радиусы должны идти по возрастанию, иначе ячейки загружались бы и тут же выгружались.
    settings_.loadRadius = std::max(settings_.loadRadius, 1.f);
    settings_.unloadRadius = std::max(settings_.unloadRadius, settings_.loadRadius + settings_.cellSize * 0.25f);
    settings_.hlodRadius = std::max(settings_.hlodRadius, settings_.unloadRadius);
    settings_.keepRadius = std::clamp(settings_.keepRadius, 0.f, settings_.loadRadius);
    settings_.maxInFlight = std::max(settings_.maxInFlight, 1);
    settings_.maxNewLoadsPerFrame = std::max(settings_.maxNewLoadsPerFrame, 1);
    settings_.maxUnloadsPerFrame = std::max(settings_.maxUnloadsPerFrame, 1);
}

std::uint64_t StreamingManager::memoryBytes() const {
    std::uint64_t total = 0;
    for (const auto& [cell, e] : cells_) {
        if (e.state == CellState::Loaded) total += e.bytes;
        else if (e.state == CellState::Loading) total += settings_.estimatedCellBytes;
    }
    return total;
}

StreamingStats StreamingManager::update(float x, float z, float dirX, float dirZ) {
    ++frame_;
    StreamingStats stats;
    const float dirLen = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (dirLen > 1e-5f) {
        dirX /= dirLen;
        dirZ /= dirLen;
    }

    // 1. Выгрузка: сначала всё дальше unloadRadius, затем (если памяти не хватает) самое далёкое за пределами keepRadius.
    struct Loaded {
        CellCoord cell;
        float distance;
    };
    std::vector<Loaded> loaded;
    for (const auto& [cell, e] : cells_)
        if (e.state == CellState::Loaded) loaded.push_back({cell, grid_.distanceTo(cell, x, z)});
    std::sort(loaded.begin(), loaded.end(), [](const Loaded& a, const Loaded& b) { return a.distance != b.distance ? a.distance > b.distance : b.cell < a.cell; });
    std::uint64_t memory = memoryBytes();
    for (const Loaded& l : loaded) {
        if (stats.unloadedThisFrame >= settings_.maxUnloadsPerFrame) break;
        const bool tooFar = l.distance > settings_.unloadRadius;
        const bool needRoom = memory + settings_.estimatedCellBytes > settings_.memoryBudgetBytes && l.distance > settings_.keepRadius;
        if (!tooFar && !needRoom) break;  // список отсортирован от дальних: дальше выгружать нечего
        Entry& e = cells_[l.cell];
        memory -= std::min(memory, e.bytes);
        loader_.unload(l.cell);
        ++stats.unloadedThisFrame;
        if (e.failures == 0) cells_.erase(l.cell);
        else {
            e.state = CellState::Unloaded;
            e.bytes = 0;
        }
    }

    // 2. Загрузка: нужные ячейки в радиусе, ближние и лежащие по ходу движения — раньше.
    struct Wanted {
        CellCoord cell;
        float score;
    };
    std::vector<Wanted> wanted;
    for (const CellCoord& c : grid_.cellsWithin(x, z, settings_.loadRadius)) {
        const auto it = cells_.find(c);
        if (it != cells_.end() && (it->second.state != CellState::Unloaded || it->second.retryAt > frame_)) continue;
        const float d = grid_.distanceTo(c, x, z);
        float ahead = 0.f;
        if (dirLen > 1e-5f) {
            const float cx = grid_.centerX(c) - x, cz = grid_.centerZ(c) - z;
            const float len = std::sqrt(cx * cx + cz * cz);
            if (len > 1e-3f) ahead = std::max(0.f, (cx * dirX + cz * dirZ) / len);
        }
        wanted.push_back({c, d * (1.f - settings_.forwardBias * ahead)});
    }
    std::sort(wanted.begin(), wanted.end(), [](const Wanted& a, const Wanted& b) { return a.score != b.score ? a.score < b.score : a.cell < b.cell; });
    int inFlight = 0;
    for (const auto& [cell, e] : cells_) inFlight += e.state == CellState::Loading ? 1 : 0;
    for (const Wanted& w : wanted) {
        if (stats.requestedThisFrame >= settings_.maxNewLoadsPerFrame || inFlight >= settings_.maxInFlight) break;
        if (memory + settings_.estimatedCellBytes > settings_.memoryBudgetBytes) break;
        cells_[w.cell].state = CellState::Loading;
        memory += settings_.estimatedCellBytes;
        ++inFlight;
        ++stats.requestedThisFrame;
        loader_.beginLoad(w.cell);
    }

    for (const auto& [cell, e] : cells_) {
        if (e.state == CellState::Loading) ++stats.loading;
        else if (e.state == CellState::Loaded) ++stats.loaded;
        else if (e.retryAt > frame_) ++stats.failedWaiting;
    }
    stats.memoryBytes = memory;
    return stats;
}

void StreamingManager::onLoaded(CellCoord cell, std::uint64_t bytes) {
    const auto it = cells_.find(cell);
    if (it == cells_.end() || it->second.state != CellState::Loading) return;  // ответ на отменённую или неизвестную загрузку
    it->second.state = CellState::Loaded;
    it->second.bytes = bytes;
    it->second.failures = 0;
}

void StreamingManager::onLoadFailed(CellCoord cell) {
    const auto it = cells_.find(cell);
    if (it == cells_.end() || it->second.state != CellState::Loading) return;
    Entry& e = it->second;
    e.state = CellState::Unloaded;
    e.bytes = 0;
    e.failures = std::min(e.failures + 1, 16);
    e.retryAt = frame_ + (static_cast<std::uint64_t>(settings_.retryFrames) << std::min(e.failures - 1, 4));
}

CellState StreamingManager::stateOf(CellCoord cell) const {
    const auto it = cells_.find(cell);
    return it == cells_.end() ? CellState::Unloaded : it->second.state;
}

CellVisual StreamingManager::visualOf(CellCoord cell, float x, float z) const {
    const float d = grid_.distanceTo(cell, x, z);
    if (stateOf(cell) == CellState::Loaded && d <= settings_.unloadRadius) return CellVisual::Full;
    if (d <= settings_.hlodRadius) return CellVisual::Hlod;
    return CellVisual::None;
}

LodSelector::LodSelector(std::vector<float> switchDistances, float hysteresis) : hysteresis_(std::clamp(hysteresis, 0.f, 0.5f)) {
    std::sort(switchDistances.begin(), switchDistances.end());
    for (float d : switchDistances)
        if (d > 0.f) distances_.push_back(d);
}

int LodSelector::select(int current, float distance, float bias) const {
    const int n = static_cast<int>(distances_.size());
    const float d = distance * std::max(bias, 0.01f);
    int level = current;
    if (level < 0 || level > n) {  // объекта ещё нет: уровень без гистерезиса
        level = 0;
        while (level < n && d > distances_[static_cast<std::size_t>(level)]) ++level;
        return level;
    }
    while (level < n && d > distances_[static_cast<std::size_t>(level)] * (1.f + hysteresis_)) ++level;
    while (level > 0 && d < distances_[static_cast<std::size_t>(level - 1)] * (1.f - hysteresis_)) --level;
    return level;
}
}  // namespace han::world
