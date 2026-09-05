#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace vortex::android {
namespace {

using Mat4 = std::array<float, 16>;

struct Vec3f final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Vec4f final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct Ray3f final {
    Vec3f origin{};
    Vec3f direction{};
};

constexpr float kRotationEpsilon = 1.0e-6F;
constexpr float kPlaneParallelEpsilon = 1.0e-4F;

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
        if (pivotMagnitude <= kRotationEpsilon) {
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

[[nodiscard]] std::optional<Vec3f> unproject(
    const Mat4& inverseViewProjection,
    const float x,
    const float y,
    const float z) noexcept {
    const Vec4f homogeneous = transform(inverseViewProjection, x, y, z, 1.0F);
    if (!std::isfinite(homogeneous.w) || std::abs(homogeneous.w) <= kRotationEpsilon) {
        return std::nullopt;
    }
    const float inverseW = 1.0F / homogeneous.w;
    const Vec3f result{
        homogeneous.x * inverseW,
        homogeneous.y * inverseW,
        homogeneous.z * inverseW,
    };
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] Vec3f add(const Vec3f a, const Vec3f b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3f subtract(const Vec3f a, const Vec3f b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3f scale(const Vec3f value, const float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] Vec3f cross(const Vec3f a, const Vec3f b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] float dot(const Vec3f a, const Vec3f b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] std::optional<Vec3f> normalized(const Vec3f value) noexcept {
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kRotationEpsilon) {
        return std::nullopt;
    }
    return scale(value, 1.0F / std::sqrt(lengthSquared));
}

[[nodiscard]] Vec3f toVec3f(const vortex::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] vortex::Vec3 axisVector(const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X:
            return {1.0F, 0.0F, 0.0F};
        case GizmoAxis::Y:
            return {0.0F, 1.0F, 0.0F};
        case GizmoAxis::Z:
            return {0.0F, 0.0F, 1.0F};
    }
    return {1.0F, 0.0F, 0.0F};
}

[[nodiscard]] std::optional<Ray3f> screenRay(
    const Mat4& inverseViewProjection,
    const float width,
    const float height,
    const float xPixels,
    const float yPixels) noexcept {
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
    return Ray3f{*nearPoint, *direction};
}

[[nodiscard]] std::optional<Vec3f> intersectPlane(
    const Ray3f& ray,
    const Vec3f planePoint,
    const Vec3f planeNormal) noexcept {
    const float denominator = dot(planeNormal, ray.direction);
    if (!std::isfinite(denominator) || std::abs(denominator) <= kPlaneParallelEpsilon) {
        return std::nullopt;
    }
    const float distance = dot(planeNormal, subtract(planePoint, ray.origin)) / denominator;
    if (!std::isfinite(distance) || distance < 0.0F) {
        return std::nullopt;
    }
    return add(ray.origin, scale(ray.direction, distance));
}

} // namespace

std::optional<float> VulkanViewport::rotationDragRadians(
    const vortex::TransformMatrix& interactionWorldMatrix,
    const GizmoAxis axis,
    const float previousXPixels,
    const float previousYPixels,
    const float currentXPixels,
    const float currentYPixels) const noexcept {
    if (!std::isfinite(previousXPixels) || !std::isfinite(previousYPixels) ||
        !std::isfinite(currentXPixels) || !std::isfinite(currentYPixels) ||
        swapchainExtent_.width == 0U || swapchainExtent_.height == 0U) {
        return std::nullopt;
    }

    const float width = static_cast<float>(swapchainExtent_.width);
    const float height = static_cast<float>(swapchainExtent_.height);
    const CameraPushConstants camera = cameraPushConstants(width / height);
    Mat4 inverseViewProjection{};
    if (!invert(camera.viewProjection, inverseViewProjection)) {
        return std::nullopt;
    }

    const auto previousRay = screenRay(
        inverseViewProjection, width, height, previousXPixels, previousYPixels);
    const auto currentRay = screenRay(
        inverseViewProjection, width, height, currentXPixels, currentYPixels);
    if (!previousRay || !currentRay) {
        return std::nullopt;
    }

    const Vec3f origin = toVec3f(vortex::transformPoint(
        interactionWorldMatrix, {0.0F, 0.0F, 0.0F}));
    const auto normal = normalized(toVec3f(vortex::transformVector(
        interactionWorldMatrix, axisVector(axis))));
    if (!normal) {
        return std::nullopt;
    }

    const auto previousPoint = intersectPlane(*previousRay, origin, *normal);
    const auto currentPoint = intersectPlane(*currentRay, origin, *normal);
    if (!previousPoint || !currentPoint) {
        return std::nullopt;
    }

    const auto previousRadial = normalized(subtract(*previousPoint, origin));
    const auto currentRadial = normalized(subtract(*currentPoint, origin));
    if (!previousRadial || !currentRadial) {
        return std::nullopt;
    }

    const float cosine = std::clamp(dot(*previousRadial, *currentRadial), -1.0F, 1.0F);
    const float sine = dot(*normal, cross(*previousRadial, *currentRadial));
    const float radians = std::atan2(sine, cosine);
    if (!std::isfinite(radians)) {
        return std::nullopt;
    }
    return radians;
}

} // namespace vortex::android
