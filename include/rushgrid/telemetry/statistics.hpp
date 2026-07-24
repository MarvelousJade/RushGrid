#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rushgrid {

class SampleWindow {
public:
    explicit SampleWindow(std::size_t capacity = 4096);

    void push(double value) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] double mean() const noexcept;
    [[nodiscard]] double percentile(double fraction) const;
    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;

private:
    std::vector<double> samples_;
    std::size_t next_{};
    std::size_t size_{};
};

struct TickMetrics {
    double totalMs{};
    double navigationMs{};
    double gridMs{};
    double steeringMs{};
    double integrationMs{};
    std::uint32_t maximumCellOccupancy{};
    std::uint32_t arrivals{};
    std::uint64_t jobsExecuted{};
    std::uint64_t jobsStolen{};
};

struct PerformanceSummary {
    double averageMs{};
    double p50Ms{};
    double p95Ms{};
    double p99Ms{};
    double minimumMs{};
    double maximumMs{};
    double updatesPerSecond{};
};

} // namespace rushgrid

