#include "rushgrid/navigation/navigation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace rushgrid {
namespace {

constexpr float infinity = std::numeric_limits<float>::infinity();

struct Neighbor {
    int x{};
    int y{};
    float cost{};
};

constexpr std::array<Neighbor, 8> neighbors{{
    {-1, 0, 1.0F},
    {1, 0, 1.0F},
    {0, -1, 1.0F},
    {0, 1, 1.0F},
    {-1, -1, 1.41421356F},
    {1, -1, 1.41421356F},
    {-1, 1, 1.41421356F},
    {1, 1, 1.41421356F},
}};

[[nodiscard]] bool diagonal_allowed(
    const NavigationGrid& grid,
    const int x,
    const int y,
    const Neighbor neighbor) noexcept {
    if (neighbor.x == 0 || neighbor.y == 0) {
        return true;
    }
    return grid.is_walkable(x + neighbor.x, y) &&
        grid.is_walkable(x, y + neighbor.y);
}

[[nodiscard]] float heuristic(
    const int fromX,
    const int fromY,
    const int toX,
    const int toY) noexcept {
    const float dx = static_cast<float>(fromX - toX);
    const float dy = static_cast<float>(fromY - toY);
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

NavigationGrid::NavigationGrid(
    const std::uint32_t width,
    const std::uint32_t height,
    const float cellSize)
    : width_(width),
      height_(height),
      cellSize_(cellSize),
      walkable_(static_cast<std::size_t>(width) * height, 1) {
}

bool NavigationGrid::in_bounds(const int x, const int y) const noexcept {
    return x >= 0 && y >= 0 &&
        x < static_cast<int>(width_) &&
        y < static_cast<int>(height_);
}

bool NavigationGrid::is_walkable(const int x, const int y) const noexcept {
    return in_bounds(x, y) && walkable_[cell_at(x, y)] != 0;
}

bool NavigationGrid::is_walkable(const CellId cell) const noexcept {
    return cell < walkable_.size() && walkable_[cell] != 0;
}

void NavigationGrid::set_walkable(const int x, const int y, const bool walkable) {
    if (!in_bounds(x, y)) {
        return;
    }
    const CellId cell = cell_at(x, y);
    const std::uint8_t next = walkable ? 1U : 0U;
    if (walkable_[cell] != next) {
        walkable_[cell] = next;
        ++revision_;
    }
}

CellId NavigationGrid::cell_at(const Vec2 position) const noexcept {
    const int x = static_cast<int>(std::floor(position.x / cellSize_));
    const int y = static_cast<int>(std::floor(position.y / cellSize_));
    return cell_at(x, y);
}

CellId NavigationGrid::cell_at(const int x, const int y) const noexcept {
    if (!in_bounds(x, y)) {
        return invalidCell;
    }
    return static_cast<CellId>(y * static_cast<int>(width_) + x);
}

Vec2 NavigationGrid::cell_center(const CellId cell) const noexcept {
    if (cell >= walkable_.size()) {
        return {};
    }
    const std::uint32_t x = cell % width_;
    const std::uint32_t y = cell / width_;
    return {
        (static_cast<float>(x) + 0.5F) * cellSize_,
        (static_cast<float>(y) + 0.5F) * cellSize_};
}

Vec2 NavigationGrid::clamp_position(const Vec2 position) const noexcept {
    constexpr float margin = 0.001F;
    return {
        std::clamp(position.x, margin, width_ * cellSize_ - margin),
        std::clamp(position.y, margin, height_ * cellSize_ - margin)};
}

void FlowField::rebuild(const NavigationGrid& grid, const CellId goal) {
    costs_.assign(grid.cell_count(), infinity);
    directions_.assign(grid.cell_count(), Vec2{});
    goal_ = invalidCell;

    if (!grid.is_walkable(goal)) {
        sourceRevision_ = grid.revision();
        ++version_;
        return;
    }

    using OpenNode = std::pair<float, CellId>;
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<>> open;
    costs_[goal] = 0.0F;
    open.emplace(0.0F, goal);

    while (!open.empty()) {
        const auto [currentCost, current] = open.top();
        open.pop();
        if (currentCost > costs_[current]) {
            continue;
        }

        const int currentX = static_cast<int>(current % grid.width());
        const int currentY = static_cast<int>(current / grid.width());
        for (const Neighbor neighbor : neighbors) {
            const int nextX = currentX + neighbor.x;
            const int nextY = currentY + neighbor.y;
            if (!grid.is_walkable(nextX, nextY) ||
                !diagonal_allowed(grid, currentX, currentY, neighbor)) {
                continue;
            }
            const CellId next = grid.cell_at(nextX, nextY);
            const float candidate = currentCost + neighbor.cost;
            if (candidate >= costs_[next]) {
                continue;
            }
            costs_[next] = candidate;
            open.emplace(candidate, next);
        }
    }

    for (std::uint32_t cell = 0; cell < grid.cell_count(); ++cell) {
        if (!grid.is_walkable(cell) || !std::isfinite(costs_[cell]) || cell == goal) {
            continue;
        }
        const int cellX = static_cast<int>(cell % grid.width());
        const int cellY = static_cast<int>(cell / grid.width());
        CellId best = cell;
        float bestCost = costs_[cell];
        for (const Neighbor neighbor : neighbors) {
            const int nextX = cellX + neighbor.x;
            const int nextY = cellY + neighbor.y;
            if (!grid.is_walkable(nextX, nextY) ||
                !diagonal_allowed(grid, cellX, cellY, neighbor)) {
                continue;
            }
            const CellId next = grid.cell_at(nextX, nextY);
            if (costs_[next] < bestCost) {
                bestCost = costs_[next];
                best = next;
            }
        }
        if (best != cell) {
            directions_[cell] = normalized_or_zero(
                grid.cell_center(best) - grid.cell_center(cell));
        }
    }

    goal_ = goal;
    sourceRevision_ = grid.revision();
    ++version_;
}

Vec2 FlowField::sample(
    const Vec2 position,
    const NavigationGrid& grid) const noexcept {
    const CellId cell = grid.cell_at(position);
    return cell < directions_.size() ? directions_[cell] : Vec2{};
}

float FlowField::integrated_cost(const CellId cell) const noexcept {
    return cell < costs_.size() ? costs_[cell] : infinity;
}

Vec2 astar_next_direction(
    const NavigationGrid& grid,
    const CellId start,
    const CellId goal) {
    if (!grid.is_walkable(start) || !grid.is_walkable(goal) || start == goal) {
        return {};
    }

    const std::size_t count = grid.cell_count();
    std::vector<float> gScore(count, infinity);
    std::vector<CellId> parent(count, invalidCell);
    using OpenNode = std::pair<float, CellId>;
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<>> open;

    const int goalX = static_cast<int>(goal % grid.width());
    const int goalY = static_cast<int>(goal / grid.width());
    gScore[start] = 0.0F;
    open.emplace(0.0F, start);

    bool found = false;
    while (!open.empty()) {
        const CellId current = open.top().second;
        open.pop();
        if (current == goal) {
            found = true;
            break;
        }

        const int currentX = static_cast<int>(current % grid.width());
        const int currentY = static_cast<int>(current / grid.width());
        for (const Neighbor neighbor : neighbors) {
            const int nextX = currentX + neighbor.x;
            const int nextY = currentY + neighbor.y;
            if (!grid.is_walkable(nextX, nextY) ||
                !diagonal_allowed(grid, currentX, currentY, neighbor)) {
                continue;
            }
            const CellId next = grid.cell_at(nextX, nextY);
            const float candidate = gScore[current] + neighbor.cost;
            if (candidate >= gScore[next]) {
                continue;
            }
            parent[next] = current;
            gScore[next] = candidate;
            open.emplace(
                candidate + heuristic(nextX, nextY, goalX, goalY),
                next);
        }
    }

    if (!found) {
        return {};
    }

    CellId step = goal;
    while (parent[step] != invalidCell && parent[step] != start) {
        step = parent[step];
    }
    if (parent[step] == invalidCell) {
        return {};
    }
    return normalized_or_zero(grid.cell_center(step) - grid.cell_center(start));
}

} // namespace rushgrid
