#include "rushgrid/jobs/thread_pool.hpp"

#include <chrono>
#include <limits>

namespace rushgrid {

ThreadPool::ThreadPool(const std::size_t workerCount) {
    workers_.reserve(workerCount);
    for (std::size_t index = 0; index < workerCount; ++index) {
        workers_.push_back(std::make_unique<Worker>());
    }
    for (std::size_t index = 0; index < workers_.size(); ++index) {
        workers_[index]->thread = std::thread([this, index] { worker_loop(index); });
    }
}

ThreadPool::~ThreadPool() {
    wait_for_idle();
    stopping_.store(true, std::memory_order_release);
    wakeCondition_.notify_all();
    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
}

void ThreadPool::submit(std::function<void()> job) {
    if (workers_.empty()) {
        job();
        return;
    }

    pending_.fetch_add(1, std::memory_order_release);
    const std::size_t target =
        nextWorker_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
    std::uint64_t depth{};
    {
        std::scoped_lock lock(workers_[target]->mutex);
        workers_[target]->queue.push_front(std::move(job));
        depth = workers_[target]->queue.size();
    }

    std::uint64_t recorded = maximumQueueDepth_.load(std::memory_order_relaxed);
    while (recorded < depth &&
           !maximumQueueDepth_.compare_exchange_weak(
               recorded,
               depth,
               std::memory_order_relaxed)) {
    }
    wakeCondition_.notify_one();
}

bool ThreadPool::try_take(
    const std::size_t workerIndex,
    std::function<void()>& job) {
    if (workerIndex >= workers_.size()) {
        return false;
    }
    std::scoped_lock lock(workers_[workerIndex]->mutex);
    if (workers_[workerIndex]->queue.empty()) {
        return false;
    }
    job = std::move(workers_[workerIndex]->queue.front());
    workers_[workerIndex]->queue.pop_front();
    return true;
}

bool ThreadPool::try_steal(
    const std::size_t thiefIndex,
    std::function<void()>& job) {
    if (workers_.empty()) {
        return false;
    }
    for (std::size_t offset = 1; offset <= workers_.size(); ++offset) {
        const std::size_t victim = (thiefIndex + offset) % workers_.size();
        if (victim == thiefIndex) {
            continue;
        }
        std::scoped_lock lock(workers_[victim]->mutex);
        if (workers_[victim]->queue.empty()) {
            continue;
        }
        job = std::move(workers_[victim]->queue.back());
        workers_[victim]->queue.pop_back();
        stolen_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool ThreadPool::try_run_one(const std::size_t helperIndex) {
    std::function<void()> job;
    if (helperIndex < workers_.size()) {
        if (!try_take(helperIndex, job) && !try_steal(helperIndex, job)) {
            return false;
        }
    } else {
        bool found = false;
        for (std::size_t index = 0; index < workers_.size(); ++index) {
            std::scoped_lock lock(workers_[index]->mutex);
            if (workers_[index]->queue.empty()) {
                continue;
            }
            job = std::move(workers_[index]->queue.back());
            workers_[index]->queue.pop_back();
            found = true;
            break;
        }
        if (!found) {
            return false;
        }
    }

    job();
    executed_.fetch_add(1, std::memory_order_relaxed);
    complete_one();
    return true;
}

void ThreadPool::complete_one() {
    const std::size_t previous = pending_.fetch_sub(1, std::memory_order_acq_rel);
    if (previous == 1) {
        idleCondition_.notify_all();
    }
}

void ThreadPool::worker_loop(const std::size_t workerIndex) {
    while (!stopping_.load(std::memory_order_acquire)) {
        if (try_run_one(workerIndex)) {
            continue;
        }
        std::unique_lock lock(wakeMutex_);
        wakeCondition_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

void ThreadPool::wait_for_idle() {
    if (workers_.empty()) {
        return;
    }

    constexpr std::size_t externalHelper = std::numeric_limits<std::size_t>::max();
    while (pending_.load(std::memory_order_acquire) != 0) {
        if (!try_run_one(externalHelper)) {
            std::unique_lock lock(wakeMutex_);
            idleCondition_.wait_for(lock, std::chrono::milliseconds(1));
        }
    }
}

JobSystemStats ThreadPool::stats() const noexcept {
    return {
        executed_.load(std::memory_order_relaxed),
        stolen_.load(std::memory_order_relaxed),
        maximumQueueDepth_.load(std::memory_order_relaxed)};
}

void ThreadPool::reset_stats() noexcept {
    executed_.store(0, std::memory_order_relaxed);
    stolen_.store(0, std::memory_order_relaxed);
    maximumQueueDepth_.store(0, std::memory_order_relaxed);
}

} // namespace rushgrid

