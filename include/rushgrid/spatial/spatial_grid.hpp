#pragma once

#include "rushgrid/core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rushgrid {

class AgentStorage;

class SpatialGrid {
public:
    SpatialGrid(float worldWidth, float worldHeight, float cellSize, std::size_t capacity);

    void build(AgentStorage& agents);

    [[nodiscard]] CellId cell_at(float x, float y) const noexcept;
    [[nodiscard]] std::uint32_t occupancy(CellId cell) const noexcept;
    [[nodiscard]] std::uint32_t maximum_occupancy() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] float cell_size() const noexcept { return cellSize_; }

    template <typename Function>
    void for_each_nearby(
        const float x,
        const float y,
        const float radius,
        Function&& function) const {
        const int centerX = std::clamp(
            static_cast<int>(x / cellSize_),
            0,
            static_cast<int>(columns_) - 1);
        const int centerY = std::clamp(
            static_cast<int>(y / cellSize_),
            0,
            static_cast<int>(rows_) - 1);
        const int extent = std::max(1, static_cast<int>(std::ceil(radius / cellSize_)));

        for (int offsetY = -extent; offsetY <= extent; ++offsetY) {
            const int cellY = centerY + offsetY;
            if (cellY < 0 || cellY >= static_cast<int>(rows_)) {
                continue;
            }
            for (int offsetX = -extent; offsetX <= extent; ++offsetX) {
                const int cellX = centerX + offsetX;
                if (cellX < 0 || cellX >= static_cast<int>(columns_)) {
                    continue;
                }
                const CellId cell = static_cast<CellId>(
                    cellY * static_cast<int>(columns_) + cellX);
                const std::uint32_t begin = offsets_[cell];
                const std::uint32_t end = offsets_[cell + 1];
                for (std::uint32_t cursor = begin; cursor < end; ++cursor) {
                    function(indices_[cursor]);
                }
            }
        }
    }

private:
    float cellSize_{};
    std::uint32_t columns_{};
    std::uint32_t rows_{};
    std::vector<std::uint32_t> counts_;
    std::vector<std::uint32_t> offsets_;
    std::vector<std::uint32_t> writeOffsets_;
    std::vector<std::uint32_t> indices_;
};

} // namespace rushgrid

