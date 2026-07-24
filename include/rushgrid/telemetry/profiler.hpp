#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rushgrid {

struct ProfileEvent {
    const char* name{};
    std::uint32_t startMicroseconds{};
    std::uint32_t durationMicroseconds{};
    std::uint32_t lane{};
};

class Profiler;

class ProfileScope {
public:
    ProfileScope() = default;
    ProfileScope(Profiler& profiler, const char* name) noexcept;
    ~ProfileScope();

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

    ProfileScope(ProfileScope&& other) noexcept;
    ProfileScope& operator=(ProfileScope&& other) noexcept;

private:
    Profiler* profiler_{};
    std::uint32_t eventIndex_{};
};

class Profiler {
public:
    explicit Profiler(std::size_t eventsPerFrame = 4096);

    void begin_frame() noexcept;
    void end_frame(std::uint64_t tick) noexcept;
    void set_enabled(const bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] ProfileScope scope(const char* name) noexcept {
        return ProfileScope(*this, name);
    }

    [[nodiscard]] std::span<const ProfileEvent> events() const noexcept;
    [[nodiscard]] std::uint64_t published_tick() const noexcept;
    [[nodiscard]] std::uint64_t dropped_events() const noexcept {
        return droppedEvents_.load(std::memory_order_relaxed);
    }

private:
    friend class ProfileScope;
    using Clock = std::chrono::steady_clock;

    std::array<std::vector<ProfileEvent>, 2> buffers_;
    std::array<std::uint32_t, 2> counts_{};
    std::array<std::uint64_t, 2> ticks_{};
    Clock::time_point frameStart_{};
    std::atomic<std::uint32_t> nextEvent_{0};
    std::atomic<std::uint32_t> publishedIndex_{0};
    std::atomic<std::uint64_t> droppedEvents_{0};
    std::uint32_t activeIndex_{1};
    bool enabled_{true};

    [[nodiscard]] std::uint32_t begin_zone(const char* name) noexcept;
    void end_zone(std::uint32_t eventIndex) noexcept;
    [[nodiscard]] std::uint32_t elapsed_microseconds() const noexcept;
};

} // namespace rushgrid
