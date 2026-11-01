// han/world/streaming.h — стриминг ячеек карты и выбор детализации (E9). Чистая логика без графики и потоков:
// сам загрузчик (асинхронный) подключается через ICellLoader и сообщает о результате в onLoaded / onLoadFailed.
// Карта делится на квадратные ячейки; вокруг камеры держится кольцо загруженных ячеек, дальше — упрощённые (HLOD), ещё дальше — ничего.
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace han::world {
struct CellCoord {
    int x = 0, z = 0;
    bool operator==(const CellCoord& o) const { return x == o.x && z == o.z; }
    bool operator<(const CellCoord& o) const { return x != o.x ? x < o.x : z < o.z; }
};
struct CellCoordHash {
    std::size_t operator()(const CellCoord& c) const {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(c.x)) * 73856093u ^ static_cast<std::size_t>(static_cast<std::uint32_t>(c.z)) * 19349663u;
    }
};

class CellGrid {
public:
    explicit CellGrid(float cellSize = 256.f) : size_(cellSize > 1.f ? cellSize : 1.f) {}
    float cellSize() const { return size_; }
    CellCoord cellOf(float x, float z) const;
    // Расстояние от точки до ближайшей точки ячейки (0 — точка внутри).
    float distanceTo(CellCoord cell, float x, float z) const;
    float centerX(CellCoord cell) const { return (static_cast<float>(cell.x) + 0.5f) * size_; }
    float centerZ(CellCoord cell) const { return (static_cast<float>(cell.z) + 0.5f) * size_; }
    // Все ячейки, чьи границы ближе radius к точке.
    std::vector<CellCoord> cellsWithin(float x, float z, float radius) const;

private:
    float size_;
};

struct StreamingSettings {
    float cellSize = 256.f;
    float loadRadius = 700.f;       // ячейки ближе — должны быть загружены полностью
    float unloadRadius = 900.f;     // ячейки дальше — выгружаются (гистерезис против дрожания на границе)
    float hlodRadius = 2500.f;      // от loadRadius до сюда — упрощённая модель ячейки
    float keepRadius = 350.f;       // ближе этого радиуса ячейки не выгружаются даже при нехватке памяти
    int maxNewLoadsPerFrame = 2;
    int maxInFlight = 4;            // одновременных загрузок
    int maxUnloadsPerFrame = 2;
    std::uint64_t memoryBudgetBytes = 512ull << 20;
    std::uint64_t estimatedCellBytes = 24ull << 20;  // оценка размера ещё не загруженной ячейки
    float forwardBias = 0.5f;       // 0..1: насколько ячейки по ходу движения грузятся раньше боковых
    int retryFrames = 120;          // повтор после ошибки загрузки; удваивается при каждой новой ошибке (до 16 раз)
};

struct ICellLoader {
    virtual void beginLoad(CellCoord cell) = 0;  // начать загрузку; по завершении — StreamingManager::onLoaded или onLoadFailed
    virtual void unload(CellCoord cell) = 0;     // освободить ячейку
    virtual ~ICellLoader() = default;
};

enum class CellState : std::uint8_t { Unloaded, Loading, Loaded };
enum class CellVisual : std::uint8_t { None, Hlod, Full };

struct StreamingStats {
    int requestedThisFrame = 0, unloadedThisFrame = 0;
    int loading = 0, loaded = 0, failedWaiting = 0;
    std::uint64_t memoryBytes = 0;  // загруженное + оценка загружаемого
};

class StreamingManager {
public:
    StreamingManager(const StreamingSettings& settings, ICellLoader& loader);

    // Раз в кадр. position — камера, (dirX, dirZ) — направление движения или взгляда (можно нулевое).
    StreamingStats update(float x, float z, float dirX, float dirZ);

    void onLoaded(CellCoord cell, std::uint64_t bytes);
    void onLoadFailed(CellCoord cell);

    CellState stateOf(CellCoord cell) const;
    // Что рисовать для ячейки сейчас: загруженную ближнюю — полностью, дальнюю — упрощённой, остальное — ничего.
    CellVisual visualOf(CellCoord cell, float x, float z) const;
    const CellGrid& grid() const { return grid_; }
    const StreamingSettings& settings() const { return settings_; }
    std::uint64_t memoryBytes() const;
    std::uint64_t frame() const { return frame_; }

private:
    struct Entry {
        CellState state = CellState::Unloaded;
        std::uint64_t bytes = 0;
        std::uint64_t retryAt = 0;  // кадр, раньше которого не пробуем снова
        int failures = 0;
    };
    StreamingSettings settings_;
    CellGrid grid_;
    ICellLoader& loader_;
    std::unordered_map<CellCoord, Entry, CellCoordHash> cells_;
    std::uint64_t frame_ = 0;
};

// Выбор уровня детализации по расстоянию с гистерезисом: у каждой границы «мёртвая зона», чтобы объект не мигал между уровнями.
class LodSelector {
public:
    // switchDistances — расстояния перехода на следующий, более грубый уровень (по возрастанию). Уровней на один больше, чем границ.
    explicit LodSelector(std::vector<float> switchDistances, float hysteresis = 0.1f);
    int levelCount() const { return static_cast<int>(distances_.size()) + 1; }
    // current — уровень сейчас (-1 — объекта ещё нет). bias > 1 огрубляет раньше (слабое железо), < 1 — позже.
    int select(int current, float distance, float bias = 1.f) const;

private:
    std::vector<float> distances_;
    float hysteresis_;
};
}  // namespace han::world
// Пример: StreamingManager s(settings, loader); auto st = s.update(cam.x, cam.z, fwd.x, fwd.z); if (s.visualOf(c, cam.x, cam.z) == CellVisual::Full) draw(c);
