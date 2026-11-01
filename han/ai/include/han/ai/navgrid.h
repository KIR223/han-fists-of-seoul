// han/ai/navgrid.h — навигация пешеходов по сетке (E11): проходимость из контуров препятствий, поиск пути A*, спрямление пути.
// Сетка плоская (улицы города), шаг по умолчанию 1 м. Контуры — здания и другие непроходимые области в метрах карты (x, z).
// Запросы НЕ потокобезопасны (внутри переиспользуется рабочая память): один запрос за раз.
#pragma once
#include <cstdint>
#include <vector>

namespace han::ai {
struct Vec2 {
    float x = 0, z = 0;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.z * s}; }

class NavGrid {
public:
    struct Settings {
        float cellSize = 1.f;
        std::size_t maxExpansions = 600000;  // потолок работы одного поиска пути (защита кадра); больше — «пути нет»
    };

    NavGrid() = default;
    // Область [minX, maxX] x [minZ, maxZ]; клетка непроходима, если её центр внутри любого контура (контуры объединяются).
    static NavGrid build(float minX, float minZ, float maxX, float maxZ, const std::vector<std::vector<Vec2>>& obstacles, const Settings& settings = {});

    int width() const { return width_; }
    int height() const { return height_; }
    float cellSize() const { return cell_; }
    float minX() const { return minX_; }
    float minZ() const { return minZ_; }
    float maxX() const { return minX_ + static_cast<float>(width_) * cell_; }
    float maxZ() const { return minZ_ + static_cast<float>(height_) * cell_; }
    std::size_t walkableCount() const { return walkable_; }
    bool valid() const { return width_ > 0 && height_ > 0; }

    bool walkable(float x, float z) const;
    // Ближайшая проходимая точка в радиусе maxRadius (метры); false — нет.
    bool nearestWalkable(Vec2 p, float maxRadius, Vec2& out) const;
    // Прямая от a до b не пересекает непроходимые клетки.
    bool lineOfSight(Vec2 a, Vec2 b) const;
    // Путь от from до to (концы подтягиваются к ближайшей проходимой точке в радиусе 15 м). out — путевые точки, первая — старт,
    // последняя — цель. false — пути нет или превышен потолок работы.
    bool findPath(Vec2 from, Vec2 to, std::vector<Vec2>& out) const;

    std::size_t lastExpansions() const { return lastExpansions_; }

private:
    bool blockedCell(int cx, int cz) const { return cx < 0 || cz < 0 || cx >= width_ || cz >= height_ || blocked_[static_cast<std::size_t>(cz) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(cx)]; }
    void cellOf(float x, float z, int& cx, int& cz) const;
    Vec2 centerOf(int cx, int cz) const { return {minX_ + (static_cast<float>(cx) + 0.5f) * cell_, minZ_ + (static_cast<float>(cz) + 0.5f) * cell_}; }

    float minX_ = 0, minZ_ = 0, cell_ = 1;
    int width_ = 0, height_ = 0;
    std::size_t walkable_ = 0, maxExpansions_ = 600000;
    std::vector<std::uint8_t> blocked_;
    // Рабочая память поиска (с «отметками» запроса, чтобы не обнулять миллионы клеток каждый раз).
    mutable std::vector<float> gCost_;
    mutable std::vector<std::int32_t> parent_;
    mutable std::vector<std::uint32_t> stamp_;
    mutable std::uint32_t currentStamp_ = 0;
    mutable std::size_t lastExpansions_ = 0;
};
}  // namespace han::ai
// Пример: auto grid = NavGrid::build(-500, -500, 500, 500, buildingFootprints); std::vector<Vec2> path; grid.findPath({0, 0}, {300, 200}, path);
