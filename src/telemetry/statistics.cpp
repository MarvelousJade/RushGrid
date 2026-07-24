#include "rushgrid/telemetry/statistics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace rushgrid {

SampleWindow::SampleWindow(const std::size_t capacity)
    : samples_(std::max<std::size_t>(capacity, 1)) {
}

void SampleWindow::push(const double value) noexcept {
    samples_[next_] = value;
    next_ = (next_ + 1) % samples_.size();
    size_ = std::min(size_ + 1, samples_.size());
}

double SampleWindow::mean() const noexcept {
    if (size_ == 0) {
        return 0.0;
    }
    return std::accumulate(samples_.begin(), samples_.begin() + size_, 0.0) /
        static_cast<double>(size_);
}

double SampleWindow::percentile(const double fraction) const {
    if (size_ == 0) {
        return 0.0;
    }
    std::vector<double> sorted(samples_.begin(), samples_.begin() + size_);
    std::sort(sorted.begin(), sorted.end());
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(clamped * static_cast<double>(sorted.size())) - 1.0);
    return sorted[std::min(index, sorted.size() - 1)];
}

double SampleWindow::minimum() const noexcept {
    return size_ == 0
        ? 0.0
        : *std::min_element(samples_.begin(), samples_.begin() + size_);
}

double SampleWindow::maximum() const noexcept {
    return size_ == 0
        ? 0.0
        : *std::max_element(samples_.begin(), samples_.begin() + size_);
}

} // namespace rushgrid
