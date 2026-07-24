#include "rushgrid/visualization/debug_image.hpp"

#include "rushgrid/simulation/simulation.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace rushgrid {
namespace {

struct Pixel {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

void put_pixel(
    std::vector<Pixel>& pixels,
    const int width,
    const int height,
    const int x,
    const int y,
    const Pixel color) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    pixels[static_cast<std::size_t>(y * width + x)] = color;
}

void draw_agent(
    std::vector<Pixel>& pixels,
    const int width,
    const int height,
    const float x,
    const float y,
    const std::uint8_t goal,
    const int scale) {
    const int centerX = static_cast<int>(x * static_cast<float>(scale));
    const int centerY = static_cast<int>(y * static_cast<float>(scale));
    const Pixel color = goal == 0
        ? Pixel{255, 184, 64}
        : Pixel{78, 203, 255};
    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            put_pixel(
                pixels,
                width,
                height,
                centerX + offsetX,
                centerY + offsetY,
                color);
        }
    }
}

} // namespace

void write_debug_ppm(
    const Simulation& simulation,
    const std::filesystem::path& output) {
    constexpr int scale = 8;
    const NavigationGrid& grid = simulation.navigation_grid();
    const int width = static_cast<int>(grid.width()) * scale;
    const int height = static_cast<int>(grid.height()) * scale;
    std::vector<Pixel> pixels(
        static_cast<std::size_t>(width) * height,
        Pixel{16, 22, 31});

    for (std::uint32_t cellY = 0; cellY < grid.height(); ++cellY) {
        for (std::uint32_t cellX = 0; cellX < grid.width(); ++cellX) {
            if (grid.is_walkable(static_cast<int>(cellX), static_cast<int>(cellY))) {
                continue;
            }
            for (int pixelY = 0; pixelY < scale; ++pixelY) {
                for (int pixelX = 0; pixelX < scale; ++pixelX) {
                    put_pixel(
                        pixels,
                        width,
                        height,
                        static_cast<int>(cellX) * scale + pixelX,
                        static_cast<int>(cellY) * scale + pixelY,
                        Pixel{62, 72, 84});
                }
            }
        }
    }

    const RenderSnapshot& snapshot = simulation.render_snapshot();
    for (std::size_t index = 0; index < snapshot.size(); ++index) {
        draw_agent(
            pixels,
            width,
            height,
            snapshot.positionX[index],
            snapshot.positionY[index],
            snapshot.goal[index],
            scale);
    }

    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("could not create debug image: " + output.string());
    }
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    stream.write(
        reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size() * sizeof(Pixel)));
}

} // namespace rushgrid
