#pragma once

#include "rushgrid/core/types.hpp"
#include "rushgrid/jobs/thread_pool.hpp"
#include "rushgrid/navigation/navigation.hpp"
#include "rushgrid/simulation/agent_storage.hpp"
#include "rushgrid/spatial/spatial_grid.hpp"
#include "rushgrid/telemetry/profiler.hpp"
#include "rushgrid/telemetry/statistics.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <vector>

namespace rushgrid {

enum class Implementation {
    baseline,
    optimized
};

[[nodiscard]] std::string_view to_string(Implementation implementation) noexcept;
[[nodiscard]] Implementation parse_implementation(std::string_view value);

struct SimulationConfig {
    std::size_t agentCount{10'000};
    std::size_t workerCount{};
    std::uint32_t seed{1337};
    Implementation implementation{Implementation::optimized};
    float fixedDeltaSeconds{1.0F / 60.0F};
    bool detailedProfiling{true};
};

struct BaselineAgent {
    Vec2 position{};
    Vec2 velocity{};
    Vec2 desired{};
    Vec2 nextWaypointDirection{};
    float radius{0.32F};
    float maxSpeed{2.0F};
    std::uint8_t goal{};
};

struct RenderSnapshot {
    std::vector<float> positionX;
    std::vector<float> positionY;
    std::vector<float> velocityX;
    std::vector<float> velocityY;
    std::vector<std::uint8_t> goal;
    std::uint64_t tick{};

    void reserve(std::size_t capacity);
    [[nodiscard]] std::size_t size() const noexcept { return positionX.size(); }
};

class Simulation {
public:
    explicit Simulation(SimulationConfig config);

    TickMetrics tick();
    void set_gate_open(std::size_t gateIndex, bool open);

    [[nodiscard]] const SimulationConfig& config() const noexcept { return config_; }
    [[nodiscard]] const NavigationGrid& navigation_grid() const noexcept { return navigation_; }
    [[nodiscard]] const AgentStorage& agents() const noexcept { return agents_; }
    [[nodiscard]] const std::vector<BaselineAgent>& baseline_agents() const noexcept {
        return baselineAgents_;
    }
    [[nodiscard]] const std::vector<FlowField>& flow_fields() const noexcept {
        return flowFields_;
    }
    [[nodiscard]] const RenderSnapshot& render_snapshot() const noexcept {
        return renderSnapshots_[publishedSnapshot_.load(std::memory_order_acquire)];
    }
    [[nodiscard]] std::span<const ProfileEvent> profile_events() const noexcept {
        return profiler_.events();
    }
    [[nodiscard]] std::uint64_t profile_tick() const noexcept {
        return profiler_.published_tick();
    }
    [[nodiscard]] std::uint64_t dropped_profile_events() const noexcept {
        return profiler_.dropped_events();
    }
    [[nodiscard]] std::uint64_t tick_index() const noexcept { return tickIndex_; }
    [[nodiscard]] std::uint64_t total_arrivals() const noexcept { return totalArrivals_; }
    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    SimulationConfig config_;
    NavigationGrid navigation_;
    AgentStorage agents_;
    SpatialGrid spatial_;
    ThreadPool jobs_;
    std::vector<FlowField> flowFields_;
    std::vector<CellId> goalCells_;
    std::vector<BaselineAgent> baselineAgents_;
    std::vector<Vec2> baselineNextPositions_;
    std::uint64_t tickIndex_{};
    std::uint64_t totalArrivals_{};
    bool navigationDirty_{true};
    bool gateOpen_[2]{true, true};
    std::array<RenderSnapshot, 2> renderSnapshots_;
    std::atomic<std::uint32_t> publishedSnapshot_{0};
    Profiler profiler_;

    void build_scenario();
    void spawn_agents();
    void publish_render_snapshot();
    void evacuate_closing_gate(std::size_t gateIndex);
    double rebuild_navigation_if_needed();
    TickMetrics tick_optimized();
    TickMetrics tick_baseline();
    void steer_range(std::size_t begin, std::size_t end);
    void integrate_range(std::size_t begin, std::size_t end);
    void steer_baseline();
    void integrate_baseline();
    std::uint32_t process_optimized_arrivals();
    std::uint32_t process_baseline_arrivals();
    [[nodiscard]] Vec2 respawn_position(std::size_t agentIndex) const noexcept;
    [[nodiscard]] bool can_occupy(Vec2 position) const noexcept;
};

} // namespace rushgrid
