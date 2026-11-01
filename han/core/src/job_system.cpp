#include "han/core/job_system.h"

#include <algorithm>

namespace han::core {

JobSystem::JobSystem(unsigned workers) {
    if (workers == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        workers = hw > 1 ? hw - 1 : 1;
    }
    threads_.reserve(workers);
    for (unsigned i = 0; i < workers; ++i) threads_.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& t : threads_) t.join();
}

void JobSystem::submit(std::function<void()> job) {
    pending_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard lock(mutex_);
        queue_.push(std::move(job));
    }
    wake_.notify_one();
}

bool JobSystem::runOne() {
    std::function<void()> job;
    {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return false;
        job = std::move(queue_.front());
        queue_.pop();
    }
    job();
    pending_.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

void JobSystem::workerLoop() {
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
        }
        runOne();
    }
}

void JobSystem::waitIdle() {
    while (pending_.load(std::memory_order_acquire) != 0) {
        if (!runOne()) std::this_thread::yield();
    }
}

void JobSystem::parallelFor(std::size_t count, std::size_t grain,
                            const std::function<void(std::size_t, std::size_t)>& body) {
    if (count == 0) return;
    grain = std::max<std::size_t>(grain, 1);
    std::atomic<std::size_t> remaining{0};
    for (std::size_t begin = 0; begin < count; begin += grain) {
        const std::size_t end = std::min(begin + grain, count);
        remaining.fetch_add(1, std::memory_order_acq_rel);
        submit([&body, &remaining, begin, end] {
            body(begin, end);
            remaining.fetch_sub(1, std::memory_order_acq_rel);
        });
    }
    while (remaining.load(std::memory_order_acquire) != 0) {
        if (!runOne()) std::this_thread::yield();
    }
}

}  // namespace han::core
