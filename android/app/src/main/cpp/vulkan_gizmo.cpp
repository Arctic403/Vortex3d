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
constexpr float kPlaneHandleOffset = 0.43F;
constexpr float kMovePlaneHalfExtent = 0.15F;
constexpr float kScalePlaneHalfExtent = 0.13F;
constexpr float kCenterHandleHalfExtent = 0.085F;
constexpr float kUniformScaleRingRadius = 0.30F;
constexpr float kUniformScaleRingTubeRadius = 0.045F;
constexpr float kRotateRingRadius = 1.45F;
constexpr float kViewRingRadius = 1.68F;
constexpr float kViewRingTubeRadius = 0.038F;
constexpr float kRotateTubeRadius = 0.045F;
constexpr float kRotateActiveTubeRadius = 0.060F;
constexpr float kRotateFeedbackRadius = 1.16F;
constexpr float kRotateFeedbackHalfWidth = 0.028F;
constexpr float kRotateFeedbackSpokeInnerRadius = 0.18F;
constexpr float kRotateFeedbackSpokeOuterRadius = 1.30F;
constexpr std::size_t kRotateFeedbackMaxSegments = 36U;
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
constexpr float kPlaneTouchRadiusDp = 22.0F;
constexpr float kCenterTouchRadiusDp = 24.0F;
constexpr float kViewRingTouchRadiusDp = 20.0F;
constexpr float kTargetPixelsPerLocalUnit = 92.0F;
constexpr float kProjectionProbeWorldUnits = 0.10F;
constexpr float kMinGizmoWorldScale = 0.025F;
constexpr float kMaxGizmoWorldScale = 12.0F;

constexpr std::array<float, 3> kXColor{0.98F, 0.16F, 0.14F};
constexpr std::array<float, 3> kYColor{0.18F, 0.94F, 0.30F};
constexpr std::array<float, 3> kZColor{0.18F, 0.44F, 1.0F};
constexpr std::array<float, 3> kNeutralColor{0.88F, 0.88F, 0.88F};
constexpr std::array<float, 3> kCenterColor{0.96F, 0.80F, 0.24F};

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


[[nodiscard]] std::optional<vortex::Quaternion> resolvedGizmoRotation(
    const TransformOrientation orientation,
    const vortex::Quaternion& objectWorldRotation,
    const std::optional<GizmoCameraFrame>& camera) noexcept {
    switch (orientation) {
        case TransformOrientation::Local:
            return vortex::normalizedQuaternion(objectWorldRotation);
        case TransformOrientation::Global:
            return vortex::Quaternion{};
        case TransformOrientation::View:
            if (!camera) return std::nullopt;
            return vortex::quaternionFromBasis(camera->right, camera->up, camera->forward);
    }
    return std::nullopt;
}

[[nodiscard]] std::array<float, 3> highlightedColor(
    const std::array<float, 3>& color) noexcept {
    constexpr float kWhiteMix = 0.42F;
    return {
        std::clamp(color[0] * (1.0F - kWhiteMix) + kWhiteMix, 0.0F, 1.0F),
        std::clamp(color[1] * (1.0F - kWhiteMix) + kWhiteMix, 0.0F, 1.0F),
        std::clamp(color[2] * (1.0F - kWhiteMix) + kWhiteMix, 0.0F, 1.0F),
    };
}

[[nodiscard]] float ringRotationSign(const GizmoAxis axis) noexcept {
    // ringPoint(Y) uses +Z for increasing parameter while positive Y-axis rotation moves
    // +X toward -Z. X and Z parameterizations follow the positive axis-angle direction.
    return axis == GizmoAxis::Y ? -1.0F : 1.0F;
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


[[nodiscard]] std::array<float, 3> mixedAxisColor(
    const GizmoAxis a,
    const GizmoAxis b) noexcept {
    const auto& ca = axisColor(a);
    const auto& cb = axisColor(b);
    return {
        (ca[0] + cb[0]) * 0.5F,
        (ca[1] + cb[1]) * 0.5F,
        (ca[2] + cb[2]) * 0.5F,
    };
}

[[nodiscard]] std::pair<GizmoAxis, GizmoAxis> planeAxesLocal(const GizmoPlane plane) noexcept {
    switch (plane) {
        case GizmoPlane::XY: return {GizmoAxis::X, GizmoAxis::Y};
        case GizmoPlane::XZ: return {GizmoAxis::X, GizmoAxis::Z};
        case GizmoPlane::YZ: return {GizmoAxis::Y, GizmoAxis::Z};
        case GizmoPlane::View: return {GizmoAxis::X, GizmoAxis::Y};
    }
    return {GizmoAxis::X, GizmoAxis::Y};
}

[[nodiscard]] vortex::Vec3 planePointLocal(
    const GizmoPlane plane,
    const float u,
    const float v) noexcept {
    const auto axes = planeAxesLocal(plane);
    return add(scale(axisVector(axes.first), u), scale(axisVector(axes.second), v));
}

void addPlaneHandle(
    std::vector<ViewportVertex>& output,
    const GizmoPlane plane,
    const float halfExtent,
    const std::array<float, 3>& color) {
    const float lo = kPlaneHandleOffset - halfExtent;
    const float hi = kPlaneHandleOffset + halfExtent;
    const vortex::Vec3 p00 = planePointLocal(plane, lo, lo);
    const vortex::Vec3 p10 = planePointLocal(plane, hi, lo);
    const vortex::Vec3 p11 = planePointLocal(plane, hi, hi);
    const vortex::Vec3 p01 = planePointLocal(plane, lo, hi);
    addTriangle(output, p00, p10, p11, color);
    addTriangle(output, p00, p11, p01, color);
}

[[nodiscard]] vortex::Vec3 basisRingPoint(
    const vortex::Vec3 basisA,
    const vortex::Vec3 basisB,
    const float radians,
    const float radius) noexcept {
    return add(
        scale(basisA, std::cos(radians) * radius),
        scale(basisB, std::sin(radians) * radius));
}

void addBasisTorus(
    std::vector<ViewportVertex>& output,
    const vortex::Vec3 basisA,
    const vortex::Vec3 basisB,
    const vortex::Vec3 normal,
    const float ringRadius,
    const float tubeRadius,
    const std::array<float, 3>& color) {
    for (std::size_t ringSegment = 0U; ringSegment < kRotateRingSegments; ++ringSegment) {
        const float ring0 = (2.0F * kPi * static_cast<float>(ringSegment)) /
                            static_cast<float>(kRotateRingSegments);
        const float ring1 = (2.0F * kPi * static_cast<float>(ringSegment + 1U)) /
                            static_cast<float>(kRotateRingSegments);
        const vortex::Vec3 radial0 = basisRingPoint(basisA, basisB, ring0, 1.0F);
        const vortex::Vec3 radial1 = basisRingPoint(basisA, basisB, ring1, 1.0F);
        const vortex::Vec3 center0 = scale(radial0, ringRadius);
        const vortex::Vec3 center1 = scale(radial1, ringRadius);
        for (std::size_t tubeSegment = 0U; tubeSegment < kRotateTubeSegments; ++tubeSegment) {
            const float tube0 = (2.0F * kPi * static_cast<float>(tubeSegment)) /
                                static_cast<float>(kRotateTubeSegments);
            const float tube1 = (2.0F * kPi * static_cast<float>(tubeSegment + 1U)) /
                                static_cast<float>(kRotateTubeSegments);
            const vortex::Vec3 p00 = add(center0, add(
                scale(radial0, std::cos(tube0) * tubeRadius),
                scale(normal, std::sin(tube0) * tubeRadius)));
            const vortex::Vec3 p01 = add(center0, add(
                scale(radial0, std::cos(tube1) * tubeRadius),
                scale(normal, std::sin(tube1) * tubeRadius)));
            const vortex::Vec3 p10 = add(center1, add(
                scale(radial1, std::cos(tube0) * tubeRadius),
                scale(normal, std::sin(tube0) * tubeRadius)));
            const vortex::Vec3 p11 = add(center1, add(
                scale(radial1, std::cos(tube1) * tubeRadius),
                scale(normal, std::sin(tube1) * tubeRadius)));
            addTriangle(output, p00, p10, p11, color);
            addTriangle(output, p00, p11, p01, color);
        }
    }
}

void addBasisRotationFeedback(
    std::vector<ViewportVertex>& output,
    const vortex::Vec3 basisA,
    const vortex::Vec3 basisB,
    const GizmoInteractionFeedback& feedback,
    const std::array<float, 3>& color) {
    if (!feedback.hasRotationReference) return;
    const float start = feedback.rotationStartRingRadians;
    const float current = feedback.rotationCurrentRingRadians;
    const auto addSpoke = [&](const float angle, const float width) {
        const vortex::Vec3 radial = basisRingPoint(basisA, basisB, angle, 1.0F);
        const vortex::Vec3 tangent = add(
            scale(basisA, -std::sin(angle)),
            scale(basisB, std::cos(angle)));
        const vortex::Vec3 inner = scale(radial, kRotateFeedbackSpokeInnerRadius);
        const vortex::Vec3 outer = scale(radial, kRotateFeedbackSpokeOuterRadius);
        const vortex::Vec3 w = scale(tangent, width);
        addTriangle(output, add(inner, w), add(outer, w), add(outer, scale(w, -1.0F)), color);
        addTriangle(output, add(inner, w), add(outer, scale(w, -1.0F)), add(inner, scale(w, -1.0F)), color);
    };
    addSpoke(start, kRotateFeedbackHalfWidth);
    addSpoke(current, kRotateFeedbackHalfWidth * 1.35F);

    const float sweep = std::remainder(feedback.rotationRadians, 2.0F * kPi);
    const float magnitude = std::abs(sweep);
    if (magnitude <= 1.0e-4F) return;
    const std::size_t segments = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil(
            magnitude / (2.0F * kPi) * static_cast<float>(kRotateFeedbackMaxSegments))),
        2U,
        kRotateFeedbackMaxSegments);
    for (std::size_t segment = 0U; segment < segments; ++segment) {
        const float t0 = static_cast<float>(segment) / static_cast<float>(segments);
        const float t1 = static_cast<float>(segment + 1U) / static_cast<float>(segments);
        const float a0 = start + sweep * t0;
        const float a1 = start + sweep * t1;
        const vortex::Vec3 inner0 = basisRingPoint(
            basisA, basisB, a0, kRotateFeedbackRadius - kRotateFeedbackHalfWidth);
        const vortex::Vec3 outer0 = basisRingPoint(
            basisA, basisB, a0, kRotateFeedbackRadius + kRotateFeedbackHalfWidth);
        const vortex::Vec3 inner1 = basisRingPoint(
            basisA, basisB, a1, kRotateFeedbackRadius - kRotateFeedbackHalfWidth);
        const vortex::Vec3 outer1 = basisRingPoint(
            basisA, basisB, a1, kRotateFeedbackRadius + kRotateFeedbackHalfWidth);
        addTriangle(output, inner0, outer0, outer1, color);
        addTriangle(output, inner0, outer1, inner1, color);
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
    const float tubeRadians,
    const float tubeRadius) noexcept {
    const vortex::Vec3 radial = ringPoint(axis, ringRadians, 1.0F);
    const vortex::Vec3 center = scale(radial, kRotateRingRadius);
    const vortex::Vec3 axisDirection = axisVector(axis);
    return add(
        center,
        add(
            scale(radial, std::cos(tubeRadians) * tubeRadius),
            scale(axisDirection, std::sin(tubeRadians) * tubeRadius)));
}

void addTorus(
    std::vector<ViewportVertex>& output,
    const GizmoAxis axis,
    const float tubeRadius,
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
            const vortex::Vec3 p00 = torusPoint(axis, ring0, tube0, tubeRadius);
            const vortex::Vec3 p01 = torusPoint(axis, ring0, tube1, tubeRadius);
            const vortex::Vec3 p10 = torusPoint(axis, ring1, tube0, tubeRadius);
            const vortex::Vec3 p11 = torusPoint(axis, ring1, tube1, tubeRadius);
            addTriangle(output, p00, p10, p11, color);
            addTriangle(output, p00, p11, p01, color);
        }
    }
}

[[nodiscard]] vortex::Vec3 ringTangent(
    const GizmoAxis axis,
    const float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    switch (axis) {
        case GizmoAxis::X:
            return {0.0F, -s, c};
        case GizmoAxis::Y:
            return {-s, 0.0F, c};
        case GizmoAxis::Z:
            return {-s, c, 0.0F};
    }
    return {0.0F, -s, c};
}

void addPlanarSpoke(
    std::vector<ViewportVertex>& output,
    const GizmoAxis axis,
    const float radians,
    const float innerRadius,
    const float outerRadius,
    const float halfWidth,
    const std::array<float, 3>& color) {
    const vortex::Vec3 radial = ringPoint(axis, radians, 1.0F);
    const vortex::Vec3 tangent = ringTangent(axis, radians);
    const vortex::Vec3 inner = scale(radial, innerRadius);
    const vortex::Vec3 outer = scale(radial, outerRadius);
    const vortex::Vec3 width = scale(tangent, halfWidth);
    addTriangle(output, add(inner, width), add(outer, width), add(outer, scale(width, -1.0F)), color);
    addTriangle(output, add(inner, width), add(outer, scale(width, -1.0F)), add(inner, scale(width, -1.0F)), color);
}

void addRotationFeedback(
    std::vector<ViewportVertex>& output,
    const GizmoAxis axis,
    const GizmoInteractionFeedback& feedback,
    const std::array<float, 3>& color) {
    if (!feedback.hasRotationReference) {
        return;
    }

    const float directionSign = ringRotationSign(axis);
    const float startLocal =
        feedback.rotationStartRingRadians - directionSign * feedback.rotationRadians;
    const float currentLocal =
        feedback.rotationCurrentRingRadians - directionSign * feedback.rotationRadians;

    addPlanarSpoke(
        output,
        axis,
        startLocal,
        kRotateFeedbackSpokeInnerRadius,
        kRotateFeedbackSpokeOuterRadius,
        kRotateFeedbackHalfWidth,
        color);
    addPlanarSpoke(
        output,
        axis,
        currentLocal,
        kRotateFeedbackSpokeInnerRadius,
        kRotateFeedbackSpokeOuterRadius,
        kRotateFeedbackHalfWidth * 1.35F,
        color);

    const float visibleSweep = std::remainder(
        directionSign * feedback.rotationRadians,
        2.0F * kPi);
    const float sweepMagnitude = std::abs(visibleSweep);
    if (sweepMagnitude <= 1.0e-4F) {
        return;
    }

    const std::size_t segments = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil(
            sweepMagnitude / (2.0F * kPi) * static_cast<float>(kRotateFeedbackMaxSegments))),
        2U,
        kRotateFeedbackMaxSegments);
    const float arcStart = startLocal;
    for (std::size_t segment = 0U; segment < segments; ++segment) {
        const float t0 = static_cast<float>(segment) / static_cast<float>(segments);
        const float t1 = static_cast<float>(segment + 1U) / static_cast<float>(segments);
        const float a0 = arcStart + visibleSweep * t0;
        const float a1 = arcStart + visibleSweep * t1;
        const vortex::Vec3 inner0 = ringPoint(
            axis, a0, kRotateFeedbackRadius - kRotateFeedbackHalfWidth);
        const vortex::Vec3 outer0 = ringPoint(
            axis, a0, kRotateFeedbackRadius + kRotateFeedbackHalfWidth);
        const vortex::Vec3 inner1 = ringPoint(
            axis, a1, kRotateFeedbackRadius - kRotateFeedbackHalfWidth);
        const vortex::Vec3 outer1 = ringPoint(
            axis, a1, kRotateFeedbackRadius + kRotateFeedbackHalfWidth);
        addTriangle(output, inner0, outer0, outer1, color);
        addTriangle(output, inner0, outer1, inner1, color);
    }
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

bool VulkanViewport::setGizmoOrientation(const TransformOrientation orientation) noexcept {
    if (gizmoOrientation_ == orientation) {
        return true;
    }
    gizmoOrientation_ = orientation;
    if (selectedObject_) {
        selectionOverlayDirty_ = true;
    }
    commandBuffersDirty_ = true;
    return true;
}

bool VulkanViewport::setGizmoInteractionFeedback(
    const GizmoInteractionFeedback& feedback) noexcept {
    if (feedback.active &&
        (feedback.mode != gizmoMode_ || !feedback.hasFrozenOrientation ||
         !vortex::normalizedQuaternion(feedback.frozenOrientationWorld))) {
        return false;
    }
    if (!std::isfinite(feedback.rotationRadians) ||
        !std::isfinite(feedback.rotationStartRingRadians) ||
        !std::isfinite(feedback.rotationCurrentRingRadians)) {
        return false;
    }

    gizmoInteractionFeedback_ = feedback;
    if (selectedObject_) {
        selectionOverlayDirty_ = true;
    }
    commandBuffersDirty_ = true;
    return true;
}

std::optional<vortex::Quaternion> VulkanViewport::visualGizmoRotation(
    const vortex::Quaternion& worldRotation) const noexcept {
    if (gizmoInteractionFeedback_.active && gizmoInteractionFeedback_.hasFrozenOrientation) {
        return vortex::normalizedQuaternion(gizmoInteractionFeedback_.frozenOrientationWorld);
    }
    return resolvedGizmoRotation(gizmoOrientation_, worldRotation, gizmoCameraFrame());
}

std::vector<ViewportVertex> VulkanViewport::buildGizmoVertices() const {
    std::vector<ViewportVertex> vertices;
    vertices.reserve(kGizmoVertexCapacity);

    const auto isActiveHandle = [this](const GizmoHandle handle) noexcept {
        return gizmoInteractionFeedback_.active &&
               gizmoInteractionFeedback_.mode == gizmoMode_ &&
               gizmoInteractionFeedback_.handle == handle;
    };
    const auto handleColor = [&](const GizmoHandle handle,
                                 const std::array<float, 3>& base) noexcept {
        return isActiveHandle(handle) ? highlightedColor(base) : base;
    };

    constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
    constexpr std::array<GizmoPlane, 3> planes{GizmoPlane::XY, GizmoPlane::XZ, GizmoPlane::YZ};

    switch (gizmoMode_) {
        case GizmoMode::Move:
            for (const GizmoAxis axis : axes) {
                const auto color = handleColor(axisGizmoHandle(axis), axisColor(axis));
                addCylinder(vertices, axis, kAxisShaftStart, kArrowBase, kAxisShaftRadius, color);
                addCone(vertices, axis, kArrowBase, kMoveTip, kArrowRadius, color);
            }
            for (const GizmoPlane plane : planes) {
                const auto [a, b] = planeAxesLocal(plane);
                const GizmoHandle handle = plane == GizmoPlane::XY ? GizmoHandle::PlaneXY :
                                           plane == GizmoPlane::XZ ? GizmoHandle::PlaneXZ :
                                                                   GizmoHandle::PlaneYZ;
                addPlaneHandle(
                    vertices,
                    plane,
                    kMovePlaneHalfExtent,
                    handleColor(handle, mixedAxisColor(a, b)));
            }
            addCube(
                vertices,
                {},
                kCenterHandleHalfExtent,
                handleColor(GizmoHandle::Center, kCenterColor));
            break;

        case GizmoMode::Scale:
            for (const GizmoAxis axis : axes) {
                const auto color = handleColor(axisGizmoHandle(axis), axisColor(axis));
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
            for (const GizmoPlane plane : planes) {
                const auto [a, b] = planeAxesLocal(plane);
                const GizmoHandle handle = plane == GizmoPlane::XY ? GizmoHandle::PlaneXY :
                                           plane == GizmoPlane::XZ ? GizmoHandle::PlaneXZ :
                                                                   GizmoHandle::PlaneYZ;
                addPlaneHandle(
                    vertices,
                    plane,
                    kScalePlaneHalfExtent,
                    handleColor(handle, mixedAxisColor(a, b)));
            }
            if (const auto camera = gizmoCameraFrame()) {
                const auto frameRotation = visualGizmoRotation(selectionWorldRotation_);
                const auto inverseFrame = frameRotation
                    ? vortex::conjugateQuaternion(*frameRotation)
                    : std::nullopt;
                if (inverseFrame) {
                    const auto localRight = vortex::rotateVectorByQuaternion(*inverseFrame, camera->right);
                    const auto localUp = vortex::rotateVectorByQuaternion(*inverseFrame, camera->up);
                    const auto localForward = vortex::rotateVectorByQuaternion(*inverseFrame, camera->forward);
                    if (localRight && localUp && localForward) {
                        addBasisTorus(
                            vertices,
                            *localRight,
                            *localUp,
                            *localForward,
                            kUniformScaleRingRadius,
                            kUniformScaleRingTubeRadius,
                            handleColor(GizmoHandle::UniformScale, kNeutralColor));
                    }
                }
            }
            break;

        case GizmoMode::Rotate:
            for (const GizmoAxis axis : axes) {
                const bool active = isActiveHandle(axisGizmoHandle(axis));
                const auto color = handleColor(axisGizmoHandle(axis), axisColor(axis));
                addTorus(
                    vertices,
                    axis,
                    active ? kRotateActiveTubeRadius : kRotateTubeRadius,
                    color);
                if (active) {
                    addRotationFeedback(
                        vertices,
                        axis,
                        gizmoInteractionFeedback_,
                        highlightedColor(color));
                }
            }

            // The view ring is generated in local gizmo coordinates from the live camera frame.
            // During a drag, the gizmo's orientation comes from the same frozen frame used by
            // the constraint solver, so Local rotation cannot make the visible rings chase the
            // changing object preview.
            if (const auto camera = gizmoCameraFrame()) {
                const auto frameRotation = visualGizmoRotation(selectionWorldRotation_);
                const auto inverseWorld = frameRotation
                    ? vortex::conjugateQuaternion(*frameRotation)
                    : std::nullopt;
                if (inverseWorld) {
                    const auto localRight = vortex::rotateVectorByQuaternion(*inverseWorld, camera->right);
                    const auto localUp = vortex::rotateVectorByQuaternion(*inverseWorld, camera->up);
                    const auto localForward = vortex::rotateVectorByQuaternion(*inverseWorld, camera->forward);
                    if (localRight && localUp && localForward) {
                        const bool active = isActiveHandle(GizmoHandle::ViewRing);
                        const auto color = handleColor(GizmoHandle::ViewRing, kNeutralColor);
                        addBasisTorus(
                            vertices,
                            *localRight,
                            *localUp,
                            *localForward,
                            kViewRingRadius,
                            active ? kRotateActiveTubeRadius : kViewRingTubeRadius,
                            color);
                        if (active) {
                            addBasisRotationFeedback(
                                vertices,
                                *localRight,
                                *localUp,
                                gizmoInteractionFeedback_,
                                highlightedColor(color));
                        }
                    }
                }
            }
            break;
    }

    return vertices;
}

vortex::TransformMatrix VulkanViewport::gizmoWorldMatrix(
    const vortex::TransformMatrix& objectWorldMatrix,
    const vortex::Quaternion& worldRotation) const noexcept {
    const auto& source = objectWorldMatrix.values;
    const auto frameRotation = visualGizmoRotation(worldRotation);
    const vortex::TransformMatrix rotationOnly = vortex::rotationTransformMatrix(
        frameRotation.value_or(worldRotation));
    const vortex::Vec3 xAxis{rotationOnly.values[0], rotationOnly.values[1], rotationOnly.values[2]};
    const vortex::Vec3 yAxis{rotationOnly.values[4], rotationOnly.values[5], rotationOnly.values[6]};
    const vortex::Vec3 zAxis{rotationOnly.values[8], rotationOnly.values[9], rotationOnly.values[10]};
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

    const CameraPushConstants cameraPush = cameraPushConstants(width / height);
    const auto pointer = gizmoPointerRay(xPixels, yPixels);
    const auto camera = gizmoCameraFrame();
    if (!pointer || !camera) {
        return std::nullopt;
    }

    const vortex::TransformMatrix visualMatrix = gizmoWorldMatrix(draw->worldMatrix, draw->worldRotation);
    const vortex::Vec3 visualOriginWorld = vortex::transformPoint(visualMatrix, {});
    ScreenPoint visualOriginScreen{};
    if (!projectWorldPoint(cameraPush, swapchainExtent_, visualOriginWorld, visualOriginScreen)) {
        return std::nullopt;
    }

    const auto frameRotation = resolvedGizmoRotation(
        gizmoOrientation_, draw->worldRotation, camera);
    if (!frameRotation) {
        return std::nullopt;
    }
    const GizmoFrame frame{visualOriginWorld, *frameRotation};
    const ScreenPoint touch{xPixels, yPixels};
    const TransformToolMode toolMode = mode == GizmoMode::Move ? TransformToolMode::Move :
                                       mode == GizmoMode::Rotate ? TransformToolMode::Rotate :
                                                                   TransformToolMode::Scale;

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
    const float planeTouchRadiusPixels = std::clamp(
        kPlaneTouchRadiusDp * displayDensity_, 30.0F, 52.0F);
    const float centerTouchRadiusPixels = std::clamp(
        kCenterTouchRadiusDp * displayDensity_, 34.0F, 56.0F);
    const float viewRingTouchRadiusPixels = std::clamp(
        kViewRingTouchRadiusDp * displayDensity_, 30.0F, 46.0F);
    const float moveCenterDeadZonePixels = std::clamp(
        kMoveCenterDeadZoneDp * displayDensity_,
        kAxisShaftStart * kTargetPixelsPerLocalUnit,
        kMoveCenterDeadZoneMaxPixels);

    float bestScore = std::numeric_limits<float>::max();
    std::optional<GizmoHit> best;
    const auto descriptorPriority = [&](const GizmoHandle handle) noexcept {
        for (const GizmoHandleDescriptor& descriptor : gizmoHandleDescriptors(toolMode)) {
            if (descriptor.handle == handle) return descriptor.pickPriority;
        }
        return 1.0F;
    };
    const auto consider = [&](const GizmoHandle handle,
                              const float distance,
                              const float radius,
                              const float conditioning) {
        if (!std::isfinite(distance) || !std::isfinite(conditioning) || radius <= 0.0F ||
            distance > radius) {
            return;
        }
        // Distance still dominates. Conditioning and descriptor priority only resolve dense
        // overlaps, so an edge-on/ambiguous handle does not steal a touch from a clear one.
        const float quality = std::max(0.12F, conditioning) * descriptorPriority(handle);
        const float score = (distance / radius) / quality;
        if (score < bestScore) {
            bestScore = score;
            best = GizmoHit{handle, score};
        }
    };

    constexpr std::array<GizmoAxis, 3> axes{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z};
    if (mode == GizmoMode::Move || mode == GizmoMode::Scale) {
        for (const GizmoAxis axis : axes) {
            const vortex::Vec3 localAxis = axisVector(axis);
            const float conditioning = axisConstraintConditioning(frame, axis, *pointer);
            if (mode == GizmoMode::Move) {
                ScreenPoint endpoint{};
                ScreenPoint hitStart{};
                const auto endpointWorld = vortex::transformPoint(visualMatrix, scale(localAxis, kMoveTip));
                const auto startWorld = vortex::transformPoint(visualMatrix, scale(localAxis, kAxisShaftStart));
                if (!projectWorldPoint(cameraPush, swapchainExtent_, endpointWorld, endpoint) ||
                    !projectWorldPoint(cameraPush, swapchainExtent_, startWorld, hitStart)) {
                    continue;
                }
                if (pointDistance(touch, visualOriginScreen) >= moveCenterDeadZonePixels) {
                    consider(
                        axisGizmoHandle(axis),
                        pointSegmentDistance(touch, hitStart, endpoint),
                        moveTouchRadiusPixels,
                        conditioning);
                }
            } else {
                ScreenPoint handle{};
                const auto handleWorld = vortex::transformPoint(
                    visualMatrix, scale(localAxis, kScaleHandleCenter));
                if (!projectWorldPoint(cameraPush, swapchainExtent_, handleWorld, handle)) {
                    continue;
                }
                consider(
                    axisGizmoHandle(axis),
                    pointDistance(touch, handle),
                    scaleTouchRadiusPixels,
                    conditioning);
            }
        }

        constexpr std::array<GizmoPlane, 3> planes{GizmoPlane::XY, GizmoPlane::XZ, GizmoPlane::YZ};
        for (const GizmoPlane plane : planes) {
            const GizmoHandle handle = plane == GizmoPlane::XY ? GizmoHandle::PlaneXY :
                                       plane == GizmoPlane::XZ ? GizmoHandle::PlaneXZ :
                                                               GizmoHandle::PlaneYZ;
            const vortex::Vec3 centerLocal = planePointLocal(plane, kPlaneHandleOffset, kPlaneHandleOffset);
            ScreenPoint center{};
            if (!projectWorldPoint(
                    cameraPush,
                    swapchainExtent_,
                    vortex::transformPoint(visualMatrix, centerLocal),
                    center)) {
                continue;
            }
            const float conditioning = planeConstraintConditioning(frame, plane, *pointer, *camera);
            consider(handle, pointDistance(touch, center), planeTouchRadiusPixels, conditioning);
        }

        if (mode == GizmoMode::Move) {
            consider(
                GizmoHandle::Center,
                pointDistance(touch, visualOriginScreen),
                centerTouchRadiusPixels,
                1.0F);
        } else {
            const auto inverseFrame = vortex::conjugateQuaternion(*frameRotation);
            if (inverseFrame) {
                const auto localRight = vortex::rotateVectorByQuaternion(*inverseFrame, camera->right);
                const auto localUp = vortex::rotateVectorByQuaternion(*inverseFrame, camera->up);
                if (localRight && localUp) {
                    float distance = std::numeric_limits<float>::max();
                    for (std::size_t segment = 0U; segment < kRotateRingSegments; ++segment) {
                        const float angleA = (2.0F * kPi * static_cast<float>(segment)) /
                                             static_cast<float>(kRotateRingSegments);
                        const float angleB = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                                             static_cast<float>(kRotateRingSegments);
                        ScreenPoint a{};
                        ScreenPoint b{};
                        if (!projectWorldPoint(
                                cameraPush,
                                swapchainExtent_,
                                vortex::transformPoint(
                                    visualMatrix,
                                    basisRingPoint(*localRight, *localUp, angleA, kUniformScaleRingRadius)),
                                a) ||
                            !projectWorldPoint(
                                cameraPush,
                                swapchainExtent_,
                                vortex::transformPoint(
                                    visualMatrix,
                                    basisRingPoint(*localRight, *localUp, angleB, kUniformScaleRingRadius)),
                                b)) {
                            continue;
                        }
                        distance = std::min(distance, pointSegmentDistance(touch, a, b));
                    }
                    consider(
                        GizmoHandle::UniformScale,
                        distance,
                        scaleTouchRadiusPixels,
                        1.0F);
                }
            }
        }
        return best;
    }

    // Axis rotation rings.
    for (const GizmoAxis axis : axes) {
        const auto worldAxis = gizmoAxisDirection(frame, axis);
        if (!worldAxis) continue;
        const float ringConditioning = std::clamp(
            std::abs(worldAxis->x * pointer->direction.x +
                     worldAxis->y * pointer->direction.y +
                     worldAxis->z * pointer->direction.z),
            0.0F,
            1.0F);
        float distance = std::numeric_limits<float>::max();
        for (std::size_t segment = 0U; segment < kRotateRingSegments; ++segment) {
            const float angleA = (2.0F * kPi * static_cast<float>(segment)) /
                                 static_cast<float>(kRotateRingSegments);
            const float angleB = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                                 static_cast<float>(kRotateRingSegments);
            ScreenPoint a{};
            ScreenPoint b{};
            if (!projectWorldPoint(
                    cameraPush,
                    swapchainExtent_,
                    vortex::transformPoint(visualMatrix, ringPoint(axis, angleA, kRotateRingRadius)),
                    a) ||
                !projectWorldPoint(
                    cameraPush,
                    swapchainExtent_,
                    vortex::transformPoint(visualMatrix, ringPoint(axis, angleB, kRotateRingRadius)),
                    b)) {
                continue;
            }
            distance = std::min(distance, pointSegmentDistance(touch, a, b));
        }
        consider(axisGizmoHandle(axis), distance, rotateTouchRadiusPixels, ringConditioning);
    }

    // Camera-facing outer view ring.
    const auto inverseWorld = vortex::conjugateQuaternion(*frameRotation);
    if (inverseWorld) {
        const auto localRight = vortex::rotateVectorByQuaternion(*inverseWorld, camera->right);
        const auto localUp = vortex::rotateVectorByQuaternion(*inverseWorld, camera->up);
        if (localRight && localUp) {
            float distance = std::numeric_limits<float>::max();
            for (std::size_t segment = 0U; segment < kRotateRingSegments; ++segment) {
                const float angleA = (2.0F * kPi * static_cast<float>(segment)) /
                                     static_cast<float>(kRotateRingSegments);
                const float angleB = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                                     static_cast<float>(kRotateRingSegments);
                ScreenPoint a{};
                ScreenPoint b{};
                if (!projectWorldPoint(
                        cameraPush,
                        swapchainExtent_,
                        vortex::transformPoint(
                            visualMatrix,
                            basisRingPoint(*localRight, *localUp, angleA, kViewRingRadius)),
                        a) ||
                    !projectWorldPoint(
                        cameraPush,
                        swapchainExtent_,
                        vortex::transformPoint(
                            visualMatrix,
                            basisRingPoint(*localRight, *localUp, angleB, kViewRingRadius)),
                        b)) {
                    continue;
                }
                distance = std::min(distance, pointSegmentDistance(touch, a, b));
            }
            consider(GizmoHandle::ViewRing, distance, viewRingTouchRadiusPixels, 1.0F);
        }
    }

    return best;
}

} // namespace vortex::android
