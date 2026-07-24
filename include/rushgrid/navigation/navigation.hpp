#pragma once

#include "rushgrid/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rushgrid {

class NavigationGrid {
public:
    NavigationGrid(std::uint32_t width, std::uint32_t height, float cellSize);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] float cell_size() const noexcept { return cellSize_; }
    [[nodiscard]] std::size_t cell_count() const noexcept { return walkable_.size(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] bool in_bounds(int x, int y) const noexcept;
    [[nodiscard]] bool is_walkable(int x, int y) const noexcept;
    [[nodiscard]] bool is_walkable(CellId cell) const noexcept;
    void set_walkable(int x, int y, bool walkable);

    [[nodiscard]] CellId cell_at(Vec2 position) const noexcept;
    [[nodiscard]] CellId cell_at(int x, int y) const noexcept;
    [[nodiscard]] Vec2 cell_center(CellId cell) const noexcept;
    [[nodiscard]] Vec2 clamp_position(Vec2 position) const noexcept;

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    float cellSize_{1.0F};
    std::vector<std::uint8_t> walkable_;
    std::uint64_t revision_{1};
};

class FlowField {
public:
    void rebuild(const NavigationGrid& grid, CellId goal);

    [[nodiscard]] Vec2 sample(Vec2 position, const NavigationGrid& grid) const noexcept;
    [[nodiscard]] float integrated_cost(CellId cell) const noexcept;
    [[nodiscard]] CellId goal() const noexcept { return goal_; }
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    [[nodiscard]] std::uint64_t source_revision() const noexcept { return sourceRevision_; }
    [[nodiscard]] bool valid() const noexcept { return goal_ != invalidCell; }

private:
    std::vector<float> costs_;
    std::vector<Vec2> directions_;
    CellId goal_{invalidCell};
    std::uint64_t version_{};
    std::uint64_t sourceRevision_{};
};

[[nodiscard]] Vec2 astar_next_direction(
    const NavigationGrid& grid,
    CellId start,
    CellId goal);

} // namespace rushgrid

