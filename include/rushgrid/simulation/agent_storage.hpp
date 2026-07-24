#pragma once

#include "rushgrid/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rushgrid {

class AgentStorage {
public:
    explicit AgentStorage(std::size_t capacity = 0);

    void reserve(std::size_t capacity);

    [[nodiscard]] EntityId create(
        Vec2 position,
        float radius,
        float maxSpeed,
        std::uint8_t goal);

    [[nodiscard]] bool remove(EntityId id);
    [[nodiscard]] bool contains(EntityId id) const noexcept;
    [[nodiscard]] std::uint32_t dense_index(EntityId id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return ids.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return ids.capacity(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

    std::vector<float> positionX;
    std::vector<float> positionY;
    std::vector<float> nextPositionX;
    std::vector<float> nextPositionY;
    std::vector<float> velocityX;
    std::vector<float> velocityY;
    std::vector<float> desiredX;
    std::vector<float> desiredY;
    std::vector<float> radius;
    std::vector<float> maxSpeed;
    std::vector<CellId> cell;
    std::vector<std::uint8_t> goal;
    std::vector<std::uint8_t> state;
    std::vector<EntityId> ids;

private:
    std::vector<std::uint32_t> sparseToDense_;
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint32_t> freeSparse_;

    void swap_dense(std::size_t left, std::size_t right);
    void pop_dense();
};

} // namespace rushgrid

