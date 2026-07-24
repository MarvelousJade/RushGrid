#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace rushgrid {

struct Vec2 {
    float x{};
    float y{};

    [[nodiscard]] constexpr Vec2 operator+(const Vec2 other) const noexcept {
        return {x + other.x, y + other.y};
    }

    [[nodiscard]] constexpr Vec2 operator-(const Vec2 other) const noexcept {
        return {x - other.x, y - other.y};
    }

    [[nodiscard]] constexpr Vec2 operator*(const float scalar) const noexcept {
        return {x * scalar, y * scalar};
    }

    constexpr Vec2& operator+=(const Vec2 other) noexcept {
        x += other.x;
        y += other.y;
        return *this;
    }
};

[[nodiscard]] inline float length_squared(const Vec2 value) noexcept {
    return value.x * value.x + value.y * value.y;
}

[[nodiscard]] inline float length(const Vec2 value) noexcept {
    return std::sqrt(length_squared(value));
}

[[nodiscard]] inline Vec2 normalized_or_zero(const Vec2 value) noexcept {
    const float magnitudeSquared = length_squared(value);
    if (magnitudeSquared <= std::numeric_limits<float>::epsilon()) {
        return {};
    }
    const float inverseMagnitude = 1.0F / std::sqrt(magnitudeSquared);
    return value * inverseMagnitude;
}

[[nodiscard]] inline Vec2 clamp_length(const Vec2 value, const float maximum) noexcept {
    const float magnitudeSquared = length_squared(value);
    if (magnitudeSquared <= maximum * maximum) {
        return value;
    }
    return normalized_or_zero(value) * maximum;
}

struct EntityId {
    std::uint32_t index{};
    std::uint32_t generation{};

    friend constexpr bool operator==(EntityId, EntityId) noexcept = default;
};

using CellId = std::uint32_t;

inline constexpr CellId invalidCell = std::numeric_limits<CellId>::max();
inline constexpr std::uint32_t invalidIndex = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] inline std::uint64_t hash_combine(
    std::uint64_t hash,
    const std::uint64_t value) noexcept {
    constexpr std::uint64_t prime = 1099511628211ULL;
    hash ^= value;
    hash *= prime;
    return hash;
}

[[nodiscard]] inline std::string_view compiler_name() noexcept {
#if defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "Unknown";
#endif
}

} // namespace rushgrid

