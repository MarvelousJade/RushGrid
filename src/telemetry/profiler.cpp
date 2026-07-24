#include "rushgrid/telemetry/profiler.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <thread>
#include <utility>

namespace rushgrid {
namespace {

constexpr std::uint32_t invalidEvent = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] std::uint32_t current_lane() noexcept {
    thread_local const std::uint32_t lane = static_cast<std::uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return lane;
}

} // namespace

ProfileScope::ProfileScope(Profiler& profiler, const char* name) noexcept
    : profiler_(&profiler),
      eventIndex_(profiler.begin_zone(name)) {
    if (eventIndex_ == invalidEvent) {
        profiler_ = nullptr;
    }
}

ProfileScope::~ProfileScope() {
    if (profiler_ != nullptr) {
        profiler_->end_zone(eventIndex_);
    }
}

ProfileScope::ProfileScope(ProfileScope&& other) noexcept
    : profiler_(std::exchange(other.profiler_, nullptr)),
      eventIndex_(other.eventIndex_) {
}

ProfileScope& ProfileScope::operator=(ProfileScope&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (profiler_ != nullptr) {
        profiler_->end_zone(eventIndex_);
    }
    profiler_ = std::exchange(other.profiler_, nullptr);
    eventIndex_ = other.eventIndex_;
    return *this;
}

Profiler::Profiler(const std::size_t eventsPerFrame) {
    const std::size_t capacity = std::max<std::size_t>(eventsPerFrame, 1);
    for (auto& buffer : buffers_) {
        buffer.resize(capacity);
    }
}

void Profiler::begin_frame() noexcept {
    activeIndex_ =
        1U - publishedIndex_.load(std::memory_order_relaxed);
    nextEvent_.store(0, std::memory_order_relaxed);
    frameStart_ = Clock::now();
}

void Profiler::end_frame(const std::uint64_t tick) noexcept {
    counts_[activeIndex_] = std::min<std::uint32_t>(
        nextEvent_.load(std::memory_order_acquire),
        static_cast<std::uint32_t>(buffers_[activeIndex_].size()));
    ticks_[activeIndex_] = tick;
    publishedIndex_.store(activeIndex_, std::memory_order_release);
}

std::span<const ProfileEvent> Profiler::events() const noexcept {
    const std::uint32_t published =
        publishedIndex_.load(std::memory_order_acquire);
    return {
        buffers_[published].data(),
        static_cast<std::size_t>(counts_[published])};
}

std::uint64_t Profiler::published_tick() const noexcept {
    const std::uint32_t published =
        publishedIndex_.load(std::memory_order_acquire);
    return ticks_[published];
}

std::uint32_t Profiler::begin_zone(const char* name) noexcept {
    if (!enabled_) {
        return invalidEvent;
    }
    const std::uint32_t index =
        nextEvent_.fetch_add(1, std::memory_order_relaxed);
    auto& buffer = buffers_[activeIndex_];
    if (index >= buffer.size()) {
        droppedEvents_.fetch_add(1, std::memory_order_relaxed);
        return invalidEvent;
    }
    buffer[index] = {
        name,
        elapsed_microseconds(),
        0,
        current_lane()};
    return index;
}

void Profiler::end_zone(const std::uint32_t eventIndex) noexcept {
    ProfileEvent& event = buffers_[activeIndex_][eventIndex];
    const std::uint32_t end = elapsed_microseconds();
    event.durationMicroseconds = end >= event.startMicroseconds
        ? end - event.startMicroseconds
        : 0;
}

std::uint32_t Profiler::elapsed_microseconds() const noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - frameStart_).count();
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        elapsed,
        0,
        std::numeric_limits<std::uint32_t>::max()));
}

} // namespace rushgrid
