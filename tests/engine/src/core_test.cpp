#include <doctest/doctest.h>

#include <atomic>
#include <numeric>
#include <vector>

#include "han/core/console.h"
#include "han/core/fixed_step.h"
#include "han/core/job_system.h"

using namespace han::core;

TEST_CASE("JobSystem: parallelFor обходит каждый элемент ровно один раз") {
    JobSystem jobs(3);
    std::vector<int> data(10000, 0);
    jobs.parallelFor(data.size(), 128, [&](std::size_t b, std::size_t e) {
        for (; b < e; ++b) ++data[b];
    });
    CHECK(std::accumulate(data.begin(), data.end(), 0) == 10000);
    for (int v : data) REQUIRE(v == 1);
}

TEST_CASE("JobSystem: waitIdle ждёт все задачи") {
    JobSystem jobs(2);
    std::atomic<int> done{0};
    for (int i = 0; i < 500; ++i) jobs.submit([&] { done.fetch_add(1); });
    jobs.waitIdle();
    CHECK(done.load() == 500);
}

TEST_CASE("JobSystem: пустой parallelFor ничего не делает") {
    JobSystem jobs(1);
    bool called = false;
    jobs.parallelFor(0, 8, [&](std::size_t, std::size_t) { called = true; });
    CHECK_FALSE(called);
}

TEST_CASE("FixedStepClock: шаги и интерполяция") {
    FixedStepClock clock(0.01, 5);
    CHECK(clock.advance(0.025) == 2);
    CHECK(clock.alpha() == doctest::Approx(0.5));
    CHECK(clock.advance(0.005) == 1);
}

TEST_CASE("FixedStepClock: долгий кадр ограничен, остаток сбрасывается") {
    FixedStepClock clock(0.01, 5);
    CHECK(clock.advance(10.0) == 5);
    CHECK(clock.alpha() == doctest::Approx(0.0));
    CHECK(clock.advance(-1.0) == 0);
}

TEST_CASE("Console: команды, аргументы в кавычках, help") {
    Console c;
    c.registerCommand("echo", "повторить", [](const std::vector<std::string>& a) {
        std::string s;
        for (const auto& x : a) s += "[" + x + "]";
        return s;
    });
    CHECK(c.execute("echo a \"b c\" d") == "[a][b c][d]");
    CHECK(c.execute("   ").empty());
    CHECK(c.execute("nope").find("неизвестная") != std::string::npos);
    CHECK(c.execute("help").find("echo") != std::string::npos);
    CHECK(c.history().size() == 3);
}
