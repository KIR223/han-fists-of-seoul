#include "han/ai/navgrid.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace han::ai {
namespace {
constexpr float kSnapRadius = 15.f;
constexpr float kSqrt2 = 1.41421356f;
}  // namespace

void NavGrid::cellOf(float x, float z, int& cx, int& cz) const {
    cx = static_cast<int>(std::floor((x - minX_) / cell_));
    cz = static_cast<int>(std::floor((z - minZ_) / cell_));
}

NavGrid NavGrid::build(float minX, float minZ, float maxX, float maxZ, const std::vector<std::vector<Vec2>>& obstacles, const Settings& settings) {
    NavGrid g;
    g.cell_ = std::max(settings.cellSize, 0.1f);
    g.maxExpansions_ = settings.maxExpansions;
    g.minX_ = minX;
    g.minZ_ = minZ;
    if (!(maxX > minX) || !(maxZ > minZ)) return g;
    const double w = std::ceil(static_cast<double>(maxX - minX) / g.cell_), h = std::ceil(static_cast<double>(maxZ - minZ) / g.cell_);
    if (w > 16384 || h > 16384 || w * h > 64e6) return g;  // слишком большая сетка: пусть вызывающий возьмёт крупнее шаг
    g.width_ = static_cast<int>(w);
    g.height_ = static_cast<int>(h);
    const std::size_t total = static_cast<std::size_t>(g.width_) * static_cast<std::size_t>(g.height_);
    g.blocked_.assign(total, 0);

    // Заливка контуров построчно: по центрам клеток строки считаем пересечения с рёбрами, закрашиваем между парами.
    std::vector<float> xs;
    for (const auto& poly : obstacles) {
        const std::size_t n = poly.size();
        if (n < 3) continue;
        float zmin = poly[0].z, zmax = poly[0].z;
        for (const Vec2& p : poly) {
            zmin = std::min(zmin, p.z);
            zmax = std::max(zmax, p.z);
        }
        const int row0 = std::max(0, static_cast<int>(std::floor((zmin - minZ) / g.cell_)));
        const int row1 = std::min(g.height_ - 1, static_cast<int>(std::floor((zmax - minZ) / g.cell_)));
        for (int row = row0; row <= row1; ++row) {
            const float zc = minZ + (static_cast<float>(row) + 0.5f) * g.cell_;
            xs.clear();
            for (std::size_t i = 0; i < n; ++i) {
                const Vec2 a = poly[i], b = poly[(i + 1) % n];
                if ((a.z <= zc && b.z > zc) || (b.z <= zc && a.z > zc)) xs.push_back(a.x + (zc - a.z) / (b.z - a.z) * (b.x - a.x));
            }
            std::sort(xs.begin(), xs.end());
            for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
                const int c0 = std::max(0, static_cast<int>(std::ceil((xs[i] - minX) / g.cell_ - 0.5f)));
                const int c1 = std::min(g.width_ - 1, static_cast<int>(std::floor((xs[i + 1] - minX) / g.cell_ - 0.5f)));
                for (int c = c0; c <= c1; ++c) g.blocked_[static_cast<std::size_t>(row) * static_cast<std::size_t>(g.width_) + static_cast<std::size_t>(c)] = 1;
            }
        }
    }
    g.walkable_ = static_cast<std::size_t>(std::count(g.blocked_.begin(), g.blocked_.end(), std::uint8_t{0}));
    g.gCost_.assign(total, 0.f);
    g.parent_.assign(total, -1);
    g.stamp_.assign(total, 0);
    return g;
}

bool NavGrid::walkable(float x, float z) const {
    int cx, cz;
    cellOf(x, z, cx, cz);
    return !blockedCell(cx, cz);
}

bool NavGrid::nearestWalkable(Vec2 p, float maxRadius, Vec2& out) const {
    if (!valid()) return false;
    int cx, cz;
    cellOf(p.x, p.z, cx, cz);
    if (!blockedCell(cx, cz)) {
        out = p;
        return true;
    }
    const int maxR = static_cast<int>(std::ceil(maxRadius / cell_));
    float best = 1e30f;
    bool found = false;
    for (int r = 1; r <= maxR; ++r) {
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;  // только кольцо радиуса r
                if (blockedCell(cx + dx, cz + dz)) continue;
                const Vec2 c = centerOf(cx + dx, cz + dz);
                const float d = (c.x - p.x) * (c.x - p.x) + (c.z - p.z) * (c.z - p.z);
                if (d < best) {
                    best = d;
                    out = c;
                    found = true;
                }
            }
        // Кольцо следующего радиуса не может оказаться ближе найденного, если найденное ближе r клеток.
        if (found && std::sqrt(best) <= static_cast<float>(r) * cell_) break;
    }
    return found && std::sqrt(best) <= maxRadius;
}

bool NavGrid::lineOfSight(Vec2 a, Vec2 b) const {
    if (!valid()) return false;
    // Обход клеток вдоль отрезка (алгоритм Амануделя—Вуо) с проверкой каждой.
    int cx, cz, ex, ez;
    cellOf(a.x, a.z, cx, cz);
    cellOf(b.x, b.z, ex, ez);
    if (blockedCell(cx, cz) || blockedCell(ex, ez)) return false;
    const float dx = b.x - a.x, dz = b.z - a.z;
    const int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0), stepZ = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
    const float inf = 1e30f;
    float tMaxX = inf, tMaxZ = inf;
    const float tDeltaX = stepX ? cell_ / std::fabs(dx) : inf, tDeltaZ = stepZ ? cell_ / std::fabs(dz) : inf;
    if (stepX) tMaxX = ((stepX > 0 ? minX_ + static_cast<float>(cx + 1) * cell_ : minX_ + static_cast<float>(cx) * cell_) - a.x) / dx;
    if (stepZ) tMaxZ = ((stepZ > 0 ? minZ_ + static_cast<float>(cz + 1) * cell_ : minZ_ + static_cast<float>(cz) * cell_) - a.z) / dz;
    int guard = std::abs(ex - cx) + std::abs(ez - cz) + 4;
    while ((cx != ex || cz != ez) && guard-- > 0) {
        if (tMaxX < tMaxZ) {
            cx += stepX;
            tMaxX += tDeltaX;
        } else if (tMaxZ < tMaxX) {
            cz += stepZ;
            tMaxZ += tDeltaZ;
        } else {  // точно через угол: оба соседа должны быть свободны, иначе проскочим между зданиями «по диагонали»
            if (blockedCell(cx + stepX, cz) || blockedCell(cx, cz + stepZ)) return false;
            cx += stepX;
            cz += stepZ;
            tMaxX += tDeltaX;
            tMaxZ += tDeltaZ;
        }
        if (blockedCell(cx, cz)) return false;
    }
    return true;
}

bool NavGrid::findPath(Vec2 from, Vec2 to, std::vector<Vec2>& out) const {
    out.clear();
    lastExpansions_ = 0;
    if (!valid()) return false;
    Vec2 start, goal;
    if (!nearestWalkable(from, kSnapRadius, start) || !nearestWalkable(to, kSnapRadius, goal)) return false;
    if (lineOfSight(start, goal)) {
        out = {start, goal};
        return true;
    }
    int sx, sz, gx, gz;
    cellOf(start.x, start.z, sx, sz);
    cellOf(goal.x, goal.z, gx, gz);
    const auto index = [&](int x, int z) { return static_cast<std::int32_t>(static_cast<std::size_t>(z) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)); };
    const auto heuristic = [&](int x, int z) {
        const float dx = static_cast<float>(std::abs(x - gx)), dz = static_cast<float>(std::abs(z - gz));
        return (dx + dz) + (kSqrt2 - 2.f) * std::min(dx, dz);  // октильное расстояние
    };

    if (++currentStamp_ == 0) {  // переполнение счётчика: сбрасываем отметки
        std::fill(stamp_.begin(), stamp_.end(), 0u);
        currentStamp_ = 1;
    }
    struct Node {
        float f;
        std::int32_t idx;
        bool operator<(const Node& o) const { return f > o.f; }  // куча-минимум по f
    };
    std::priority_queue<Node> open;
    const std::int32_t startIdx = index(sx, sz), goalIdx = index(gx, gz);
    gCost_[static_cast<std::size_t>(startIdx)] = 0.f;
    parent_[static_cast<std::size_t>(startIdx)] = -1;
    stamp_[static_cast<std::size_t>(startIdx)] = currentStamp_;
    open.push({heuristic(sx, sz), startIdx});
    static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dzs[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    bool found = false;
    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        const int cx = cur.idx % width_, cz = cur.idx / width_;
        const float g = gCost_[static_cast<std::size_t>(cur.idx)];
        if (cur.f > g + heuristic(cx, cz) + 1e-3f) continue;  // устаревшая запись очереди
        if (cur.idx == goalIdx) {
            found = true;
            break;
        }
        if (++lastExpansions_ > maxExpansions_) return false;
        for (int k = 0; k < 8; ++k) {
            const int nx = cx + dxs[k], nz = cz + dzs[k];
            if (blockedCell(nx, nz)) continue;
            if (k >= 4 && (blockedCell(cx + dxs[k], cz) || blockedCell(cx, cz + dzs[k]))) continue;  // не срезаем углы зданий
            const std::int32_t ni = index(nx, nz);
            const float ng = g + (k < 4 ? 1.f : kSqrt2);
            const auto n = static_cast<std::size_t>(ni);
            if (stamp_[n] == currentStamp_ && ng >= gCost_[n]) continue;
            stamp_[n] = currentStamp_;
            gCost_[n] = ng;
            parent_[n] = cur.idx;
            open.push({ng + heuristic(nx, nz), ni});
        }
    }
    if (!found) return false;

    // Восстановление и спрямление: из текущей точки идём как можно дальше по прямой видимости.
    std::vector<Vec2> raw;
    for (std::int32_t i = goalIdx; i != -1; i = parent_[static_cast<std::size_t>(i)]) raw.push_back(centerOf(i % width_, i / width_));
    std::reverse(raw.begin(), raw.end());
    raw.front() = start;
    raw.back() = goal;
    out.push_back(raw.front());
    std::size_t anchor = 0;
    while (anchor + 1 < raw.size()) {
        std::size_t far = anchor + 1;
        // Бинарный поиск по прямой видимости недопустим (видимость не монотонна), поэтому идём с конца.
        for (std::size_t j = raw.size() - 1; j > anchor + 1; --j)
            if (lineOfSight(raw[anchor], raw[j])) {
                far = j;
                break;
            }
        out.push_back(raw[far]);
        anchor = far;
    }
    return true;
}
}  // namespace han::ai
