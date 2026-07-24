#include "rushgrid/simulation/simulation.hpp"
#include "rushgrid/telemetry/statistics.hpp"
#include "rushgrid/visualization/debug_image.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

struct DemoArguments {
    std::size_t agents{10'000};
    std::size_t threads{
        std::thread::hardware_concurrency() > 1
            ? std::thread::hardware_concurrency() - 1
            : 0};
    std::uint32_t seed{1337};
    std::size_t ticks{600};
    std::size_t barrierTick{300};
    rushgrid::Implementation implementation{rushgrid::Implementation::optimized};
    std::filesystem::path output{"artifacts/screenshots/rushgrid_debug.ppm"};
};

[[nodiscard]] std::string require_value(
    const int argc,
    char** argv,
    int& index) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + argv[index]);
    }
    return argv[++index];
}

[[nodiscard]] DemoArguments parse_arguments(const int argc, char** argv) {
    DemoArguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--agents") {
            arguments.agents = std::stoull(require_value(argc, argv, index));
        } else if (option == "--threads") {
            arguments.threads = std::stoull(require_value(argc, argv, index));
        } else if (option == "--seed") {
            arguments.seed = static_cast<std::uint32_t>(
                std::stoul(require_value(argc, argv, index)));
        } else if (option == "--ticks") {
            arguments.ticks = std::stoull(require_value(argc, argv, index));
        } else if (option == "--barrier-tick") {
            arguments.barrierTick = std::stoull(require_value(argc, argv, index));
        } else if (option == "--implementation") {
            arguments.implementation = rushgrid::parse_implementation(
                require_value(argc, argv, index));
        } else if (option == "--output") {
            arguments.output = require_value(argc, argv, index);
        } else if (option == "--help") {
            std::cout
                << "RushGrid technical demo\n\n"
                << "Options:\n"
                << "  --agents N                 Active commuters (default 10000)\n"
                << "  --threads N                Worker threads; 0 is single-threaded\n"
                << "  --seed N                   Deterministic scenario seed\n"
                << "  --ticks N                  Fixed 60 Hz ticks to simulate\n"
                << "  --barrier-tick N           Close upper gate on this tick\n"
                << "  --implementation MODE      baseline or optimized\n"
                << "  --output FILE              PPM debug snapshot path\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    if (arguments.agents == 0 || arguments.ticks == 0) {
        throw std::invalid_argument("--agents and --ticks must be greater than zero");
    }
    return arguments;
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const DemoArguments arguments = parse_arguments(argc, argv);
        rushgrid::Simulation simulation({
            arguments.agents,
            arguments.threads,
            arguments.seed,
            arguments.implementation});
        rushgrid::SampleWindow samples(arguments.ticks);

        std::cout << "RushGrid " << rushgrid::to_string(arguments.implementation)
                  << " | agents=" << arguments.agents
                  << " workers=" << arguments.threads
                  << " seed=" << arguments.seed << '\n';

        for (std::size_t tick = 0; tick < arguments.ticks; ++tick) {
            if (tick == arguments.barrierTick) {
                simulation.set_gate_open(0, false);
                std::cout << "incident: upper fare gate closed; flow fields invalidated\n";
            }
            const rushgrid::TickMetrics metrics = simulation.tick();
            samples.push(metrics.totalMs);
            if ((tick + 1) % 60 == 0 || tick + 1 == arguments.ticks) {
                std::cout
                    << "tick " << std::setw(5) << tick + 1
                    << " | " << std::fixed << std::setprecision(3)
                    << metrics.totalMs << " ms"
                    << " | grid " << metrics.gridMs
                    << " | steer " << metrics.steeringMs
                    << " | integrate " << metrics.integrationMs
                    << " | max-cell " << metrics.maximumCellOccupancy
                    << " | arrivals " << simulation.total_arrivals()
                    << '\n';
            }
        }

        rushgrid::write_debug_ppm(simulation, arguments.output);
        std::cout
            << "summary: avg=" << samples.mean()
            << " ms p95=" << samples.percentile(0.95)
            << " ms p99=" << samples.percentile(0.99)
            << " ms state-hash=0x" << std::hex << simulation.state_hash() << std::dec
            << "\ndebug snapshot: " << arguments.output.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "rushgrid_demo: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

