#include "rushgrid/simulation/simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace rushgrid {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsed_ms(const Clock::time_point start) noexcept {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

[[nodiscard]] std::uint64_t mix_bits(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] float unit_from_hash(const std::uint64_t value) noexcept {
    return static_cast<float>(value & 0x00ffffffULL) /
        static_cast<float>(0x01000000ULL);
}

[[nodiscard]] std::int64_t quantize(const float value) noexcept {
    return static_cast<std::int64_t>(std::llround(value * 1000.0F));
}

} // namespace

std::string_view to_string(const Implementation implementation) noexcept {
    return implementation == Implementation::baseline ? "baseline" : "optimized";
}

Implementation parse_implementation(const std::string_view value) {
    if (value == "baseline") {
        return Implementation::baseline;
    }
    if (value == "optimized") {
        return Implementation::optimized;
    }
    throw std::invalid_argument(
        "implementation must be 'baseline' or 'optimized', got '" +
        std::string(value) + "'");
}

void RenderSnapshot::reserve(const std::size_t capacity) {
    positionX.reserve(capacity);
    positionY.reserve(capacity);
    velocityX.reserve(capacity);
    velocityY.reserve(capacity);
    goal.reserve(capacity);
}

Simulation::Simulation(SimulationConfig config)
    : config_(config),
      navigation_(96, 54, 1.0F),
      agents_(config.agentCount),
      spatial_(96.0F, 54.0F, 2.0F, config.agentCount),
      jobs_(config.workerCount),
      flowFields_(2) {
    profiler_.set_enabled(config_.detailedProfiling);
    build_scenario();
    spawn_agents();
    for (RenderSnapshot& snapshot : renderSnapshots_) {
        snapshot.reserve(config_.agentCount);
    }
    publish_render_snapshot();
}

void Simulation::build_scenario() {
    for (int x = 0; x < static_cast<int>(navigation_.width()); ++x) {
        navigation_.set_walkable(x, 0, false);
        navigation_.set_walkable(x, static_cast<int>(navigation_.height()) - 1, false);
    }
    for (int y = 0; y < static_cast<int>(navigation_.height()); ++y) {
        navigation_.set_walkable(0, y, false);
        navigation_.set_walkable(static_cast<int>(navigation_.width()) - 1, y, false);
    }

    // Central service block splits the approach and makes route choice visible.
    for (int y = 16; y <= 37; ++y) {
        for (int x = 42; x <= 53; ++x) {
            navigation_.set_walkable(x, y, false);
        }
    }

    // Fare line with two controllable six-cell gates.
    for (int y = 1; y < 53; ++y) {
        navigation_.set_walkable(68, y, false);
    }
    for (int y = 9; y <= 14; ++y) {
        navigation_.set_walkable(68, y, true);
    }
    for (int y = 39; y <= 44; ++y) {
        navigation_.set_walkable(68, y, true);
    }

    // Platform furniture creates smaller local avoidance decisions.
    for (int x = 75; x <= 84; ++x) {
        navigation_.set_walkable(x, 25, false);
        navigation_.set_walkable(x, 29, false);
    }

    goalCells_ = {
        navigation_.cell_at(93, 11),
        navigation_.cell_at(93, 42)};
    navigationDirty_ = true;
}

void Simulation::spawn_agents() {
    std::mt19937 random(config_.seed);
    std::uniform_real_distribution<float> xDistribution(2.0F, 39.5F);
    std::uniform_real_distribution<float> yDistribution(2.0F, 51.5F);
    std::uniform_real_distribution<float> radiusDistribution(0.22F, 0.32F);
    std::uniform_real_distribution<float> speedDistribution(1.55F, 2.35F);
    std::uniform_int_distribution<int> goalDistribution(0, 1);

    if (config_.implementation == Implementation::baseline) {
        baselineAgents_.reserve(config_.agentCount);
        baselineNextPositions_.resize(config_.agentCount);
    }

    for (std::size_t index = 0; index < config_.agentCount; ++index) {
        Vec2 position{};
        do {
            position = {xDistribution(random), yDistribution(random)};
        } while (!can_occupy(position));

        const float radius = radiusDistribution(random);
        const float speed = speedDistribution(random);
        const auto goal = static_cast<std::uint8_t>(goalDistribution(random));
        if (config_.implementation == Implementation::optimized) {
            static_cast<void>(agents_.create(position, radius, speed, goal));
        } else {
            baselineAgents_.push_back({
                position,
                {},
                {},
                {},
                radius,
                speed,
                goal});
        }
    }
}

double Simulation::rebuild_navigation_if_needed() {
    if (!navigationDirty_) {
        return 0.0;
    }
    const auto start = Clock::now();
    for (std::size_t index = 0; index < flowFields_.size(); ++index) {
        flowFields_[index].rebuild(navigation_, goalCells_[index]);
    }
    navigationDirty_ = false;
    return elapsed_ms(start);
}

TickMetrics Simulation::tick() {
    const auto totalStart = Clock::now();
    profiler_.begin_frame();
    jobs_.reset_stats();
    double navigationMs{};
    {
        auto profile = profiler_.scope("Navigation");
        navigationMs = rebuild_navigation_if_needed();
    }

    TickMetrics metrics = config_.implementation == Implementation::optimized
        ? tick_optimized()
        : tick_baseline();
    metrics.navigationMs = navigationMs;
    const JobSystemStats jobStats = jobs_.stats();
    metrics.jobsExecuted = jobStats.executed;
    metrics.jobsStolen = jobStats.stolen;
    metrics.maximumJobQueueDepth = jobStats.maximumQueueDepth;
    ++tickIndex_;
    {
        auto profile = profiler_.scope("Snapshot");
        publish_render_snapshot();
    }
    profiler_.end_frame(tickIndex_);
    metrics.totalMs = elapsed_ms(totalStart);
    return metrics;
}

TickMetrics Simulation::tick_optimized() {
    TickMetrics metrics{};

    auto sectionStart = Clock::now();
    {
        auto profile = profiler_.scope("Spatial grid");
        spatial_.build(agents_);
    }
    metrics.gridMs = elapsed_ms(sectionStart);
    metrics.maximumCellOccupancy = spatial_.maximum_occupancy();

    sectionStart = Clock::now();
    {
        auto profile = profiler_.scope("Steering");
        jobs_.parallel_for(
            agents_.size(),
            256,
            [this](const std::size_t begin, const std::size_t end) {
                auto jobProfile = profiler_.scope("Steering job");
                steer_range(begin, end);
            });
    }
    metrics.steeringMs = elapsed_ms(sectionStart);

    sectionStart = Clock::now();
    {
        auto profile = profiler_.scope("Integration");
        jobs_.parallel_for(
            agents_.size(),
            512,
            [this](const std::size_t begin, const std::size_t end) {
                auto jobProfile = profiler_.scope("Integration job");
                integrate_range(begin, end);
            });
    }
    agents_.positionX.swap(agents_.nextPositionX);
    agents_.positionY.swap(agents_.nextPositionY);
    {
        auto profile = profiler_.scope("Arrivals");
        metrics.arrivals = process_optimized_arrivals();
    }
    metrics.integrationMs = elapsed_ms(sectionStart);
    return metrics;
}

void Simulation::steer_range(
    const std::size_t begin,
    const std::size_t end) {
    for (std::size_t index = begin; index < end; ++index) {
        const Vec2 position{agents_.positionX[index], agents_.positionY[index]};
        const Vec2 navigationDirection =
            flowFields_[agents_.goal[index]].sample(position, navigation_);

        Vec2 separation{};
        std::size_t considered = 0;
        spatial_.for_each_nearby(
            position.x,
            position.y,
            1.25F,
            [this, index, position, &separation, &considered](
                const std::uint32_t otherIndex) {
                if (otherIndex == index || considered >= 48) {
                    return;
                }
                ++considered;
                const Vec2 delta{
                    position.x - agents_.positionX[otherIndex],
                    position.y - agents_.positionY[otherIndex]};
                const float distanceSquared = length_squared(delta);
                const float personalSpace =
                    agents_.radius[index] + agents_.radius[otherIndex] + 0.18F;
                if (distanceSquared >= personalSpace * personalSpace) {
                    return;
                }
                if (distanceSquared <= 0.000001F) {
                    const float sign = ((index ^ otherIndex) & 1U) == 0U
                        ? 1.0F
                        : -1.0F;
                    separation += {sign, -sign};
                    return;
                }
                const float distance = std::sqrt(distanceSquared);
                separation +=
                    (delta * (1.0F / distance)) *
                    ((personalSpace - distance) / personalSpace);
            });

        const std::uint32_t occupancy = spatial_.occupancy(agents_.cell[index]);
        const float densityScale = 1.0F /
            (1.0F + 0.035F * static_cast<float>(
                occupancy > 6 ? occupancy - 6 : 0));
        Vec2 direction = normalized_or_zero(
            navigationDirection + separation * 1.65F);

        const Vec2 lookAhead = position + direction * 0.8F;
        if (!can_occupy(lookAhead)) {
            const Vec2 left{-navigationDirection.y, navigationDirection.x};
            const Vec2 right{navigationDirection.y, -navigationDirection.x};
            if (can_occupy(position + left * 0.8F)) {
                direction = normalized_or_zero(left + separation);
            } else if (can_occupy(position + right * 0.8F)) {
                direction = normalized_or_zero(right + separation);
            } else {
                direction = {};
            }
        }

        const Vec2 desired =
            direction * (agents_.maxSpeed[index] * densityScale);
        agents_.desiredX[index] = desired.x;
        agents_.desiredY[index] = desired.y;
    }
}

void Simulation::integrate_range(
    const std::size_t begin,
    const std::size_t end) {
    const float delta = config_.fixedDeltaSeconds;
    const float maximumVelocityChange = 5.5F * delta;
    for (std::size_t index = begin; index < end; ++index) {
        Vec2 velocity{agents_.velocityX[index], agents_.velocityY[index]};
        const Vec2 desired{agents_.desiredX[index], agents_.desiredY[index]};
        velocity += clamp_length(desired - velocity, maximumVelocityChange);
        velocity = clamp_length(velocity, agents_.maxSpeed[index]);

        const Vec2 current{agents_.positionX[index], agents_.positionY[index]};
        Vec2 next = navigation_.clamp_position(current + velocity * delta);
        if (!can_occupy(next)) {
            const Vec2 xOnly{next.x, current.y};
            const Vec2 yOnly{current.x, next.y};
            if (can_occupy(xOnly)) {
                next = xOnly;
                velocity.y = 0.0F;
            } else if (can_occupy(yOnly)) {
                next = yOnly;
                velocity.x = 0.0F;
            } else {
                next = current;
                velocity = {};
            }
        }

        agents_.nextPositionX[index] = next.x;
        agents_.nextPositionY[index] = next.y;
        agents_.velocityX[index] = velocity.x;
        agents_.velocityY[index] = velocity.y;
    }
}

TickMetrics Simulation::tick_baseline() {
    TickMetrics metrics{};
    const auto steeringStart = Clock::now();
    {
        auto profile = profiler_.scope("Baseline steering");
        steer_baseline();
    }
    metrics.steeringMs = elapsed_ms(steeringStart);

    const auto integrationStart = Clock::now();
    {
        auto profile = profiler_.scope("Baseline integration");
        integrate_baseline();
        metrics.arrivals = process_baseline_arrivals();
    }
    metrics.integrationMs = elapsed_ms(integrationStart);
    return metrics;
}

void Simulation::steer_baseline() {
    for (std::size_t index = 0; index < baselineAgents_.size(); ++index) {
        BaselineAgent& agent = baselineAgents_[index];
        if (((tickIndex_ + index) % 30U) == 0U ||
            length_squared(agent.nextWaypointDirection) < 0.01F) {
            agent.nextWaypointDirection = astar_next_direction(
                navigation_,
                navigation_.cell_at(agent.position),
                goalCells_[agent.goal]);
        }

        Vec2 separation{};
        for (std::size_t otherIndex = 0;
             otherIndex < baselineAgents_.size();
             ++otherIndex) {
            if (otherIndex == index) {
                continue;
            }
            const BaselineAgent& other = baselineAgents_[otherIndex];
            const Vec2 delta = agent.position - other.position;
            const float distanceSquared = length_squared(delta);
            const float personalSpace = agent.radius + other.radius + 0.18F;
            if (distanceSquared <= 0.000001F ||
                distanceSquared >= personalSpace * personalSpace) {
                continue;
            }
            const float distance = std::sqrt(distanceSquared);
            separation +=
                (delta * (1.0F / distance)) *
                ((personalSpace - distance) / personalSpace);
        }

        const Vec2 direction = normalized_or_zero(
            agent.nextWaypointDirection + separation * 1.65F);
        agent.desired = direction * agent.maxSpeed;
    }
}

void Simulation::integrate_baseline() {
    const float delta = config_.fixedDeltaSeconds;
    for (std::size_t index = 0; index < baselineAgents_.size(); ++index) {
        BaselineAgent& agent = baselineAgents_[index];
        agent.velocity += clamp_length(agent.desired - agent.velocity, 5.5F * delta);
        agent.velocity = clamp_length(agent.velocity, agent.maxSpeed);
        Vec2 next = navigation_.clamp_position(agent.position + agent.velocity * delta);
        if (!can_occupy(next)) {
            next = agent.position;
            agent.velocity = {};
            agent.nextWaypointDirection = {};
        }
        baselineNextPositions_[index] = next;
    }
    for (std::size_t index = 0; index < baselineAgents_.size(); ++index) {
        baselineAgents_[index].position = baselineNextPositions_[index];
    }
}

std::uint32_t Simulation::process_optimized_arrivals() {
    std::uint32_t arrivals{};
    for (std::size_t index = 0; index < agents_.size(); ++index) {
        const Vec2 position{agents_.positionX[index], agents_.positionY[index]};
        const Vec2 goal = navigation_.cell_center(goalCells_[agents_.goal[index]]);
        if (length_squared(position - goal) > 1.5F * 1.5F) {
            continue;
        }
        const Vec2 respawn = respawn_position(index);
        agents_.positionX[index] = respawn.x;
        agents_.positionY[index] = respawn.y;
        agents_.nextPositionX[index] = respawn.x;
        agents_.nextPositionY[index] = respawn.y;
        agents_.velocityX[index] = 0.0F;
        agents_.velocityY[index] = 0.0F;
        agents_.goal[index] = static_cast<std::uint8_t>(
            mix_bits(index + tickIndex_ + totalArrivals_) & 1U);
        ++arrivals;
    }
    totalArrivals_ += arrivals;
    return arrivals;
}

std::uint32_t Simulation::process_baseline_arrivals() {
    std::uint32_t arrivals{};
    for (std::size_t index = 0; index < baselineAgents_.size(); ++index) {
        BaselineAgent& agent = baselineAgents_[index];
        const Vec2 goal = navigation_.cell_center(goalCells_[agent.goal]);
        if (length_squared(agent.position - goal) > 1.5F * 1.5F) {
            continue;
        }
        agent.position = respawn_position(index);
        agent.velocity = {};
        agent.desired = {};
        agent.nextWaypointDirection = {};
        agent.goal = static_cast<std::uint8_t>(
            mix_bits(index + tickIndex_ + totalArrivals_) & 1U);
        ++arrivals;
    }
    totalArrivals_ += arrivals;
    return arrivals;
}

Vec2 Simulation::respawn_position(const std::size_t agentIndex) const noexcept {
    const std::uint64_t base = mix_bits(
        config_.seed + agentIndex * 17U + tickIndex_ * 131U + totalArrivals_);
    return {
        2.5F + unit_from_hash(base) * 8.0F,
        17.0F + unit_from_hash(mix_bits(base)) * 20.0F};
}

bool Simulation::can_occupy(const Vec2 position) const noexcept {
    return navigation_.is_walkable(navigation_.cell_at(position));
}

void Simulation::set_gate_open(const std::size_t gateIndex, const bool open) {
    if (gateIndex >= 2 || gateOpen_[gateIndex] == open) {
        return;
    }
    if (!open) {
        evacuate_closing_gate(gateIndex);
    }
    gateOpen_[gateIndex] = open;
    const int startY = gateIndex == 0 ? 9 : 39;
    const int endY = gateIndex == 0 ? 14 : 44;
    for (int y = startY; y <= endY; ++y) {
        navigation_.set_walkable(68, y, open);
    }
    navigationDirty_ = true;
}

void Simulation::evacuate_closing_gate(const std::size_t gateIndex) {
    const int startY = gateIndex == 0 ? 9 : 39;
    const int endY = gateIndex == 0 ? 14 : 44;

    const auto occupiesGate = [startY, endY](const Vec2 position) {
        const int cellX = static_cast<int>(std::floor(position.x));
        const int cellY = static_cast<int>(std::floor(position.y));
        return cellX == 68 && cellY >= startY && cellY <= endY;
    };
    const auto safeSide = [](const Vec2 position, const Vec2 velocity) {
        const bool passedGate = velocity.x >= 0.0F || position.x >= 68.5F;
        return Vec2{passedGate ? 69.05F : 67.95F, position.y};
    };

    if (config_.implementation == Implementation::optimized) {
        for (std::size_t index = 0; index < agents_.size(); ++index) {
            const Vec2 position{agents_.positionX[index], agents_.positionY[index]};
            if (!occupiesGate(position)) {
                continue;
            }
            const Vec2 next = safeSide(
                position,
                {agents_.velocityX[index], agents_.velocityY[index]});
            agents_.positionX[index] = next.x;
            agents_.positionY[index] = next.y;
            agents_.nextPositionX[index] = next.x;
            agents_.nextPositionY[index] = next.y;
        }
    } else {
        for (BaselineAgent& agent : baselineAgents_) {
            if (!occupiesGate(agent.position)) {
                continue;
            }
            agent.position = safeSide(agent.position, agent.velocity);
            agent.nextWaypointDirection = {};
        }
    }
}

void Simulation::publish_render_snapshot() {
    const std::uint32_t current =
        publishedSnapshot_.load(std::memory_order_relaxed);
    const std::uint32_t writeIndex = 1U - current;
    RenderSnapshot& snapshot = renderSnapshots_[writeIndex];
    snapshot.positionX.clear();
    snapshot.positionY.clear();
    snapshot.velocityX.clear();
    snapshot.velocityY.clear();
    snapshot.goal.clear();

    if (config_.implementation == Implementation::optimized) {
        snapshot.positionX.insert(
            snapshot.positionX.end(),
            agents_.positionX.begin(),
            agents_.positionX.end());
        snapshot.positionY.insert(
            snapshot.positionY.end(),
            agents_.positionY.begin(),
            agents_.positionY.end());
        snapshot.velocityX.insert(
            snapshot.velocityX.end(),
            agents_.velocityX.begin(),
            agents_.velocityX.end());
        snapshot.velocityY.insert(
            snapshot.velocityY.end(),
            agents_.velocityY.begin(),
            agents_.velocityY.end());
        snapshot.goal.insert(
            snapshot.goal.end(),
            agents_.goal.begin(),
            agents_.goal.end());
    } else {
        for (const BaselineAgent& agent : baselineAgents_) {
            snapshot.positionX.push_back(agent.position.x);
            snapshot.positionY.push_back(agent.position.y);
            snapshot.velocityX.push_back(agent.velocity.x);
            snapshot.velocityY.push_back(agent.velocity.y);
            snapshot.goal.push_back(agent.goal);
        }
    }
    snapshot.tick = tickIndex_;
    publishedSnapshot_.store(writeIndex, std::memory_order_release);
}

std::uint64_t Simulation::state_hash() const noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    if (config_.implementation == Implementation::optimized) {
        for (std::size_t index = 0; index < agents_.size(); ++index) {
            hash = hash_combine(hash, static_cast<std::uint64_t>(
                quantize(agents_.positionX[index])));
            hash = hash_combine(hash, static_cast<std::uint64_t>(
                quantize(agents_.positionY[index])));
            hash = hash_combine(hash, agents_.goal[index]);
        }
    } else {
        for (const BaselineAgent& agent : baselineAgents_) {
            hash = hash_combine(
                hash,
                static_cast<std::uint64_t>(quantize(agent.position.x)));
            hash = hash_combine(
                hash,
                static_cast<std::uint64_t>(quantize(agent.position.y)));
            hash = hash_combine(hash, agent.goal);
        }
    }
    return hash_combine(hash, tickIndex_);
}

std::size_t Simulation::memory_bytes() const noexcept {
    std::size_t snapshotBytes{};
    for (const RenderSnapshot& snapshot : renderSnapshots_) {
        snapshotBytes +=
            snapshot.positionX.capacity() * sizeof(float) +
            snapshot.positionY.capacity() * sizeof(float) +
            snapshot.velocityX.capacity() * sizeof(float) +
            snapshot.velocityY.capacity() * sizeof(float) +
            snapshot.goal.capacity() * sizeof(std::uint8_t);
    }
    return agents_.memory_bytes() + spatial_.memory_bytes() + snapshotBytes +
        baselineAgents_.capacity() * sizeof(BaselineAgent) +
        baselineNextPositions_.capacity() * sizeof(Vec2);
}

} // namespace rushgrid
