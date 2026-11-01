// Тесты стриминга ячеек и LOD (E9). Без графики и потоков: загрузчик в тесте — запоминающая заглушка с задержкой.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "han/world/streaming.h"

using namespace han::world;

namespace {
// Заглушка загрузчика: завершает загрузку через latency кадров; считает загрузки и выгрузки каждой ячейки.
struct FakeLoader final : ICellLoader {
    StreamingManager* manager = nullptr;
    int latency = 3;
    std::uint64_t bytes = 20ull << 20;
    std::vector<std::pair<std::uint64_t, CellCoord>> pending;
    std::uint64_t now = 0;
    std::map<CellCoord, int> loads, unloads;
    std::set<CellCoord> failOnce;
    void beginLoad(CellCoord c) override {
        ++loads[c];
        pending.push_back({now + static_cast<std::uint64_t>(latency), c});
    }
    void unload(CellCoord c) override { ++unloads[c]; }
    void tick() {
        ++now;
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->first <= now) {
                if (failOnce.erase(it->second)) manager->onLoadFailed(it->second);
                else manager->onLoaded(it->second, bytes);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
    }
};

StreamingSettings smallSettings() {
    StreamingSettings s;
    s.cellSize = 100.f;
    s.loadRadius = 250.f;
    s.unloadRadius = 320.f;
    s.hlodRadius = 800.f;
    s.keepRadius = 120.f;
    s.maxNewLoadsPerFrame = 2;
    s.maxInFlight = 4;
    s.maxUnloadsPerFrame = 2;
    s.estimatedCellBytes = 20ull << 20;
    s.memoryBudgetBytes = 4000ull << 20;
    return s;
}
}  // namespace

TEST_CASE("streaming: сетка — ячейка точки, отрицательные координаты, расстояние до ячейки") {
    CellGrid g(100.f);
    CHECK(g.cellOf(50.f, 50.f) == CellCoord{0, 0});
    CHECK(g.cellOf(-0.1f, 0.f) == CellCoord{-1, 0});
    CHECK(g.cellOf(-250.f, 199.9f) == CellCoord{-3, 1});
    CHECK(g.distanceTo({0, 0}, 50.f, 50.f) == doctest::Approx(0.f));
    CHECK(g.distanceTo({1, 0}, 50.f, 50.f) == doctest::Approx(50.f));
    CHECK(g.distanceTo({1, 1}, 0.f, 0.f) == doctest::Approx(std::sqrt(2.f) * 100.f));
    CHECK(g.centerX({2, 0}) == doctest::Approx(250.f));
}

TEST_CASE("streaming: cellsWithin отдаёт ровно ячейки в радиусе, включая угловые по расстоянию до границы") {
    CellGrid g(100.f);
    const auto cells = g.cellsWithin(50.f, 50.f, 120.f);
    for (const auto& c : cells) CHECK(g.distanceTo(c, 50.f, 50.f) <= 120.f);
    CHECK(std::find(cells.begin(), cells.end(), CellCoord{0, 0}) != cells.end());
    CHECK(std::find(cells.begin(), cells.end(), CellCoord{-1, 0}) != cells.end());  // граница ячейки в 50 м
    CHECK(std::find(cells.begin(), cells.end(), CellCoord{1, 0}) != cells.end());   // граница в 50 м
    CHECK(std::find(cells.begin(), cells.end(), CellCoord{2, 0}) == cells.end());   // граница в 150 м > 120
    CHECK(std::find(cells.begin(), cells.end(), CellCoord{1, 1}) != cells.end());   // угол: sqrt(50^2 + 50^2) = 71 м
    CHECK(g.cellsWithin(0.f, 0.f, -1.f).empty());
}

TEST_CASE("streaming: стоя на месте, загружает кольцо ячеек порциями и затихает") {
    FakeLoader loader;
    StreamingManager m(smallSettings(), loader);
    loader.manager = &m;
    int maxRequested = 0, maxLoading = 0;
    StreamingStats last;
    for (int f = 0; f < 120; ++f) {
        last = m.update(50.f, 50.f, 0.f, 0.f);
        maxRequested = std::max(maxRequested, last.requestedThisFrame);
        maxLoading = std::max(maxLoading, last.loading);
        loader.tick();
    }
    CHECK(maxRequested <= 2);
    CHECK(maxLoading <= 4);
    CHECK(last.requestedThisFrame == 0);
    CHECK(last.loading == 0);
    const auto needed = m.grid().cellsWithin(50.f, 50.f, 250.f);
    CHECK(last.loaded == static_cast<int>(needed.size()));
    for (const auto& c : needed) {
        CHECK(m.stateOf(c) == CellState::Loaded);
        CHECK(loader.loads[c] == 1);
    }
}

TEST_CASE("streaming: сначала грузится ячейка под камерой, по ходу движения — раньше боковых") {
    FakeLoader loader;
    StreamingSettings s = smallSettings();
    s.maxNewLoadsPerFrame = 1;
    s.maxInFlight = 1;
    std::vector<CellCoord> order;
    struct Spy final : ICellLoader {
        ICellLoader& inner;
        std::vector<CellCoord>& order;
        Spy(ICellLoader& i, std::vector<CellCoord>& o) : inner(i), order(o) {}
        void beginLoad(CellCoord c) override {
            order.push_back(c);
            inner.beginLoad(c);
        }
        void unload(CellCoord c) override { inner.unload(c); }
    } spy(loader, order);
    StreamingManager m2(s, spy);
    loader.manager = &m2;
    for (int f = 0; f < 60; ++f) {
        m2.update(50.f, 50.f, 1.f, 0.f);  // движемся вдоль +X
        loader.tick();
    }
    REQUIRE(order.size() > 3);
    CHECK(order[0] == CellCoord{0, 0});
    // Из двух ячеек на одинаковом расстоянии (по ходу и против) первой должна прийти та, что по ходу.
    const auto pos = [&](CellCoord c) { return std::find(order.begin(), order.end(), c) - order.begin(); };
    CHECK(pos({2, 0}) < pos({-2, 0}));
}

TEST_CASE("streaming: гистерезис — ячейка между loadRadius и unloadRadius не выгружается и не грузится заново") {
    FakeLoader loader;
    StreamingManager m(smallSettings(), loader);
    loader.manager = &m;
    for (int f = 0; f < 80; ++f) {
        m.update(50.f, 50.f, 0.f, 0.f);
        loader.tick();
    }
    // Камера дрожит туда-сюда в 40 м: граница радиуса загрузки гуляет, но ничего не должно перезагружаться.
    for (int f = 0; f < 400; ++f) {
        m.update(50.f + (f % 2 ? 20.f : -20.f), 50.f, 0.f, 0.f);
        loader.tick();
    }
    for (const auto& [c, n] : loader.loads) CHECK_MESSAGE(n <= 2, "ячейка (" << c.x << "," << c.z << ") грузилась " << n << " раз");
    int total = 0;
    for (const auto& [c, n] : loader.unloads) total += n;
    CHECK(total <= 6);  // дрожание не вызывает лавины выгрузок
}

TEST_CASE("streaming: пролёт 6 км на скорости 100 м/с — без перегрузки по бюджетам, впереди всё готово") {
    FakeLoader loader;
    loader.latency = 6;
    StreamingSettings s = smallSettings();
    s.cellSize = 256.f;
    s.loadRadius = 700.f;
    s.unloadRadius = 900.f;
    s.hlodRadius = 2500.f;
    s.keepRadius = 300.f;
    s.memoryBudgetBytes = 900ull << 20;
    StreamingManager m(s, loader);
    loader.manager = &m;
    float x = 0.f;
    int maxReq = 0, maxUnl = 0, missingUnderCamera = 0;
    std::uint64_t maxMem = 0;
    for (int f = 0; f < 3600; ++f) {  // 60 с по 100 м/с = 6 км
        x += 100.f / 60.f;
        const auto st = m.update(x, 0.f, 1.f, 0.f);
        maxReq = std::max(maxReq, st.requestedThisFrame);
        maxUnl = std::max(maxUnl, st.unloadedThisFrame);
        maxMem = std::max(maxMem, st.memoryBytes);
        loader.tick();
        if (f > 200 && m.visualOf(m.grid().cellOf(x, 0.f), x, 0.f) != CellVisual::Full) ++missingUnderCamera;
    }
    CHECK(maxReq <= s.maxNewLoadsPerFrame);
    CHECK(maxUnl <= s.maxUnloadsPerFrame);
    CHECK(maxMem <= s.memoryBudgetBytes);
    CHECK(missingUnderCamera == 0);  // под камерой ячейка всегда загружена (нет «дыр в земле»)
    CHECK(m.stateOf(m.grid().cellOf(x + 300.f, 0.f)) == CellState::Loaded);  // и впереди уже готово
    CHECK(m.stateOf({0, 0}) == CellState::Unloaded);  // позади выгружено
}

TEST_CASE("streaming: нехватка памяти выгружает самые далёкие вне keepRadius, ближние остаются") {
    FakeLoader loader;
    StreamingSettings s = smallSettings();
    s.memoryBudgetBytes = 200ull << 20;  // всего 10 ячеек по 20 МБ при радиусе в ~25 ячеек
    StreamingManager m(s, loader);
    loader.manager = &m;
    std::uint64_t maxMem = 0;
    for (int f = 0; f < 200; ++f) {
        const auto st = m.update(50.f, 50.f, 0.f, 0.f);
        maxMem = std::max(maxMem, st.memoryBytes);
        loader.tick();
    }
    CHECK(maxMem <= s.memoryBudgetBytes);
    CHECK(m.stateOf({0, 0}) == CellState::Loaded);  // под камерой — на месте
}

TEST_CASE("streaming: ошибка загрузки — повтор не раньше retryFrames, потом всё доезжает") {
    FakeLoader loader;
    StreamingSettings s = smallSettings();
    s.retryFrames = 30;
    StreamingManager m(s, loader);
    loader.manager = &m;
    loader.failOnce.insert({0, 0});
    for (int f = 0; f < 25; ++f) {
        m.update(50.f, 50.f, 0.f, 0.f);
        loader.tick();
    }
    CHECK(loader.loads[CellCoord{0, 0}] == 1);  // неудача случилась, но повтора ещё нет
    CHECK(m.stateOf({0, 0}) == CellState::Unloaded);
    for (int f = 0; f < 120; ++f) {
        m.update(50.f, 50.f, 0.f, 0.f);
        loader.tick();
    }
    CHECK(loader.loads[CellCoord{0, 0}] == 2);
    CHECK(m.stateOf({0, 0}) == CellState::Loaded);
}

TEST_CASE("streaming: visualOf — полная, упрощённая, никакая; ответы о неизвестных ячейках игнорируются") {
    FakeLoader loader;
    StreamingManager m(smallSettings(), loader);
    loader.manager = &m;
    m.onLoaded({50, 50}, 1);  // такую загрузку никто не просил
    CHECK(m.stateOf({50, 50}) == CellState::Unloaded);
    m.onLoadFailed({51, 51});
    for (int f = 0; f < 60; ++f) {
        m.update(50.f, 50.f, 0.f, 0.f);
        loader.tick();
    }
    CHECK(m.visualOf({0, 0}, 50.f, 50.f) == CellVisual::Full);
    CHECK(m.visualOf({5, 0}, 50.f, 50.f) == CellVisual::Hlod);   // ~450 м: вне loadRadius, в пределах hlodRadius
    CHECK(m.visualOf({20, 0}, 50.f, 50.f) == CellVisual::None);  // ~1950 м
}

TEST_CASE("streaming: странные настройки приводятся к рабочим") {
    FakeLoader loader;
    StreamingSettings s;
    s.loadRadius = 500.f;
    s.unloadRadius = 100.f;  // меньше loadRadius — иначе вечная перезагрузка
    s.hlodRadius = 0.f;
    s.maxNewLoadsPerFrame = 0;
    s.maxInFlight = 0;
    StreamingManager m(s, loader);
    CHECK(m.settings().unloadRadius > m.settings().loadRadius);
    CHECK(m.settings().hlodRadius >= m.settings().unloadRadius);
    CHECK(m.settings().maxNewLoadsPerFrame >= 1);
    CHECK(m.settings().maxInFlight >= 1);
}

TEST_CASE("lod: уровни по расстоянию, гистерезис, смещение качества") {
    LodSelector lod({50.f, 120.f, 300.f}, 0.1f);
    CHECK(lod.levelCount() == 4);
    CHECK(lod.select(-1, 10.f) == 0);
    CHECK(lod.select(-1, 80.f) == 1);
    CHECK(lod.select(-1, 200.f) == 2);
    CHECK(lod.select(-1, 1000.f) == 3);
    // На границе 50 м уровень не мигает: уходим на 1 только за 55 м, возвращаемся на 0 только ближе 45 м.
    CHECK(lod.select(0, 52.f) == 0);
    CHECK(lod.select(0, 56.f) == 1);
    CHECK(lod.select(1, 48.f) == 1);
    CHECK(lod.select(1, 44.f) == 0);
    // Прыжок через несколько уровней при быстром движении.
    CHECK(lod.select(0, 1000.f) == 3);
    CHECK(lod.select(3, 10.f) == 0);
    // Смещение: на слабом железе (bias 2) огрубляем вдвое раньше.
    CHECK(lod.select(-1, 30.f, 2.f) == 1);
    CHECK(lod.select(-1, 30.f, 0.5f) == 0);
}

TEST_CASE("lod: дрожание расстояния вокруг границы не меняет уровень") {
    LodSelector lod({100.f}, 0.1f);
    int level = lod.select(-1, 95.f);
    int changes = 0;
    for (int i = 0; i < 1000; ++i) {
        const float d = 100.f + ((i * 37) % 17 - 8) * 0.5f;  // 96..104 м
        const int next = lod.select(level, d);
        changes += next != level;
        level = next;
    }
    CHECK(changes == 0);
}

TEST_CASE("lod: пустой и некорректный список границ") {
    LodSelector none({}, 0.1f);
    CHECK(none.levelCount() == 1);
    CHECK(none.select(-1, 1e6f) == 0);
    LodSelector messy({300.f, -5.f, 0.f, 100.f}, 9.f);  // отрицательные отбрасываются, порядок наводится
    CHECK(messy.levelCount() == 3);
    CHECK(messy.select(-1, 150.f) == 1);
}
