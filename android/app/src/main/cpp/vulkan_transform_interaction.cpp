#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace vortex::android {
namespace {

using Mat4 = std::array<float, 16>;

struct Vec4f final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

constexpr float kInteractionEpsilon = 1.0e-6F;

[[nodiscard]] bool invert(const Mat4& source, Mat4& inverse) noexcept {
    float augmented[4][8]{};
    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            augmented[row][column] = source[column * 4U + row];
        }
        augmented[row][4U + row] = 1.0F;
    }

    for (std::size_t column = 0; column < 4U; ++column) {
        std::size_t pivot = column;
        float pivotMagnitude = std::abs(augmented[pivot][column]);
        for (std::size_t row = column + 1U; row < 4U; ++row) {
            const float magnitude = std::abs(augmented[row][column]);
            if (magnitude > pivotMagnitude) {
                pivot = row;
                pivotMagnitude = magnitude;
            }
        }
        if (pivotMagnitude <= kInteractionEpsilon) {
            return false;
        }
        if (pivot != column) {
            for (std::size_t entry = 0; entry < 8U; ++entry) {
                std::swap(augmented[pivot][entry], augmented[column][entry]);
            }
        }

        const float divisor = augmented[column][column];
        for (float& entry : augmented[column]) {
            entry /= divisor;
        }

        for (std::size_t row = 0; row < 4U; ++row) {
            if (row == column) {
                continue;
            }
            const float factor = augmented[row][column];
            for (std::size_t entry = 0; entry < 8U; ++entry) {
                augmented[row][entry] -= factor * augmented[column][entry];
            }
        }
    }

    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            inverse[column * 4U + row] = augmented[row][4U + column];
        }
    }
    return true;
}

[[nodiscard]] Vec4f transform(
    const Mat4& matrix,
    const float x,
    const float y,
    const float z,
    const float w) noexcept {
    return {
        matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12] * w,
        matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13] * w,
        matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14] * w,
        matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15] * w,
    };
}

[[nodiscard]] std::optional<vortex::Vec3> unproject(
    const Mat4& inverseViewProjection,
    const float x,
    const float y,
    const float z) noexcept {
    const Vec4f homogeneous = transform(inverseViewProjection, x, y, z, 1.0F);
    if (!std::isfinite(homogeneous.w) || std::abs(homogeneous.w) <= kInteractionEpsilon) {
        return std::nullopt;
    }
    const float inverseW = 1.0F / homogeneous.w;
    const vortex::Vec3 result{
        homogeneous.x * inverseW,
        homogeneous.y * inverseW,
        homogeneous.z * inverseW,
    };
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] vortex::Vec3 subtract(const vortex::Vec3 a, const vortex::Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] vortex::Vec3 scale(const vortex::Vec3 v, const float s) noexcept {
    return {v.x * s, v.y * s, v.z * s};
}

[[nodiscard]] float dot(const vortex::Vec3 a, const vortex::Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] vortex::Vec3 cross(const vortex::Vec3 a, const vortex::Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] std::optional<vortex::Vec3> normalized(const vortex::Vec3 value) noexcept {
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kInteractionEpsilon * kInteractionEpsilon) {
        return std::nullopt;
    }
    return scale(value, 1.0F / std::sqrt(lengthSquared));
}

} // namespace

std::optional<PointerRay> VulkanViewport::gizmoPointerRay(
    const float xPixels,
    const float yPixels) const noexcept {
    if (!std::isfinite(xPixels) || !std::isfinite(yPixels) ||
        swapchainExtent_.width == 0U || swapchainExtent_.height == 0U) {
        return std::nullopt;
    }

    const float width = static_cast<float>(swapchainExtent_.width);
    const float height = static_cast<float>(swapchainExtent_.height);
    if (xPixels < 0.0F || yPixels < 0.0F || xPixels > width || yPixels > height) {
        return std::nullopt;
    }

    const CameraPushConstants camera = cameraPushConstants(width / height);
    Mat4 inverseViewProjection{};
    if (!invert(camera.viewProjection, inverseViewProjection)) {
        return std::nullopt;
    }

    const float ndcX = (2.0F * xPixels / width) - 1.0F;
    const float ndcY = (2.0F * yPixels / height) - 1.0F;
    const auto nearPoint = unproject(inverseViewProjection, ndcX, ndcY, 0.0F);
    const auto farPoint = unproject(inverseViewProjection, ndcX, ndcY, 1.0F);
    if (!nearPoint || !farPoint) {
        return std::nullopt;
    }
    const auto direction = normalized(subtract(*farPoint, *nearPoint));
    if (!direction) {
        return std::nullopt;
    }
    return PointerRay{*nearPoint, *direction};
}

std::optional<GizmoCameraFrame> VulkanViewport::gizmoCameraFrame() const noexcept {
    if (swapchainExtent_.width < 4U || swapchainExtent_.height < 4U) {
        return std::nullopt;
    }
    const float cx = static_cast<float>(swapchainExtent_.width) * 0.5F;
    const float cy = static_cast<float>(swapchainExtent_.height) * 0.5F;
    constexpr float probe = 2.0F;

    const auto center = gizmoPointerRay(cx, cy);
    const auto left = gizmoPointerRay(cx - probe, cy);
    const auto rightRay = gizmoPointerRay(cx + probe, cy);
    const auto top = gizmoPointerRay(cx, cy - probe);
    const auto bottom = gizmoPointerRay(cx, cy + probe);
    if (!center || !left || !rightRay || !top || !bottom) {
        return std::nullopt;
    }

    const auto forward = normalized(center->direction);
    if (!forward) {
        return std::nullopt;
    }

    vortex::Vec3 screenRight = subtract(rightRay->direction, left->direction);
    screenRight = subtract(screenRight, scale(*forward, dot(screenRight, *forward)));
    const auto right = normalized(screenRight);
    if (!right) {
        return std::nullopt;
    }

    // Android screen Y grows downward, so screen-up is top minus bottom.
    vortex::Vec3 screenUp = subtract(top->direction, bottom->direction);
    screenUp = subtract(screenUp, scale(*forward, dot(screenUp, *forward)));
    screenUp = subtract(screenUp, scale(*right, dot(screenUp, *right)));
    auto up = normalized(screenUp);
    if (!up) {
        return std::nullopt;
    }

    // Force a stable right-handed frame while keeping forward pointed into the scene.
    auto rebuiltForward = normalized(cross(*right, *up));
    if (!rebuiltForward) {
        return std::nullopt;
    }
    if (dot(*rebuiltForward, *forward) < 0.0F) {
        up = scale(*up, -1.0F);
        rebuiltForward = normalized(cross(*right, *up));
        if (!rebuiltForward) {
            return std::nullopt;
        }
    }

    return GizmoCameraFrame{*rebuiltForward, *right, *up};
}

} // namespace vortex::android
