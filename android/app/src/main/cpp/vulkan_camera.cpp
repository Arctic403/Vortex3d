#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace vortex::android {
namespace {

using Mat4 = std::array<float, 16>;

constexpr float kOrbitRadiansPerPixel = 0.0060F;
constexpr float kPitchLimit = 1.5533430343F; // 89 degrees.
constexpr float kMinDistance = 1.5F;
constexpr float kMaxDistance = 50.0F;

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
    cameraDirty_ = true;
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
    cameraDirty_ = true;
    return true;
}

bool VulkanViewport::zoomCamera(const float scaleFactor) noexcept {
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0F) {
        return false;
    }
    camera_.distance = std::clamp(camera_.distance / scaleFactor, kMinDistance, kMaxDistance);
    cameraDirty_ = true;
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

} // namespace vortex::android
