#pragma once

#include <filesystem>

namespace rushgrid {

class Simulation;

void write_debug_ppm(const Simulation& simulation, const std::filesystem::path& output);

} // namespace rushgrid
