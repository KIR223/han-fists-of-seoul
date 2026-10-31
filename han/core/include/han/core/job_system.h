#pragma once
// Job system (E1): пул потоков с общей очередью. Задачи не должны бросать исключения.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace han::core {

class JobSystem {
public:
    // workers == 0 — взять число ядер минус одно (главный поток тоже работает).
    explicit JobSystem(unsigned workers = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(std::function<void()> job);
    // Ждёт завершения всех поставленных задач; пока ждёт, сам выполняет задачи из очереди.
    void waitIdle();
    // Делит [0, count) на куски и выполняет body(begin, end) параллельно; возвращается по завершении.
    void parallelFor(std::size_t count, std::size_t grain, const std::function<void(std::size_t, std::size_t)>& body);

    unsigned workerCount() const { return static_cast<unsigned>(threads_.size()); }

private:
    bool runOne();
    void workerLoop();

    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<std::size_t> pending_{0};
    bool stopping_ = false;
};

}  // namespace han::core
// Пример: han::core::JobSystem jobs; jobs.parallelFor(n, 256, [&](size_t b, size_t e) { for (; b < e; ++b) work(b); });
