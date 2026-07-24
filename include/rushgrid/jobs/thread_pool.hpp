#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace rushgrid {

struct JobSystemStats {
    std::uint64_t executed{};
    std::uint64_t stolen{};
    std::uint64_t maximumQueueDepth{};
};

class ThreadPool {
public:
    explicit ThreadPool(std::size_t workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(std::function<void()> job);
    void wait_for_idle();

    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }
    [[nodiscard]] JobSystemStats stats() const noexcept;
    void reset_stats() noexcept;

    template <typename Function>
    void parallel_for(
        const std::size_t count,
        const std::size_t minimumGrain,
        Function&& function) {
        if (count == 0) {
            return;
        }

        if (workers_.empty() || count <= minimumGrain) {
            function(std::size_t{0}, count);
            return;
        }

        const std::size_t targetJobs = workers_.size() * 4;
        const std::size_t grain = std::max(
            minimumGrain,
            (count + targetJobs - 1) / targetJobs);

        for (std::size_t begin = 0; begin < count; begin += grain) {
            const std::size_t end = std::min(begin + grain, count);
            submit([begin, end, &function] { function(begin, end); });
        }
        wait_for_idle();
    }

private:
    struct alignas(64) Worker {
        std::mutex mutex;
        std::deque<std::function<void()>> queue;
        std::thread thread;
    };

    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> pending_{0};
    std::atomic<std::size_t> nextWorker_{0};
    std::atomic<std::uint64_t> executed_{0};
    std::atomic<std::uint64_t> stolen_{0};
    std::atomic<std::uint64_t> maximumQueueDepth_{0};
    mutable std::mutex wakeMutex_;
    std::condition_variable wakeCondition_;
    std::condition_variable idleCondition_;

    void worker_loop(std::size_t workerIndex);
    bool try_take(std::size_t workerIndex, std::function<void()>& job);
    bool try_steal(std::size_t thiefIndex, std::function<void()>& job);
    bool try_run_one(std::size_t helperIndex);
    void complete_one();
};

} // namespace rushgrid

