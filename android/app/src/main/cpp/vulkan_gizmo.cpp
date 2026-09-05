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
constexpr float kAxisShaftStart = 0.14F;
constexpr float kArrowBase = 1.26F;
constexpr float kMoveTip = 1.55F;
constexpr float kArrowRadius = 0.13F;
constexpr float kAxisShaftRadius = 0.034F;
constexpr float kScaleHandleCenter = 0.98F;
constexpr float kScaleHandleHalfExtent = 0.10F;
constexpr float kRotateRingRadius = 1.45F;
constexpr float kRotateTubeRadius = 0.045F;
constexpr std::size_t kShaftSegments = 10U;
constexpr std::size_t kArrowSegments = 12U;
constexpr std::size_t kRotateRingSegments = 40U;
constexpr std::size_t kRotateTubeSegments = 8U;
// 3D handles stay visually pixel-locked, while invisible touch regions compensate for
// Android display density. Upper bounds prevent neighboring RGB handles from becoming one
// giant overlapping target on very dense screens.
constexpr float kMoveTouchRadiusDp = 24.0F;
constexpr float kScaleTouchRadiusDp = 24.0F;
constexpr float kRotateTouchRadiusDp = 22.0F;
constexpr float kMoveTouchRadiusMinPixels = 30.0F;
constexpr float kScaleTouchRadiusMinPixels = 34.0F;
constexpr float kRotateTouchRadiusMinPixels = 36.0F;
constexpr float kMoveTouchRadiusMaxPixels = 52.0F;
constexpr float kScaleTouchRadiusMaxPixels = 54.0F;
constexpr float kRotateTouchRadiusMaxPixels = 50.0F;
constexpr float kMoveCenterDeadZoneDp = 16.0F;
constexpr float kMoveCenterDeadZoneMaxPixels = 38.0F;
constexpr float kTargetPixelsPerLocalUnit = 92.0F;
constexpr float kProjectionProbeWorldUnits = 0.10F;
constexpr float kMinGizmoWorldScale = 0.025F;
constexpr float kMaxGizmoWorldScale = 12.0F;

constexpr std::array<float, 3> kXColor{0.98F, 0.16F, 0.14F};
constexpr std::array<float, 3> kYColor{0.18F, 0.94F, 0.30F};
constexpr std::array<float, 3> kZColor{0.18F, 0.44F, 1.0F};

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

void addTriangle(
    std::vector<ViewportVertex>& output,
    const vortex::Vec3 a,
    const vortex::Vec3 b,
    const vortex::Vec3 c,
    const std::array<float, 3>& color) {
    output.push_back(ViewportVertex{toArray(a), color});
    output.push_back(ViewportVertex{toArray(b), color});
    output.push_back(ViewportVertex{toArray(c), color});
}

[[nodiscard]] vortex::Vec3 axisCirclePoint(
    const GizmoAxis axis,
    const float distance,
    const float radians,
    const float radius) noexcept {
    const vortex::Vec3 center = scale(axisVector(axis), distance);
    const vortex::Vec3 a = perpendicularA(axis);
    const vortex::Vec3 b = perpendicularB(axis);
    return add(
        center,
        add(
            scale(a, std::cos(radians) * radius),
            scale(b, std::sin(radians) * radius)));
}

void addCylinder(
    std::vector<ViewportVertex>& output,
    const GizmoAxis axis,
    const float start,
    const float end,
    const float radius,
    const std::array<float, 3>& color) {
    const vortex::Vec3 startCenter = scale(axisVector(axis), start);
    const vortex::Vec3 endCenter = scale(axisVector(axis), end);
    for (std::size_t segment = 0U; segment < kShaftSegments; ++segment) {
        const float angle0 = (2.0F * kPi * static_cast<float>(segment)) /
                             static_cast<float>(kShaftSegments);
        const float angle1 = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                             static_cast<float>(kShaftSegments);
        const vortex::Vec3 start0 = axisCirclePoint(axis, start, angle0, radius);
        const vortex::Vec3 start1 = axisCirclePoint(axis, start, angle1, radius);
        const vortex::Vec3 end0 = axisCirclePoint(axis, end, angle0, radius);
        const vortex::Vec3 end1 = axisCirclePoint(axis, end, angle1, radius);

        addTriangle(output, start0, end0, end1, color);
        addTriangle(output, start0, end1, start1, color);
        addTriangle(output, startCenter, start1, start0, color);
        addTriangle(output, endCenter, end0, end1, color);
    }
}

void addCone(
    std::vector<ViewportVertex>& output,
    const GizmoAxis axis,
    const float baseDistance,
    const float tipDistance,
    const float radius,
    const std::array<float, 3>& color) {
    const vortex::Vec3 baseCenter = scale(axisVector(axis), baseDistance);
    const vortex::Vec3 tip = scale(axisVector(axis), tipDistance);
    for (std::size_t segment = 0U; segment < kArrowSegments; ++segment) {
        const float angle0 = (2.0F * kPi * static_cast<float>(segment)) /
                             static_cast<float>(kArrowSegments);
        const float angle1 = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                             static_cast<float>(kArrowSegments);
        const vortex::Vec3 base0 = axisCirclePoint(axis, baseDistance, angle0, radius);
        const vortex::Vec3 base1 = axisCirclePoint(axis, baseDistance, angle1, radius);
        addTriangle(output, base0, tip, base1, color);
        addTriangle(output, baseCenter, base1, base0, color);
    }
}

void addCube(
    std::vector<ViewportVertex>& output,
    const vortex::Vec3 center,
    const float halfExtent,
    const std::array<float, 3>& color) {
    const std::array<vortex::Vec3, 8> corners{{
        {center.x - halfExtent, center.y - halfExtent, center.z - halfExtent},
        {center.x + halfExtent, center.y - halfExtent, center.z - halfExtent},
        {center.x + halfExtent, center.y + halfExtent, center.z - halfExtent},
        {center.x - halfExtent, center.y + halfExtent, center.z - halfExtent},
        {center.x - halfExtent, center.y - halfExtent, center.z + halfExtent},
        {center.x + halfExtent, center.y - halfExtent, center.z + halfExtent},
        {center.x + halfExtent, center.y + halfExtent, center.z + halfExtent},
        {center.x - halfExtent, center.y + halfExtent, center.z + halfExtent},
    }};
    constexpr std::array<std::array<std::size_t, 3>, 12> triangles{{
        {{0U, 2U, 1U}}, {{0U, 3U, 2U}},
        {{4U, 5U, 6U}}, {{4U, 6U, 7U}},
        {{0U, 1U, 5U}}, {{0U, 5U, 4U}},
        {{3U, 7U, 6U}}, {{3U, 6U, 2U}},
        {{0U, 4U, 7U}}, {{0U, 7U, 3U}},
        {{1U, 2U, 6U}}, {{1U, 6U, 5U}},
    }};
    for (const auto& triangle : triangles) {
        addTriangle(
            output,
            corners[triangle[0]],
            corners[triangle[1]],
            corners[triangle[2]],
            color);
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

[[nodiscard]] vortex::Vec3 torusPoint(
    const GizmoAxis axis,
    const float ringRadians,
    const float tubeRadians) noexcept {
    const vortex::Vec3 radial = ringPoint(axis, ringRadians, 1.0F);
    const vortex::Vec3 center = scale(radial, kRotateRingRadius);
    const vortex::Vec3 axisDirection = axisVector(axis);
    return add(
        center,
        add(
            scale(radial, std::cos(tubeRadians) * kRotateTubeRadius),
            scale(axisDirection, std::sin(tubeRadians) * kRotateTubeRadius)));
}

void addTorus(
    std::vector<ViewportVertex>& output,
    const GizmoAxis axis,
    const std::array<float, 3>& color) {
    for (std::size_t ringSegment = 0U; ringSegment < kRotateRingSegments; ++ringSegment) {
        const float ring0 = (2.0F * kPi * static_cast<float>(ringSegment)) /
                            static_cast<float>(kRotateRingSegments);
        const float ring1 = (2.0F * kPi * static_cast<float>(ringSegment + 1U)) /
                            static_cast<float>(kRotateRingSegments);
        for (std::size_t tubeSegment = 0U; tubeSegment < kRotateTubeSegments; ++tubeSegment) {
            const float tube0 = (2.0F * kPi * static_cast<float>(tubeSegment)) /
                                static_cast<float>(kRotateTubeSegments);
            const float tube1 = (2.0F * kPi * static_cast<float>(tubeSegment + 1U)) /
                                static_cast<float>(kRotateTubeSegments);
            const vortex::Vec3 p00 = torusPoint(axis, ring0, tube0);
            const vortex::Vec3 p01 = torusPoint(axis, ring0, tube1);
            const vortex::Vec3 p10 = torusPoint(axis, ring1, tube0);
            const vortex::Vec3 p11 = torusPoint(axis, ring1, tube1);
            addTriangle(output, p00, p10, p11, color);
            addTriangle(output, p00, p11, p01, color);
        }
    }
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

bool VulkanViewport::setDisplayDensity(const float density) noexcept {
    if (!std::isfinite(density) || density <= 0.0F) {
        return false;
    }
    displayDensity_ = std::clamp(density, 0.5F, 8.0F);
    return true;
}

bool VulkanViewport::setGizmoMode(const GizmoMode mode) noexcept {
    if (gizmoMode_ == mode) {
        return true;
    }
    gizmoMode_ = mode;
    if (selectedObject_) {
        selectionOverlayDirty_ = true;
    }
    commandBuffersDirty_ = true;
    return true;
}

std::vector<ViewportVertex> VulkanViewport::buildGizmoVertices() const {
    std::vector<ViewportVertex> vertices;
    vertices.reserve(kGizmoVertexCapacity);

    constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
    switch (gizmoMode_) {
        case GizmoMode::Move:
            for (const GizmoAxis axis : axes) {
                const auto& color = axisColor(axis);
                addCylinder(vertices, axis, kAxisShaftStart, kArrowBase, kAxisShaftRadius, color);
                addCone(vertices, axis, kArrowBase, kMoveTip, kArrowRadius, color);
            }
            break;

        case GizmoMode::Scale:
            for (const GizmoAxis axis : axes) {
                const auto& color = axisColor(axis);
                addCylinder(
                    vertices,
                    axis,
                    kAxisShaftStart,
                    kScaleHandleCenter,
                    kAxisShaftRadius,
                    color);
                addCube(
                    vertices,
                    scale(axisVector(axis), kScaleHandleCenter),
                    kScaleHandleHalfExtent,
                    color);
            }
            break;

        case GizmoMode::Rotate:
            // Thick torus geometry gives Rotate the Blender-style ball/ring appearance
            // without depending on optional wide-line Vulkan features on mobile GPUs.
            for (const GizmoAxis axis : axes) {
                addTorus(vertices, axis, axisColor(axis));
            }
            break;
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

    auto matrixForScale = [&](const float scaleValue) noexcept {
        vortex::TransformMatrix matrix = vortex::identityTransformMatrix();
        matrix.values[0] = xAxis.x * scaleValue;
        matrix.values[1] = xAxis.y * scaleValue;
        matrix.values[2] = xAxis.z * scaleValue;
        matrix.values[4] = yAxis.x * scaleValue;
        matrix.values[5] = yAxis.y * scaleValue;
        matrix.values[6] = yAxis.z * scaleValue;
        matrix.values[8] = zAxis.x * scaleValue;
        matrix.values[9] = zAxis.y * scaleValue;
        matrix.values[10] = zAxis.z * scaleValue;
        matrix.values[12] = origin.x;
        matrix.values[13] = origin.y;
        matrix.values[14] = origin.z;
        return matrix;
    };

    float worldScale = 1.0F;
    if (swapchainExtent_.width != 0U && swapchainExtent_.height != 0U) {
        const float aspect = static_cast<float>(swapchainExtent_.width) /
                             static_cast<float>(swapchainExtent_.height);
        const CameraPushConstants camera = cameraPushConstants(aspect);
        ScreenPoint originScreen{};
        if (projectWorldPoint(camera, swapchainExtent_, origin, originScreen)) {
            float pixelsPerWorldUnit = 0.0F;
            const std::array<vortex::Vec3, 3> worldAxes{xAxis, yAxis, zAxis};
            for (const vortex::Vec3 axis : worldAxes) {
                const vortex::Vec3 probeOffset = scale(axis, kProjectionProbeWorldUnits);
                const vortex::Vec3 positiveProbe = add(origin, probeOffset);
                const vortex::Vec3 negativeProbe = add(origin, scale(probeOffset, -1.0F));
                ScreenPoint positiveScreen{};
                ScreenPoint negativeScreen{};
                const bool hasPositive = projectWorldPoint(
                    camera, swapchainExtent_, positiveProbe, positiveScreen);
                const bool hasNegative = projectWorldPoint(
                    camera, swapchainExtent_, negativeProbe, negativeScreen);

                float axisPixelsPerWorldUnit = 0.0F;
                if (hasPositive && hasNegative) {
                    axisPixelsPerWorldUnit = pointDistance(positiveScreen, negativeScreen) /
                                             (2.0F * kProjectionProbeWorldUnits);
                } else if (hasPositive) {
                    axisPixelsPerWorldUnit = pointDistance(originScreen, positiveScreen) /
                                             kProjectionProbeWorldUnits;
                } else if (hasNegative) {
                    axisPixelsPerWorldUnit = pointDistance(originScreen, negativeScreen) /
                                             kProjectionProbeWorldUnits;
                }

                if (std::isfinite(axisPixelsPerWorldUnit)) {
                    pixelsPerWorldUnit = std::max(pixelsPerWorldUnit, axisPixelsPerWorldUnit);
                }
            }

            if (pixelsPerWorldUnit > kProjectionEpsilon) {
                worldScale = std::clamp(
                    kTargetPixelsPerLocalUnit / pixelsPerWorldUnit,
                    kMinGizmoWorldScale,
                    kMaxGizmoWorldScale);
            }

            float referenceRadius = kMoveTip;
            switch (gizmoMode_) {
                case GizmoMode::Move:
                    referenceRadius = kMoveTip;
                    break;
                case GizmoMode::Rotate:
                    referenceRadius = kRotateRingRadius + kRotateTubeRadius;
                    break;
                case GizmoMode::Scale:
                    referenceRadius = std::sqrt(
                        (kScaleHandleCenter + kScaleHandleHalfExtent) *
                            (kScaleHandleCenter + kScaleHandleHalfExtent) +
                        2.0F * kScaleHandleHalfExtent * kScaleHandleHalfExtent);
                    break;
            }
            const float targetRadiusPixels = referenceRadius * kTargetPixelsPerLocalUnit;
            constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

            // The small derivative probe above gets us close. Calibrate against the finite,
            // actually visible handle footprint so perspective at the ends cannot make the
            // control breathe as the orbit distance changes. Three passes converge well below
            // a pixel on the supported phone zoom range while keeping the geometry fully 3D.
            for (std::size_t iteration = 0U; iteration < 3U; ++iteration) {
                const vortex::TransformMatrix candidate = matrixForScale(worldScale);
                float measuredRadiusPixels = 0.0F;

                const auto measureLocalPoint = [&](const vortex::Vec3 localPoint) {
                    const vortex::Vec3 worldPoint = vortex::transformPoint(candidate, localPoint);
                    ScreenPoint screen{};
                    if (projectWorldPoint(camera, swapchainExtent_, worldPoint, screen)) {
                        measuredRadiusPixels = std::max(
                            measuredRadiusPixels,
                            pointDistance(originScreen, screen));
                    }
                };

                if (gizmoMode_ == GizmoMode::Move) {
                    constexpr std::size_t sampleSegments = 8U;
                    for (const GizmoAxis axis : axes) {
                        measureLocalPoint(scale(axisVector(axis), kMoveTip));
                        for (std::size_t segment = 0U; segment < sampleSegments; ++segment) {
                            const float angle = (2.0F * kPi * static_cast<float>(segment)) /
                                                static_cast<float>(sampleSegments);
                            measureLocalPoint(axisCirclePoint(axis, kArrowBase, angle, kArrowRadius));
                        }
                    }
                } else if (gizmoMode_ == GizmoMode::Scale) {
                    constexpr std::array<float, 2> signs{-1.0F, 1.0F};
                    for (const GizmoAxis axis : axes) {
                        const vortex::Vec3 center = scale(axisVector(axis), kScaleHandleCenter);
                        for (const float sx : signs) {
                            for (const float sy : signs) {
                                for (const float sz : signs) {
                                    measureLocalPoint({
                                        center.x + sx * kScaleHandleHalfExtent,
                                        center.y + sy * kScaleHandleHalfExtent,
                                        center.z + sz * kScaleHandleHalfExtent,
                                    });
                                }
                            }
                        }
                    }
                } else {
                    constexpr std::size_t sampleSegments = 16U;
                    const float outerRadius = kRotateRingRadius + kRotateTubeRadius;
                    for (const GizmoAxis axis : axes) {
                        for (std::size_t segment = 0U; segment < sampleSegments; ++segment) {
                            const float angle = (2.0F * kPi * static_cast<float>(segment)) /
                                                static_cast<float>(sampleSegments);
                            measureLocalPoint(ringPoint(axis, angle, outerRadius));
                        }
                    }
                }

                if (!std::isfinite(measuredRadiusPixels) ||
                    measuredRadiusPixels <= kProjectionEpsilon) {
                    break;
                }

                const float correction = targetRadiusPixels / measuredRadiusPixels;
                if (!std::isfinite(correction) || correction <= 0.0F) {
                    break;
                }
                worldScale = std::clamp(
                    worldScale * correction,
                    kMinGizmoWorldScale,
                    kMaxGizmoWorldScale);
                if (std::abs(correction - 1.0F) <= 0.001F) {
                    break;
                }
            }
        }
    }

    return matrixForScale(worldScale);
}

std::optional<GizmoHit> VulkanViewport::hitTestGizmo(
    const vortex::ObjectId objectId,
    const GizmoMode mode,
    const float xPixels,
    const float yPixels) const noexcept {
    if (!objectId || objectId != selectedObject_ || mode != gizmoMode_ ||
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
    const float moveTouchRadiusPixels = std::clamp(
        kMoveTouchRadiusDp * displayDensity_,
        kMoveTouchRadiusMinPixels,
        kMoveTouchRadiusMaxPixels);
    const float scaleTouchRadiusPixels = std::clamp(
        kScaleTouchRadiusDp * displayDensity_,
        kScaleTouchRadiusMinPixels,
        kScaleTouchRadiusMaxPixels);
    const float rotateTouchRadiusPixels = std::clamp(
        kRotateTouchRadiusDp * displayDensity_,
        kRotateTouchRadiusMinPixels,
        kRotateTouchRadiusMaxPixels);
    const float moveCenterDeadZonePixels = std::clamp(
        kMoveCenterDeadZoneDp * displayDensity_,
        kAxisShaftStart * kTargetPixelsPerLocalUnit,
        kMoveCenterDeadZoneMaxPixels);
    constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};

    for (const GizmoAxis axis : axes) {
        const vortex::Vec3 localAxis = axisVector(axis);

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
            if (directionLength < 10.0F) {
                continue;
            }
            ScreenPoint hitStart{};
            const vortex::Vec3 hitStartWorld = vortex::transformPoint(
                visualMatrix,
                scale(localAxis, kAxisShaftStart));
            if (!projectWorldPoint(camera, swapchainExtent_, hitStartWorld, hitStart)) {
                continue;
            }
            if (pointDistance(touch, visualOriginScreen) < moveCenterDeadZonePixels) {
                continue;
            }
            const float distance = pointSegmentDistance(touch, hitStart, endpoint);
            if (distance <= moveTouchRadiusPixels && distance < bestDistance) {
                bestDistance = distance;
                best = GizmoHit{axis};
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
            if (directionLength < 8.0F) {
                continue;
            }
            const float distance = pointDistance(touch, handle);
            if (distance <= scaleTouchRadiusPixels && distance < bestDistance) {
                bestDistance = distance;
                best = GizmoHit{axis};
            }
            continue;
        }

        // Rotate hit testing follows the visible torus centerline. Gesture motion itself is
        // solved later against the selected ring's 3D plane.
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
            if (distance <= rotateTouchRadiusPixels && distance < bestDistance) {
                bestDistance = distance;
                best = GizmoHit{axis};
            }
        }
    }

    return best;
}

} // namespace vortex::android
