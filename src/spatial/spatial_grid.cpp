#include "rushgrid/spatial/spatial_grid.hpp"

#include "rushgrid/simulation/agent_storage.hpp"

#include <numeric>

namespace rushgrid {

SpatialGrid::SpatialGrid(
    const float worldWidth,
    const float worldHeight,
    const float cellSize,
    const std::size_t capacity)
    : cellSize_(cellSize),
      columns_(static_cast<std::uint32_t>(std::ceil(worldWidth / cellSize))),
      rows_(static_cast<std::uint32_t>(std::ceil(worldHeight / cellSize))),
      counts_(columns_ * rows_),
      offsets_(columns_ * rows_ + 1),
      writeOffsets_(columns_ * rows_),
      indices_(capacity) {
}

CellId SpatialGrid::cell_at(const float x, const float y) const noexcept {
    const int cellX = std::clamp(
        static_cast<int>(x / cellSize_),
        0,
        static_cast<int>(columns_) - 1);
    const int cellY = std::clamp(
        static_cast<int>(y / cellSize_),
        0,
        static_cast<int>(rows_) - 1);
    return static_cast<CellId>(cellY * static_cast<int>(columns_) + cellX);
}

void SpatialGrid::build(AgentStorage& agents) {
    std::fill(counts_.begin(), counts_.end(), 0U);
    if (indices_.size() < agents.size()) {
        indices_.resize(agents.size());
    }

    for (std::size_t index = 0; index < agents.size(); ++index) {
        const CellId cell = cell_at(agents.positionX[index], agents.positionY[index]);
        agents.cell[index] = cell;
        ++counts_[cell];
    }

    offsets_[0] = 0;
    std::partial_sum(counts_.begin(), counts_.end(), offsets_.begin() + 1);
    std::copy(offsets_.begin(), offsets_.end() - 1, writeOffsets_.begin());

    for (std::uint32_t index = 0; index < agents.size(); ++index) {
        const CellId cell = agents.cell[index];
        indices_[writeOffsets_[cell]++] = index;
    }
}

std::uint32_t SpatialGrid::occupancy(const CellId cell) const noexcept {
    return cell < counts_.size() ? counts_[cell] : 0;
}

std::uint32_t SpatialGrid::maximum_occupancy() const noexcept {
    return counts_.empty() ? 0U : *std::max_element(counts_.begin(), counts_.end());
}

std::size_t SpatialGrid::memory_bytes() const noexcept {
    return counts_.capacity() * sizeof(std::uint32_t) +
        offsets_.capacity() * sizeof(std::uint32_t) +
        writeOffsets_.capacity() * sizeof(std::uint32_t) +
        indices_.capacity() * sizeof(std::uint32_t);
}

} // namespace rushgrid

