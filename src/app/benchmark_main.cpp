#include "rushgrid/core/types.hpp"
#include "rushgrid/simulation/simulation.hpp"
#include "rushgrid/telemetry/statistics.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

struct BenchmarkArguments {
    std::string scenario{"concourse"};
    std::size_t agents{10'000};
    std::size_t threads{
        std::thread::hardware_concurrency() > 1
            ? std::thread::hardware_concurrency() - 1
            : 0};
    rushgrid::Implementation implementation{rushgrid::Implementation::optimized};
    std::uint32_t seed{1337};
    double warmupSeconds{1.0};
    double durationSeconds{5.0};
    std::size_t repeat{1};
    std::filesystem::path output{"artifacts/csv/run.csv"};
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

[[nodiscard]] BenchmarkArguments parse_arguments(const int argc, char** argv) {
    BenchmarkArguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--scenario") {
            arguments.scenario = require_value(argc, argv, index);
        } else if (option == "--agents") {
            arguments.agents = std::stoull(require_value(argc, argv, index));
        } else if (option == "--threads") {
            arguments.threads = std::stoull(require_value(argc, argv, index));
        } else if (option == "--implementation") {
            arguments.implementation = rushgrid::parse_implementation(
                require_value(argc, argv, index));
        } else if (option == "--seed") {
            arguments.seed = static_cast<std::uint32_t>(
                std::stoul(require_value(argc, argv, index)));
        } else if (option == "--warmup-seconds") {
            arguments.warmupSeconds = std::stod(require_value(argc, argv, index));
        } else if (option == "--duration-seconds") {
            arguments.durationSeconds = std::stod(require_value(argc, argv, index));
        } else if (option == "--repeat") {
            arguments.repeat = std::stoull(require_value(argc, argv, index));
        } else if (option == "--output") {
            arguments.output = require_value(argc, argv, index);
        } else if (option == "--help") {
            std::cout
                << "RushGrid reproducible headless benchmark\n\n"
                << "Options:\n"
                << "  --scenario concourse       Deterministic scenario\n"
                << "  --agents N                 Active commuter count\n"
                << "  --threads N                Worker count; 0 is single-threaded\n"
                << "  --implementation MODE      baseline or optimized\n"
                << "  --seed N                   Random seed\n"
                << "  --warmup-seconds N         Simulated seconds before capture\n"
                << "  --duration-seconds N       Simulated seconds measured\n"
                << "  --repeat N                 Independent repeated runs\n"
                << "  --output FILE              Append raw summaries to CSV\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }

    if (arguments.scenario != "concourse") {
        throw std::invalid_argument("the available scenario is 'concourse'");
    }
    if (arguments.agents == 0 || arguments.repeat == 0 ||
        arguments.warmupSeconds < 0.0 || arguments.durationSeconds <= 0.0) {
        throw std::invalid_argument("invalid benchmark count or duration");
    }
    return arguments;
}

void write_csv_header(std::ofstream& stream) {
    stream
        << "scenario,implementation,agents,workers,seed,repeat,warmup_ticks,"
           "measured_ticks,compiler,average_ms,p50_ms,p95_ms,p99_ms,min_ms,"
           "max_ms,grid_average_ms,navigation_average_ms,steering_average_ms,"
           "integration_average_ms,agent_updates_per_second,total_arrivals,"
           "memory_bytes,state_hash\n";
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const BenchmarkArguments arguments = parse_arguments(argc, argv);
        constexpr double ticksPerSecond = 60.0;
        const std::size_t warmupTicks = static_cast<std::size_t>(
            arguments.warmupSeconds * ticksPerSecond);
        const std::size_t measuredTicks = std::max<std::size_t>(
            1,
            static_cast<std::size_t>(arguments.durationSeconds * ticksPerSecond));

        if (!arguments.output.parent_path().empty()) {
            std::filesystem::create_directories(arguments.output.parent_path());
        }
        const bool needsHeader =
            !std::filesystem::exists(arguments.output) ||
            std::filesystem::file_size(arguments.output) == 0;
        std::ofstream csv(arguments.output, std::ios::app);
        if (!csv) {
            throw std::runtime_error(
                "could not open benchmark output: " + arguments.output.string());
        }
        if (needsHeader) {
            write_csv_header(csv);
        }

        for (std::size_t repeat = 0; repeat < arguments.repeat; ++repeat) {
            rushgrid::Simulation simulation({
                arguments.agents,
                arguments.threads,
                arguments.seed,
                arguments.implementation});
            for (std::size_t tick = 0; tick < warmupTicks; ++tick) {
                static_cast<void>(simulation.tick());
            }

            rushgrid::SampleWindow totalSamples(measuredTicks);
            double gridTotal{};
            double navigationTotal{};
            double steeringTotal{};
            double integrationTotal{};
            for (std::size_t tick = 0; tick < measuredTicks; ++tick) {
                if (tick == measuredTicks / 2) {
                    simulation.set_gate_open(0, false);
                }
                const rushgrid::TickMetrics metrics = simulation.tick();
                totalSamples.push(metrics.totalMs);
                gridTotal += metrics.gridMs;
                navigationTotal += metrics.navigationMs;
                steeringTotal += metrics.steeringMs;
                integrationTotal += metrics.integrationMs;
            }

            const double averageMs = totalSamples.mean();
            const double updatesPerSecond = averageMs > 0.0
                ? static_cast<double>(arguments.agents) * 1000.0 / averageMs
                : 0.0;
            const double divisor = static_cast<double>(measuredTicks);
            csv
                << arguments.scenario << ','
                << rushgrid::to_string(arguments.implementation) << ','
                << arguments.agents << ','
                << arguments.threads << ','
                << arguments.seed << ','
                << repeat + 1 << ','
                << warmupTicks << ','
                << measuredTicks << ','
                << rushgrid::compiler_name() << ','
                << std::fixed << std::setprecision(6)
                << averageMs << ','
                << totalSamples.percentile(0.50) << ','
                << totalSamples.percentile(0.95) << ','
                << totalSamples.percentile(0.99) << ','
                << totalSamples.minimum() << ','
                << totalSamples.maximum() << ','
                << gridTotal / divisor << ','
                << navigationTotal / divisor << ','
                << steeringTotal / divisor << ','
                << integrationTotal / divisor << ','
                << updatesPerSecond << ','
                << simulation.total_arrivals() << ','
                << simulation.memory_bytes() << ','
                << simulation.state_hash()
                << '\n';

            std::cout
                << "run " << repeat + 1 << '/' << arguments.repeat
                << " | " << rushgrid::to_string(arguments.implementation)
                << " | agents=" << arguments.agents
                << " workers=" << arguments.threads
                << " | avg=" << std::fixed << std::setprecision(3)
                << averageMs << " ms"
                << " p95=" << totalSamples.percentile(0.95) << " ms"
                << " | " << std::setprecision(0) << updatesPerSecond
                << " updates/s\n";
        }

        std::cout << "CSV: " << arguments.output.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "rushgrid_benchmark: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

