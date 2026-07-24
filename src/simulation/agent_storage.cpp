#include "rushgrid/simulation/agent_storage.hpp"

#include <algorithm>
#include <utility>

namespace rushgrid {
namespace {

template <typename T>
[[nodiscard]] std::size_t vector_memory(const std::vector<T>& values) noexcept {
    return values.capacity() * sizeof(T);
}

} // namespace

AgentStorage::AgentStorage(const std::size_t capacity) {
    reserve(capacity);
}

void AgentStorage::reserve(const std::size_t capacity) {
    positionX.reserve(capacity);
    positionY.reserve(capacity);
    nextPositionX.reserve(capacity);
    nextPositionY.reserve(capacity);
    velocityX.reserve(capacity);
    velocityY.reserve(capacity);
    desiredX.reserve(capacity);
    desiredY.reserve(capacity);
    radius.reserve(capacity);
    maxSpeed.reserve(capacity);
    cell.reserve(capacity);
    goal.reserve(capacity);
    state.reserve(capacity);
    ids.reserve(capacity);
    sparseToDense_.reserve(capacity);
    generations_.reserve(capacity);
    freeSparse_.reserve(capacity);
}

EntityId AgentStorage::create(
    const Vec2 position,
    const float agentRadius,
    const float agentMaxSpeed,
    const std::uint8_t agentGoal) {
    std::uint32_t sparseIndex{};
    if (freeSparse_.empty()) {
        sparseIndex = static_cast<std::uint32_t>(sparseToDense_.size());
        sparseToDense_.push_back(invalidIndex);
        generations_.push_back(0);
    } else {
        sparseIndex = freeSparse_.back();
        freeSparse_.pop_back();
    }

    const EntityId id{sparseIndex, generations_[sparseIndex]};
    const auto denseIndex = static_cast<std::uint32_t>(ids.size());
    sparseToDense_[sparseIndex] = denseIndex;

    positionX.push_back(position.x);
    positionY.push_back(position.y);
    nextPositionX.push_back(position.x);
    nextPositionY.push_back(position.y);
    velocityX.push_back(0.0F);
    velocityY.push_back(0.0F);
    desiredX.push_back(0.0F);
    desiredY.push_back(0.0F);
    radius.push_back(agentRadius);
    maxSpeed.push_back(agentMaxSpeed);
    cell.push_back(invalidCell);
    goal.push_back(agentGoal);
    state.push_back(0);
    ids.push_back(id);
    return id;
}

bool AgentStorage::contains(const EntityId id) const noexcept {
    if (id.index >= sparseToDense_.size() ||
        id.generation != generations_[id.index]) {
        return false;
    }
    const std::uint32_t dense = sparseToDense_[id.index];
    return dense != invalidIndex && dense < ids.size() && ids[dense] == id;
}

std::uint32_t AgentStorage::dense_index(const EntityId id) const noexcept {
    return contains(id) ? sparseToDense_[id.index] : invalidIndex;
}

bool AgentStorage::remove(const EntityId id) {
    if (!contains(id)) {
        return false;
    }

    const std::size_t dense = sparseToDense_[id.index];
    const std::size_t last = ids.size() - 1;
    if (dense != last) {
        swap_dense(dense, last);
        sparseToDense_[ids[dense].index] = static_cast<std::uint32_t>(dense);
    }
    pop_dense();

    sparseToDense_[id.index] = invalidIndex;
    ++generations_[id.index];
    freeSparse_.push_back(id.index);
    return true;
}

void AgentStorage::swap_dense(const std::size_t left, const std::size_t right) {
    using std::swap;
    swap(positionX[left], positionX[right]);
    swap(positionY[left], positionY[right]);
    swap(nextPositionX[left], nextPositionX[right]);
    swap(nextPositionY[left], nextPositionY[right]);
    swap(velocityX[left], velocityX[right]);
    swap(velocityY[left], velocityY[right]);
    swap(desiredX[left], desiredX[right]);
    swap(desiredY[left], desiredY[right]);
    swap(radius[left], radius[right]);
    swap(maxSpeed[left], maxSpeed[right]);
    swap(cell[left], cell[right]);
    swap(goal[left], goal[right]);
    swap(state[left], state[right]);
    swap(ids[left], ids[right]);
}

void AgentStorage::pop_dense() {
    positionX.pop_back();
    positionY.pop_back();
    nextPositionX.pop_back();
    nextPositionY.pop_back();
    velocityX.pop_back();
    velocityY.pop_back();
    desiredX.pop_back();
    desiredY.pop_back();
    radius.pop_back();
    maxSpeed.pop_back();
    cell.pop_back();
    goal.pop_back();
    state.pop_back();
    ids.pop_back();
}

std::size_t AgentStorage::memory_bytes() const noexcept {
    return vector_memory(positionX) + vector_memory(positionY) +
        vector_memory(nextPositionX) + vector_memory(nextPositionY) +
        vector_memory(velocityX) + vector_memory(velocityY) +
        vector_memory(desiredX) + vector_memory(desiredY) +
        vector_memory(radius) + vector_memory(maxSpeed) + vector_memory(cell) +
        vector_memory(goal) + vector_memory(state) + vector_memory(ids) +
        vector_memory(sparseToDense_) + vector_memory(generations_) +
        vector_memory(freeSparse_);
}

} // namespace rushgrid

