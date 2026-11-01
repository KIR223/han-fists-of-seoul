#include "han/world/geometry.h"

#include <algorithm>
#include <cmath>
#include <list>

namespace han::world {
namespace {
float cross(Vec2 a, Vec2 b, Vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

bool inTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    const float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}
}  // namespace

float signedArea(const std::vector<Vec2>& p) {
    double area = 0;
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % n];
        area += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return static_cast<float>(area * 0.5);
}

std::vector<std::uint32_t> triangulate(const std::vector<Vec2>& input) {
    std::vector<Vec2> poly = input;
    if (poly.size() >= 2 && poly.front().x == poly.back().x && poly.front().y == poly.back().y) poly.pop_back();
    const std::size_t n = poly.size();
    std::vector<std::uint32_t> result;
    if (n < 3) return result;
    const float area = signedArea(poly);
    if (std::fabs(area) < 1e-6f) return result;

    // Индексы в порядке против часовой стрелки.
    std::list<std::uint32_t> ring;
    if (area > 0) for (std::uint32_t i = 0; i < n; ++i) ring.push_back(i);
    else for (std::uint32_t i = static_cast<std::uint32_t>(n); i-- > 0;) ring.push_back(i);

    result.reserve((n - 2) * 3);
    std::size_t guard = n * n + 8;
    auto it = ring.begin();
    while (ring.size() > 3 && guard-- > 0) {
        auto prev = it == ring.begin() ? std::prev(ring.end()) : std::prev(it);
        auto next = std::next(it) == ring.end() ? ring.begin() : std::next(it);
        const Vec2 a = poly[*prev], b = poly[*it], c = poly[*next];
        bool ear = cross(a, b, c) > 1e-9f;  // выпуклая вершина
        if (ear) {
            for (auto q = ring.begin(); q != ring.end() && ear; ++q) {
                if (q == prev || q == it || q == next) continue;
                const Vec2 p = poly[*q];
                // Совпадающие с вершинами уха точки (касание) не мешают; строго внутри — мешают.
                if ((p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) || (p.x == c.x && p.y == c.y)) continue;
                if (inTriangle(p, a, b, c)) ear = false;
            }
        }
        if (ear) {
            result.push_back(*prev);
            result.push_back(*it);
            result.push_back(*next);
            it = ring.erase(it);
            if (it == ring.end()) it = ring.begin();
        } else {
            ++it;
            if (it == ring.end()) it = ring.begin();
        }
    }
    if (ring.size() == 3) {
        auto i = ring.begin();
        const std::uint32_t a = *i++, b = *i++, c = *i;
        if (std::fabs(cross(poly[a], poly[b], poly[c])) > 1e-9f) {
            result.push_back(a);
            result.push_back(b);
            result.push_back(c);
        }
    } else {
        result.clear();  // не удалось отсечь все уши: многоугольник самопересекается
    }
    return result;
}

void GeoMesh::append(const GeoMesh& other) {
    const auto base = static_cast<std::uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    for (std::uint32_t i : other.indices) indices.push_back(base + i);
}
}  // namespace han::world
