#include "rushgrid/jobs/thread_pool.hpp"
#include "rushgrid/navigation/navigation.hpp"
#include "rushgrid/simulation/agent_storage.hpp"
#include "rushgrid/simulation/simulation.hpp"
#include "rushgrid/spatial/spatial_grid.hpp"
#include "rushgrid/telemetry/statistics.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_generational_handles() {
    rushgrid::AgentStorage agents(4);
    const rushgrid::EntityId first = agents.create({1.0F, 2.0F}, 0.3F, 2.0F, 0);
    const rushgrid::EntityId second = agents.create({3.0F, 4.0F}, 0.3F, 2.0F, 1);
    expect(agents.contains(first), "new handle should resolve");
    expect(agents.remove(first), "existing handle should remove");
    expect(!agents.contains(first), "removed handle must become stale");
    expect(agents.dense_index(second) == 0, "swap-remove must repair sparse mapping");

    const rushgrid::EntityId replacement =
        agents.create({5.0F, 6.0F}, 0.3F, 2.0F, 0);
    expect(replacement.index == first.index, "free sparse slot should be reused");
    expect(replacement.generation != first.generation, "generation must advance");
}

void test_spatial_grid() {
    rushgrid::AgentStorage agents(4);
    static_cast<void>(agents.create({1.0F, 1.0F}, 0.3F, 2.0F, 0));
    static_cast<void>(agents.create({1.5F, 1.5F}, 0.3F, 2.0F, 0));
    static_cast<void>(agents.create({8.0F, 8.0F}, 0.3F, 2.0F, 0));
    rushgrid::SpatialGrid grid(10.0F, 10.0F, 2.0F, 4);
    grid.build(agents);
    expect(grid.maximum_occupancy() == 2, "maximum occupancy should be counted");

    std::size_t nearby{};
    grid.for_each_nearby(1.0F, 1.0F, 1.0F, [&nearby](std::uint32_t) {
        ++nearby;
    });
    expect(nearby == 2, "adjacent query should visit local agents only");
}

void test_flow_field_and_unreachable_cells() {
    rushgrid::NavigationGrid grid(8, 8, 1.0F);
    for (int y = 0; y < 8; ++y) {
        grid.set_walkable(3, y, false);
    }
    grid.set_walkable(3, 4, true);

    rushgrid::FlowField field;
    field.rebuild(grid, grid.cell_at(7, 4));
    const rushgrid::Vec2 direction = field.sample({1.5F, 1.5F}, grid);
    expect(rushgrid::length_squared(direction) > 0.5F, "reachable cell needs direction");
    expect(std::isfinite(field.integrated_cost(grid.cell_at(1, 1))),
           "gap should keep left region reachable");

    grid.set_walkable(3, 4, false);
    field.rebuild(grid, grid.cell_at(7, 4));
    expect(!std::isfinite(field.integrated_cost(grid.cell_at(1, 1))),
           "sealed region should be unreachable");
}

void test_job_system_parallel_coverage() {
    rushgrid::ThreadPool jobs(3);
    std::vector<std::atomic<int>> visits(1000);
    jobs.parallel_for(
        visits.size(),
        31,
        [&visits](const std::size_t begin, const std::size_t end) {
            for (std::size_t index = begin; index < end; ++index) {
                visits[index].fetch_add(1, std::memory_order_relaxed);
            }
        });
    for (const auto& visit : visits) {
        expect(visit.load(std::memory_order_relaxed) == 1,
               "parallel_for must cover each index exactly once");
    }
    expect(jobs.stats().executed > 0, "job instrumentation should record work");
}

void test_statistics() {
    rushgrid::SampleWindow samples(5);
    for (int value = 1; value <= 5; ++value) {
        samples.push(static_cast<double>(value));
    }
    expect(samples.mean() == 3.0, "sample mean should match");
    expect(samples.percentile(0.5) == 3.0, "median should match");
    samples.push(10.0);
    expect(samples.minimum() == 2.0, "ring buffer should evict oldest sample");
    expect(samples.maximum() == 10.0, "ring buffer should contain latest sample");
}

void test_thread_count_determinism() {
    rushgrid::Simulation single({
        256,
        0,
        42,
        rushgrid::Implementation::optimized});
    rushgrid::Simulation parallel({
        256,
        3,
        42,
        rushgrid::Implementation::optimized});

    for (int tick = 0; tick < 90; ++tick) {
        if (tick == 45) {
            single.set_gate_open(0, false);
            parallel.set_gate_open(0, false);
        }
        static_cast<void>(single.tick());
        static_cast<void>(parallel.tick());
    }
    expect(single.state_hash() == parallel.state_hash(),
           "optimized state should be stable across worker counts");
    expect(parallel.render_snapshot().tick == parallel.tick_index(),
           "published render snapshot should be current");
    expect(parallel.render_snapshot().size() == parallel.config().agentCount,
           "render snapshot should contain every agent");
}

void test_gate_closure_keeps_agents_walkable() {
    rushgrid::Simulation simulation({
        512,
        2,
        7,
        rushgrid::Implementation::optimized});
    simulation.set_gate_open(0, false);
    for (int tick = 0; tick < 120; ++tick) {
        static_cast<void>(simulation.tick());
    }
    const rushgrid::NavigationGrid& navigation = simulation.navigation_grid();
    const rushgrid::RenderSnapshot& snapshot = simulation.render_snapshot();
    for (std::size_t index = 0; index < snapshot.size(); ++index) {
        expect(
            navigation.is_walkable(
                navigation.cell_at({snapshot.positionX[index], snapshot.positionY[index]})),
            "agents must not occupy blocked cells after a gate command");
    }
}

void test_profiler_publishes_worker_zones() {
    rushgrid::Simulation simulation({
        2048,
        3,
        11,
        rushgrid::Implementation::optimized});
    static_cast<void>(simulation.tick());
    const std::span<const rushgrid::ProfileEvent> events =
        simulation.profile_events();
    expect(!events.empty(), "profile frame should contain events");
    expect(simulation.profile_tick() == simulation.tick_index(),
           "profile frame should identify its source tick");

    bool foundSteering{};
    bool foundIntegration{};
    bool foundWorkerJob{};
    for (const rushgrid::ProfileEvent& event : events) {
        const std::string name = event.name;
        foundSteering = foundSteering || name == "Steering";
        foundIntegration = foundIntegration || name == "Integration";
        foundWorkerJob = foundWorkerJob || name == "Steering job";
    }
    expect(foundSteering, "profiler should record steering phase");
    expect(foundIntegration, "profiler should record integration phase");
    expect(foundWorkerJob, "profiler should record worker jobs");
    expect(simulation.dropped_profile_events() == 0,
           "normal tick should fit in the preallocated event buffer");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"generational handles", test_generational_handles},
        {"spatial grid", test_spatial_grid},
        {"flow field and unreachable cells", test_flow_field_and_unreachable_cells},
        {"job system parallel coverage", test_job_system_parallel_coverage},
        {"ring statistics", test_statistics},
        {"thread-count determinism", test_thread_count_determinism},
        {"safe gate closure", test_gate_closure_keeps_agents_walkable},
        {"scoped profiler", test_profiler_publishes_worker_zones},
    };

    std::size_t failures{};
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[pass] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[fail] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
