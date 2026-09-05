#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

constexpr float kOrbitRadiansPerPixel = 0.0060F;
constexpr float kPitchLimit = 1.5533430343F; // 89 degrees.
constexpr float kMinDistance = 1.5F;
constexpr float kMaxDistance = 50.0F;
constexpr float kPickEpsilon = 1.0e-6F;

[[nodiscard]] constexpr Mat4 identity() noexcept {
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] Mat4 multiply(const Mat4& a, const Mat4& b) noexcept {
    Mat4 result{};
    for (std::size_t column = 0; column < 4U; ++column) {
        for (std::size_t row = 0; row < 4U; ++row) {
            float value = 0.0F;
            for (std::size_t k = 0; k < 4U; ++k) {
                value += a[k * 4U + row] * b[column * 4U + k];
            }
            result[column * 4U + row] = value;
        }
    }
    return result;
}

[[nodiscard]] Mat4 rotationY(const float radians) noexcept {
    Mat4 result = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result[0] = c;
    result[2] = -s;
    result[8] = s;
    result[10] = c;
    return result;
}

[[nodiscard]] Mat4 rotationX(const float radians) noexcept {
    Mat4 result = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result[5] = c;
    result[6] = s;
    result[9] = -s;
    result[10] = c;
    return result;
}

[[nodiscard]] Mat4 translation(const float x, const float y, const float z) noexcept {
    Mat4 result = identity();
    result[12] = x;
    result[13] = y;
    result[14] = z;
    return result;
}

[[nodiscard]] Mat4 perspectiveVulkan(
    const float fovYRadians,
    const float aspect,
    const float nearPlane,
    const float farPlane) noexcept {
    Mat4 result{};
    const float safeAspect = aspect > 0.001F ? aspect : 0.001F;
    const float f = 1.0F / std::tan(fovYRadians * 0.5F);
    const float q = farPlane / (farPlane - nearPlane);

    result[0] = f / safeAspect;
    result[5] = -f;
    result[10] = q;
    result[11] = 1.0F;
    result[14] = -(q * nearPlane);
    return result;
}

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
        if (pivotMagnitude <= kPickEpsilon) {
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
    if (!std::isfinite(homogeneous.w) || std::abs(homogeneous.w) <= kPickEpsilon) {
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

[[nodiscard]] Vec3f subtract(const Vec3f a, const Vec3f b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
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

[[nodiscard]] std::optional<float> intersectTriangle(
    const Vec3f rayOrigin,
    const Vec3f rayDirection,
    const Vec3f a,
    const Vec3f b,
    const Vec3f c) noexcept {
    const Vec3f edge1 = subtract(b, a);
    const Vec3f edge2 = subtract(c, a);
    const Vec3f p = cross(rayDirection, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) <= kPickEpsilon) {
        return std::nullopt;
    }

    const float inverseDeterminant = 1.0F / determinant;
    const Vec3f t = subtract(rayOrigin, a);
    const float u = dot(t, p) * inverseDeterminant;
    if (u < 0.0F || u > 1.0F) {
        return std::nullopt;
    }

    const Vec3f q = cross(t, edge1);
    const float v = dot(rayDirection, q) * inverseDeterminant;
    if (v < 0.0F || u + v > 1.0F) {
        return std::nullopt;
    }

    const float distance = dot(edge2, q) * inverseDeterminant;
    if (!std::isfinite(distance) || distance <= kPickEpsilon) {
        return std::nullopt;
    }
    return distance;
}

[[nodiscard]] Vec3f fromArray(const std::array<float, 3>& value) noexcept {
    return {value[0], value[1], value[2]};
}

} // namespace

bool VulkanViewport::orbitCamera(const float deltaXPixels, const float deltaYPixels) noexcept {
    if (!std::isfinite(deltaXPixels) || !std::isfinite(deltaYPixels)) {
        return false;
    }
    camera_.yawRadians += deltaXPixels * kOrbitRadiansPerPixel;
    camera_.pitchRadians = std::clamp(
        camera_.pitchRadians + deltaYPixels * kOrbitRadiansPerPixel,
        -kPitchLimit,
        kPitchLimit);
    commandBuffersDirty_ = true;
    return true;
}

bool VulkanViewport::panCamera(const float deltaXPixels, const float deltaYPixels) noexcept {
    if (!std::isfinite(deltaXPixels) || !std::isfinite(deltaYPixels) || swapchainExtent_.height == 0U) {
        return false;
    }

    // Convert screen pixels to view-space world units at the current orbit distance.
    const float visibleHeight = 2.0F * camera_.distance * std::tan(camera_.fovYRadians * 0.5F);
    const float unitsPerPixel = visibleHeight / static_cast<float>(swapchainExtent_.height);
    camera_.panX += deltaXPixels * unitsPerPixel;
    camera_.panY += deltaYPixels * unitsPerPixel;
    commandBuffersDirty_ = true;
    return true;
}

bool VulkanViewport::zoomCamera(const float scaleFactor) noexcept {
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0F) {
        return false;
    }
    camera_.distance = std::clamp(camera_.distance / scaleFactor, kMinDistance, kMaxDistance);
    commandBuffersDirty_ = true;
    return true;
}

CameraPushConstants VulkanViewport::cameraPushConstants(const float aspect) const noexcept {
    const Mat4 yaw = rotationY(camera_.yawRadians);
    const Mat4 pitch = rotationX(camera_.pitchRadians);
    const Mat4 panAndDistance = translation(camera_.panX, camera_.panY, camera_.distance);
    const Mat4 view = multiply(panAndDistance, multiply(pitch, yaw));
    const Mat4 projection = perspectiveVulkan(
        camera_.fovYRadians,
        aspect,
        camera_.nearPlane,
        camera_.farPlane);

    CameraPushConstants push{};
    push.viewProjection = multiply(projection, view);
    return push;
}

std::optional<vortex::FaceId> VulkanViewport::pickFace(
    const float xPixels,
    const float yPixels) const noexcept {
    if (!std::isfinite(xPixels) || !std::isfinite(yPixels) ||
        swapchainExtent_.width == 0U || swapchainExtent_.height == 0U || pickTriangles_.empty()) {
        return std::nullopt;
    }

    const float width = static_cast<float>(swapchainExtent_.width);
    const float height = static_cast<float>(swapchainExtent_.height);
    if (xPixels < 0.0F || yPixels < 0.0F || xPixels > width || yPixels > height) {
        return std::nullopt;
    }

    const float aspect = width / height;
    const CameraPushConstants push = cameraPushConstants(aspect);
    Mat4 inverseViewProjection{};
    if (!invert(push.viewProjection, inverseViewProjection)) {
        return std::nullopt;
    }

    // Vulkan NDC uses z in [0, 1]. The projection matrix flips Y, so Android's
    // top-left/down-positive pixel coordinates map directly to NDC y = 2y/h - 1.
    const float ndcX = (2.0F * xPixels / width) - 1.0F;
    const float ndcY = (2.0F * yPixels / height) - 1.0F;
    const auto nearPoint = unproject(inverseViewProjection, ndcX, ndcY, 0.0F);
    const auto farPoint = unproject(inverseViewProjection, ndcX, ndcY, 1.0F);
    if (!nearPoint || !farPoint) {
        return std::nullopt;
    }

    Vec3f direction = subtract(*farPoint, *nearPoint);
    const float lengthSquared = dot(direction, direction);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kPickEpsilon) {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    direction.x *= inverseLength;
    direction.y *= inverseLength;
    direction.z *= inverseLength;

    float closestDistance = std::numeric_limits<float>::max();
    std::optional<vortex::FaceId> closestFace;
    for (const PickTriangle& triangle : pickTriangles_) {
        const auto distance = intersectTriangle(
            *nearPoint,
            direction,
            fromArray(triangle.a),
            fromArray(triangle.b),
            fromArray(triangle.c));
        if (distance && *distance < closestDistance) {
            closestDistance = *distance;
            closestFace = triangle.sourceFace;
        }
    }
    return closestFace;
}

} // namespace vortex::android
