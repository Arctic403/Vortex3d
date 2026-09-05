#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vortex::android {
namespace {

struct ScreenPoint final {
    float x = 0.0F;
    float y = 0.0F;
};

constexpr float kProjectionEpsilon = 1.0e-5F;
constexpr float kAxisLength = 1.35F;
constexpr float kTouchRadiusPixels = 28.0F;

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

[[nodiscard]] float length3(const vortex::Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] bool projectWorldPoint(
    const CameraPushConstants& camera,
    const VkExtent2D extent,
    const vortex::Vec3 world,
    ScreenPoint& screen) noexcept {
    const auto& matrix = camera.viewProjection;
    const float clipX = matrix[0] * world.x + matrix[4] * world.y + matrix[8] * world.z + matrix[12];
    const float clipY = matrix[1] * world.x + matrix[5] * world.y + matrix[9] * world.z + matrix[13];
    const float clipW = matrix[3] * world.x + matrix[7] * world.y + matrix[11] * world.z + matrix[15];
    if (!std::isfinite(clipX) || !std::isfinite(clipY) || !std::isfinite(clipW) ||
        clipW <= kProjectionEpsilon) {
        return false;
    }

    const float inverseW = 1.0F / clipW;
    const float ndcX = clipX * inverseW;
    const float ndcY = clipY * inverseW;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
        return false;
    }

    screen.x = (ndcX + 1.0F) * 0.5F * static_cast<float>(extent.width);
    // The Vulkan camera projection already flips Y, matching Android's down-positive pixels.
    screen.y = (ndcY + 1.0F) * 0.5F * static_cast<float>(extent.height);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

[[nodiscard]] float pointSegmentDistance(
    const ScreenPoint point,
    const ScreenPoint a,
    const ScreenPoint b) noexcept {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= kProjectionEpsilon) {
        const float px = point.x - a.x;
        const float py = point.y - a.y;
        return std::sqrt(px * px + py * py);
    }
    const float projection = std::clamp(
        ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared,
        0.0F,
        1.0F);
    const float closestX = a.x + projection * dx;
    const float closestY = a.y + projection * dy;
    const float px = point.x - closestX;
    const float py = point.y - closestY;
    return std::sqrt(px * px + py * py);
}

} // namespace

std::optional<GizmoHit> VulkanViewport::hitTestGizmo(
    const vortex::ObjectId objectId,
    const float xPixels,
    const float yPixels) const noexcept {
    if (!objectId || objectId != selectedObject_ ||
        !std::isfinite(xPixels) || !std::isfinite(yPixels) ||
        swapchainExtent_.width == 0U || swapchainExtent_.height == 0U) {
        return std::nullopt;
    }

    const auto draw = std::find_if(
        sceneDrawRanges_.begin(),
        sceneDrawRanges_.end(),
        [objectId](const SceneDrawRange& value) { return value.objectId == objectId; });
    if (draw == sceneDrawRanges_.end()) {
        return std::nullopt;
    }

    const float width = static_cast<float>(swapchainExtent_.width);
    const float height = static_cast<float>(swapchainExtent_.height);
    if (xPixels < 0.0F || yPixels < 0.0F || xPixels > width || yPixels > height) {
        return std::nullopt;
    }

    const CameraPushConstants camera = cameraPushConstants(width / height);
    const vortex::Vec3 originWorld = vortex::transformPoint(draw->worldMatrix, {0.0F, 0.0F, 0.0F});
    ScreenPoint originScreen{};
    if (!projectWorldPoint(camera, swapchainExtent_, originWorld, originScreen)) {
        return std::nullopt;
    }

    const ScreenPoint touch{xPixels, yPixels};
    float bestDistance = std::numeric_limits<float>::max();
    std::optional<GizmoHit> best;
    constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

    for (const GizmoAxis axis : axes) {
        const vortex::Vec3 localAxis = axisVector(axis);
        const vortex::Vec3 visualEndpointWorld = vortex::transformPoint(
            draw->worldMatrix,
            {localAxis.x * kAxisLength, localAxis.y * kAxisLength, localAxis.z * kAxisLength});
        const vortex::Vec3 worldAxisRaw = vortex::transformVector(draw->worldMatrix, localAxis);
        const float worldAxisLength = length3(worldAxisRaw);
        if (!std::isfinite(worldAxisLength) || worldAxisLength <= kProjectionEpsilon) {
            continue;
        }
        const vortex::Vec3 worldAxisUnit{
            worldAxisRaw.x / worldAxisLength,
            worldAxisRaw.y / worldAxisLength,
            worldAxisRaw.z / worldAxisLength,
        };
        const vortex::Vec3 unitEndpointWorld{
            originWorld.x + worldAxisUnit.x,
            originWorld.y + worldAxisUnit.y,
            originWorld.z + worldAxisUnit.z,
        };

        ScreenPoint visualEndpointScreen{};
        ScreenPoint unitEndpointScreen{};
        if (!projectWorldPoint(camera, swapchainExtent_, visualEndpointWorld, visualEndpointScreen) ||
            !projectWorldPoint(camera, swapchainExtent_, unitEndpointWorld, unitEndpointScreen)) {
            continue;
        }

        const float directionX = visualEndpointScreen.x - originScreen.x;
        const float directionY = visualEndpointScreen.y - originScreen.y;
        const float directionLength = std::sqrt(directionX * directionX + directionY * directionY);
        const float unitX = unitEndpointScreen.x - originScreen.x;
        const float unitY = unitEndpointScreen.y - originScreen.y;
        const float pixelsPerWorldUnit = std::sqrt(unitX * unitX + unitY * unitY);
        if (directionLength < 8.0F || pixelsPerWorldUnit < 2.0F || !std::isfinite(pixelsPerWorldUnit)) {
            continue;
        }

        const float distance = pointSegmentDistance(touch, originScreen, visualEndpointScreen);
        if (distance <= kTouchRadiusPixels && distance < bestDistance) {
            bestDistance = distance;
            best = GizmoHit{
                axis,
                directionX / directionLength,
                directionY / directionLength,
                pixelsPerWorldUnit,
            };
        }
    }

    return best;
}

} // namespace vortex::android
