#ifndef RUBIKS_CUBE_RENDERER_HPP
#define RUBIKS_CUBE_RENDERER_HPP

#include "cube.h"

#include <cstddef>
#include <cstdint>

namespace rubiks {

constexpr std::size_t kRoundedBoxSegments = 6;
constexpr std::size_t kMaxCubeVertices =
    27 * (6 + 12 * kRoundedBoxSegments + 8 * kRoundedBoxSegments * kRoundedBoxSegments) * 6
    + CUBE_STICKER_COUNT * 54;

struct RenderVertex {
    float x, y, z, r, g, b;
};

std::size_t build_cube_vertices(const Cube &cube, float yaw, float pitch,
                               float cameraDistance, int animatedMove,
                               float animationProgress, std::uint32_t width,
                               std::uint32_t height, RenderVertex *vertices,
                               std::size_t capacity);

} // namespace rubiks

#endif
