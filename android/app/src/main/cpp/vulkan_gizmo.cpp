#include "vulkan_viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace vortex::android {
namespace {

struct ScreenPoint final {
    float x = 0.0F;
    float y = 0.0F;
};

constexpr float kProjectionEpsilon = 1.0e-5F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kMoveTip = 1.72F;
constexpr float kArrowBase = 1.43F;
constexpr float kArrowRadius = 0.16F;
constexpr float kScaleHandleCenter = 1.02F;
constexpr float kScaleHandleHalfExtent = 0.105F;
constexpr float kRotateRingRadius = 0.76F;
constexpr std::size_t kRotateRingSegments = 48U;
constexpr float kMoveTouchRadiusPixels = 30.0F;
constexpr float kScaleTouchRadiusPixels = 34.0F;
constexpr float kRotateTouchRadiusPixels = 27.0F;
constexpr float kTargetPixelsPerLocalUnit = 92.0F;
constexpr float kMinGizmoWorldScale = 0.025F;
constexpr float kMaxGizmoWorldScale = 12.0F;

constexpr std::array<float, 3> kXColor{0.98F, 0.16F, 0.14F};
constexpr std::array<float, 3> kYColor{0.18F, 0.94F, 0.30F};
constexpr std::array<float, 3> kZColor{0.18F, 0.44F, 1.0F};
constexpr std::array<float, 3> kCenterColor{1.0F, 0.82F, 0.20F};

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

[[nodiscard]] vortex::Vec3 perpendicularA(const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X:
            return {0.0F, 1.0F, 0.0F};
        case GizmoAxis::Y:
            return {1.0F, 0.0F, 0.0F};
        case GizmoAxis::Z:
            return {1.0F, 0.0F, 0.0F};
    }
    return {0.0F, 1.0F, 0.0F};
}

[[nodiscard]] vortex::Vec3 perpendicularB(const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X:
            return {0.0F, 0.0F, 1.0F};
        case GizmoAxis::Y:
            return {0.0F, 0.0F, 1.0F};
        case GizmoAxis::Z:
            return {0.0F, 1.0F, 0.0F};
    }
    return {0.0F, 0.0F, 1.0F};
}

[[nodiscard]] const std::array<float, 3>& axisColor(const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X:
            return kXColor;
        case GizmoAxis::Y:
            return kYColor;
        case GizmoAxis::Z:
            return kZColor;
    }
    return kXColor;
}

[[nodiscard]] vortex::Vec3 add(const vortex::Vec3 a, const vortex::Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] vortex::Vec3 scale(const vortex::Vec3 value, const float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] std::array<float, 3> toArray(const vortex::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

void addLine(
    std::vector<ViewportVertex>& output,
    const vortex::Vec3 a,
    const vortex::Vec3 b,
    const std::array<float, 3>& color) {
    output.push_back(ViewportVertex{toArray(a), color});
    output.push_back(ViewportVertex{toArray(b), color});
}

void addWireCube(
    std::vector<ViewportVertex>& output,
    const vortex::Vec3 center,
    const float halfExtent,
    const std::array<float, 3>& color) {
    std::array<vortex::Vec3, 8> corners{};
    std::size_t index = 0U;
    for (int z = -1; z <= 1; z += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int x = -1; x <= 1; x += 2) {
                corners[index++] = {
                    center.x + static_cast<float>(x) * halfExtent,
                    center.y + static_cast<float>(y) * halfExtent,
                    center.z + static_cast<float>(z) * halfExtent,
                };
            }
        }
    }

    constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
        {{0U, 1U}}, {{2U, 3U}}, {{4U, 5U}}, {{6U, 7U}},
        {{0U, 2U}}, {{1U, 3U}}, {{4U, 6U}}, {{5U, 7U}},
        {{0U, 4U}}, {{1U, 5U}}, {{2U, 6U}}, {{3U, 7U}},
    }};
    for (const auto& edge : edges) {
        addLine(output, corners[edge[0]], corners[edge[1]], color);
    }
}

[[nodiscard]] vortex::Vec3 ringPoint(
    const GizmoAxis axis,
    const float radians,
    const float radius) noexcept {
    const float c = std::cos(radians) * radius;
    const float s = std::sin(radians) * radius;
    switch (axis) {
        case GizmoAxis::X:
            return {0.0F, c, s};
        case GizmoAxis::Y:
            return {c, 0.0F, s};
        case GizmoAxis::Z:
            return {c, s, 0.0F};
    }
    return {0.0F, c, s};
}

[[nodiscard]] float length3(const vortex::Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] vortex::Vec3 normalizedOr(
    const vortex::Vec3 value,
    const vortex::Vec3 fallback) noexcept {
    const float length = length3(value);
    if (!std::isfinite(length) || length <= kProjectionEpsilon) {
        return fallback;
    }
    return {value.x / length, value.y / length, value.z / length};
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

[[nodiscard]] float pointDistance(const ScreenPoint a, const ScreenPoint b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

std::vector<ViewportVertex> VulkanViewport::buildGizmoVertices() const {
    std::vector<ViewportVertex> vertices;
    vertices.reserve(kGizmoVertexCapacity);

    addWireCube(vertices, {0.0F, 0.0F, 0.0F}, 0.065F, kCenterColor);

    constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
    for (const GizmoAxis axis : axes) {
        const vortex::Vec3 direction = axisVector(axis);
        const vortex::Vec3 a = perpendicularA(axis);
        const vortex::Vec3 b = perpendicularB(axis);
        const auto& color = axisColor(axis);

        // Move shaft and a four-sided wire arrowhead.
        const vortex::Vec3 arrowBase = scale(direction, kArrowBase);
        const vortex::Vec3 arrowTip = scale(direction, kMoveTip);
        addLine(vertices, {0.0F, 0.0F, 0.0F}, arrowBase, color);

        const vortex::Vec3 p0 = add(add(arrowBase, scale(a, kArrowRadius)), scale(b, kArrowRadius));
        const vortex::Vec3 p1 = add(add(arrowBase, scale(a, -kArrowRadius)), scale(b, kArrowRadius));
        const vortex::Vec3 p2 = add(add(arrowBase, scale(a, -kArrowRadius)), scale(b, -kArrowRadius));
        const vortex::Vec3 p3 = add(add(arrowBase, scale(a, kArrowRadius)), scale(b, -kArrowRadius));
        addLine(vertices, arrowTip, p0, color);
        addLine(vertices, arrowTip, p1, color);
        addLine(vertices, arrowTip, p2, color);
        addLine(vertices, arrowTip, p3, color);
        addLine(vertices, p0, p1, color);
        addLine(vertices, p1, p2, color);
        addLine(vertices, p2, p3, color);
        addLine(vertices, p3, p0, color);

        // Scale handle: a small wire cube placed before the move arrowhead.
        addWireCube(
            vertices,
            scale(direction, kScaleHandleCenter),
            kScaleHandleHalfExtent,
            color);
    }

    // Rotate handles: three colored local-axis rings around the object origin.
    for (const GizmoAxis axis : axes) {
        const auto& color = axisColor(axis);
        for (std::size_t segment = 0U; segment < kRotateRingSegments; ++segment) {
            const float a = (2.0F * kPi * static_cast<float>(segment)) /
                            static_cast<float>(kRotateRingSegments);
            const float b = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                            static_cast<float>(kRotateRingSegments);
            addLine(
                vertices,
                ringPoint(axis, a, kRotateRingRadius),
                ringPoint(axis, b, kRotateRingRadius),
                color);
        }
    }

    return vertices;
}

vortex::TransformMatrix VulkanViewport::gizmoWorldMatrix(
    const vortex::TransformMatrix& objectWorldMatrix) const noexcept {
    const auto& source = objectWorldMatrix.values;
    const vortex::Vec3 xAxis = normalizedOr({source[0], source[1], source[2]}, {1.0F, 0.0F, 0.0F});
    const vortex::Vec3 yAxis = normalizedOr({source[4], source[5], source[6]}, {0.0F, 1.0F, 0.0F});
    const vortex::Vec3 zAxis = normalizedOr({source[8], source[9], source[10]}, {0.0F, 0.0F, 1.0F});
    const vortex::Vec3 origin{source[12], source[13], source[14]};

    float worldScale = 1.0F;
    if (swapchainExtent_.width != 0U && swapchainExtent_.height != 0U) {
        const float aspect = static_cast<float>(swapchainExtent_.width) /
                             static_cast<float>(swapchainExtent_.height);
        const CameraPushConstants camera = cameraPushConstants(aspect);
        const auto& viewProjection = camera.viewProjection;
        const float clipW =
            viewProjection[3] * origin.x +
            viewProjection[7] * origin.y +
            viewProjection[11] * origin.z +
            viewProjection[15];
        const float f = 1.0F / std::tan(camera_.fovYRadians * 0.5F);
        if (std::isfinite(clipW) && clipW > kProjectionEpsilon && std::isfinite(f) && f > kProjectionEpsilon) {
            worldScale = (2.0F * clipW * kTargetPixelsPerLocalUnit) /
                         (static_cast<float>(swapchainExtent_.height) * f);
            worldScale = std::clamp(worldScale, kMinGizmoWorldScale, kMaxGizmoWorldScale);
        }
    }

    vortex::TransformMatrix result = vortex::identityTransformMatrix();
    result.values[0] = xAxis.x * worldScale;
    result.values[1] = xAxis.y * worldScale;
    result.values[2] = xAxis.z * worldScale;
    result.values[4] = yAxis.x * worldScale;
    result.values[5] = yAxis.y * worldScale;
    result.values[6] = yAxis.z * worldScale;
    result.values[8] = zAxis.x * worldScale;
    result.values[9] = zAxis.y * worldScale;
    result.values[10] = zAxis.z * worldScale;
    result.values[12] = origin.x;
    result.values[13] = origin.y;
    result.values[14] = origin.z;
    return result;
}

std::optional<GizmoHit> VulkanViewport::hitTestGizmo(
    const vortex::ObjectId objectId,
    const GizmoMode mode,
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
    const vortex::TransformMatrix visualMatrix = gizmoWorldMatrix(draw->worldMatrix);
    const vortex::Vec3 objectOriginWorld = vortex::transformPoint(
        draw->worldMatrix,
        {0.0F, 0.0F, 0.0F});
    const vortex::Vec3 visualOriginWorld = vortex::transformPoint(
        visualMatrix,
        {0.0F, 0.0F, 0.0F});

    ScreenPoint visualOriginScreen{};
    if (!projectWorldPoint(camera, swapchainExtent_, visualOriginWorld, visualOriginScreen)) {
        return std::nullopt;
    }

    const ScreenPoint touch{xPixels, yPixels};
    float bestDistance = std::numeric_limits<float>::max();
    std::optional<GizmoHit> best;
    constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

    for (const GizmoAxis axis : axes) {
        const vortex::Vec3 localAxis = axisVector(axis);
        const vortex::Vec3 worldAxisRaw = vortex::transformVector(draw->worldMatrix, localAxis);
        const vortex::Vec3 worldAxisUnit = normalizedOr(worldAxisRaw, localAxis);
        const vortex::Vec3 unitEndpointWorld{
            objectOriginWorld.x + worldAxisUnit.x,
            objectOriginWorld.y + worldAxisUnit.y,
            objectOriginWorld.z + worldAxisUnit.z,
        };
        ScreenPoint objectOriginScreen{};
        ScreenPoint unitEndpointScreen{};
        const bool hasWorldAxisProjection =
            projectWorldPoint(camera, swapchainExtent_, objectOriginWorld, objectOriginScreen) &&
            projectWorldPoint(camera, swapchainExtent_, unitEndpointWorld, unitEndpointScreen);
        float pixelsPerWorldUnit = 1.0F;
        if (hasWorldAxisProjection) {
            pixelsPerWorldUnit = pointDistance(objectOriginScreen, unitEndpointScreen);
        }

        if (mode == GizmoMode::Move) {
            ScreenPoint endpoint{};
            const vortex::Vec3 endpointWorld = vortex::transformPoint(
                visualMatrix,
                scale(localAxis, kMoveTip));
            if (!projectWorldPoint(camera, swapchainExtent_, endpointWorld, endpoint)) {
                continue;
            }
            const float directionX = endpoint.x - visualOriginScreen.x;
            const float directionY = endpoint.y - visualOriginScreen.y;
            const float directionLength = std::sqrt(directionX * directionX + directionY * directionY);
            if (directionLength < 10.0F || pixelsPerWorldUnit < 2.0F || !std::isfinite(pixelsPerWorldUnit)) {
                continue;
            }
            const float distance = pointSegmentDistance(touch, visualOriginScreen, endpoint);
            if (distance <= kMoveTouchRadiusPixels && distance < bestDistance) {
                bestDistance = distance;
                best = GizmoHit{
                    axis,
                    directionX / directionLength,
                    directionY / directionLength,
                    pixelsPerWorldUnit,
                };
            }
            continue;
        }

        if (mode == GizmoMode::Scale) {
            ScreenPoint handle{};
            const vortex::Vec3 handleWorld = vortex::transformPoint(
                visualMatrix,
                scale(localAxis, kScaleHandleCenter));
            if (!projectWorldPoint(camera, swapchainExtent_, handleWorld, handle)) {
                continue;
            }
            const float directionX = handle.x - visualOriginScreen.x;
            const float directionY = handle.y - visualOriginScreen.y;
            const float directionLength = std::sqrt(directionX * directionX + directionY * directionY);
            if (directionLength < 8.0F || pixelsPerWorldUnit < 2.0F || !std::isfinite(pixelsPerWorldUnit)) {
                continue;
            }
            const float distance = pointDistance(touch, handle);
            if (distance <= kScaleTouchRadiusPixels && distance < bestDistance) {
                bestDistance = distance;
                best = GizmoHit{
                    axis,
                    directionX / directionLength,
                    directionY / directionLength,
                    pixelsPerWorldUnit,
                };
            }
            continue;
        }

        // Rotate mode: touch the visible colored ring and use the local screen tangent as
        // the drag direction. The ring itself is screen-scaled, so it remains usable when
        // zooming in or out.
        for (std::size_t segment = 0U; segment < kRotateRingSegments; ++segment) {
            const float angleA = (2.0F * kPi * static_cast<float>(segment)) /
                                 static_cast<float>(kRotateRingSegments);
            const float angleB = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                                 static_cast<float>(kRotateRingSegments);
            const vortex::Vec3 aWorld = vortex::transformPoint(
                visualMatrix,
                ringPoint(axis, angleA, kRotateRingRadius));
            const vortex::Vec3 bWorld = vortex::transformPoint(
                visualMatrix,
                ringPoint(axis, angleB, kRotateRingRadius));
            ScreenPoint a{};
            ScreenPoint b{};
            if (!projectWorldPoint(camera, swapchainExtent_, aWorld, a) ||
                !projectWorldPoint(camera, swapchainExtent_, bWorld, b)) {
                continue;
            }
            const float directionX = b.x - a.x;
            const float directionY = b.y - a.y;
            const float directionLength = std::sqrt(directionX * directionX + directionY * directionY);
            if (directionLength <= 1.0F) {
                continue;
            }
            const float distance = pointSegmentDistance(touch, a, b);
            if (distance <= kRotateTouchRadiusPixels && distance < bestDistance) {
                bestDistance = distance;
                best = GizmoHit{
                    axis,
                    directionX / directionLength,
                    directionY / directionLength,
                    1.0F,
                };
            }
        }
    }

    return best;
}

} // namespace vortex::android
