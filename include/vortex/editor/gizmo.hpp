#pragma once

#include "vortex/core/transform.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace vortex {

enum class TransformToolMode : std::uint8_t {
    Move = 0U,
    Rotate = 1U,
    Scale = 2U,
};

enum class TransformOrientation : std::uint8_t {
    Global = 0U,
    Local = 1U,
    View = 2U,
};

enum class GizmoAxis : std::uint8_t {
    X = 0U,
    Y = 1U,
    Z = 2U,
};

enum class GizmoPlane : std::uint8_t {
    XY = 0U,
    XZ = 1U,
    YZ = 2U,
    View = 3U,
};

enum class GizmoMode : std::uint8_t {
    Move = 0U,
    Rotate = 1U,
    Scale = 2U,
};

enum class GizmoHandle : std::uint8_t {
    AxisX = 0U,
    AxisY = 1U,
    AxisZ = 2U,
    PlaneXY = 3U,
    PlaneXZ = 4U,
    PlaneYZ = 5U,
    Center = 6U,
    ViewRing = 7U,
    UniformScale = 8U,
};

enum class GizmoVisualState : std::uint8_t {
    Normal = 0U,
    Hovered = 1U,
    Active = 2U,
    Disabled = 3U,
};

enum class GizmoConstraintKind : std::uint8_t {
    AxisTranslation = 0U,
    PlaneTranslation = 1U,
    ViewPlaneTranslation = 2U,
    AxisRotation = 3U,
    ViewRotation = 4U,
    AxisScale = 5U,
    PlaneScale = 6U,
    UniformScale = 7U,
};

struct GizmoConstraint final {
    TransformToolMode mode = TransformToolMode::Move;
    GizmoHandle handle = GizmoHandle::AxisX;
    TransformOrientation orientation = TransformOrientation::Local;
};

struct GizmoHandleDescriptor final {
    TransformToolMode mode = TransformToolMode::Move;
    GizmoHandle handle = GizmoHandle::AxisX;
    GizmoConstraintKind kind = GizmoConstraintKind::AxisTranslation;
    std::optional<GizmoAxis> axis;
    std::optional<GizmoPlane> plane;
    float pickPriority = 1.0F;
};

struct PointerRay final {
    Vec3 origin{};
    Vec3 direction{0.0F, 0.0F, -1.0F};
};

struct GizmoCameraFrame final {
    Vec3 forward{0.0F, 0.0F, -1.0F};
    Vec3 right{1.0F, 0.0F, 0.0F};
    Vec3 up{0.0F, 1.0F, 0.0F};
};

struct GizmoFrame final {
    Vec3 pivotWorld{};
    Quaternion orientationWorld{};
};

struct AxisConstraintSample final {
    float parameter = 0.0F;
};

struct PlaneConstraintSample final {
    Vec3 pointWorld{};
    float u = 0.0F;
    float v = 0.0F;
};

struct RotationConstraintSample final {
    float phaseRadians = 0.0F;
};

struct TranslateDelta final {
    Vec3 worldDelta{};
};

enum class RotationComposeSpace : std::uint8_t {
    Local = 0U,
    World = 1U,
};

struct RotateDelta final {
    Quaternion delta{};
    RotationComposeSpace space = RotationComposeSpace::Local;
};

struct ScaleDelta final {
    Vec3 factor{1.0F, 1.0F, 1.0F};
};

using TransformDelta = std::variant<TranslateDelta, RotateDelta, ScaleDelta>;

struct TransformComposeContext final {
    TransformMatrix parentWorldMatrix{};
    Quaternion parentWorldRotation{};
};

// Zero/negative steps disable snapping. These helpers operate on constraint parameters
// before transform composition, so snapping stays independent from rendering and parent space.
struct GizmoSnapSettings final {
    float translationStep = 0.0F;
    float rotationStepRadians = 0.0F;
    float scaleStep = 0.0F;
};

[[nodiscard]] constexpr GizmoMode gizmoMode(TransformToolMode mode) noexcept {
    switch (mode) {
        case TransformToolMode::Move: return GizmoMode::Move;
        case TransformToolMode::Rotate: return GizmoMode::Rotate;
        case TransformToolMode::Scale: return GizmoMode::Scale;
    }
    return GizmoMode::Move;
}

[[nodiscard]] constexpr GizmoHandle axisGizmoHandle(GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X: return GizmoHandle::AxisX;
        case GizmoAxis::Y: return GizmoHandle::AxisY;
        case GizmoAxis::Z: return GizmoHandle::AxisZ;
    }
    return GizmoHandle::AxisX;
}

[[nodiscard]] constexpr std::optional<GizmoAxis> gizmoAxis(GizmoHandle handle) noexcept {
    switch (handle) {
        case GizmoHandle::AxisX: return GizmoAxis::X;
        case GizmoHandle::AxisY: return GizmoAxis::Y;
        case GizmoHandle::AxisZ: return GizmoAxis::Z;
        default: return std::nullopt;
    }
}

[[nodiscard]] constexpr std::optional<GizmoPlane> gizmoPlane(GizmoHandle handle) noexcept {
    switch (handle) {
        case GizmoHandle::PlaneXY: return GizmoPlane::XY;
        case GizmoHandle::PlaneXZ: return GizmoPlane::XZ;
        case GizmoHandle::PlaneYZ: return GizmoPlane::YZ;
        case GizmoHandle::Center:
        case GizmoHandle::UniformScale:
            return GizmoPlane::View;
        default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<GizmoConstraintKind> gizmoConstraintKind(
    const GizmoConstraint& constraint) noexcept;
[[nodiscard]] std::span<const GizmoHandleDescriptor> gizmoHandleDescriptors(
    TransformToolMode mode) noexcept;
[[nodiscard]] std::optional<Vec3> gizmoAxisDirection(
    const GizmoFrame& frame,
    GizmoAxis axis) noexcept;
[[nodiscard]] float axisConstraintConditioning(
    const GizmoFrame& frame,
    GizmoAxis axis,
    const PointerRay& ray) noexcept;
[[nodiscard]] float planeConstraintConditioning(
    const GizmoFrame& frame,
    GizmoPlane plane,
    const PointerRay& ray,
    const GizmoCameraFrame& camera) noexcept;
[[nodiscard]] std::optional<AxisConstraintSample> sampleAxisConstraint(
    const GizmoFrame& frame,
    GizmoAxis axis,
    const PointerRay& ray) noexcept;
[[nodiscard]] std::optional<PlaneConstraintSample> samplePlaneConstraint(
    const GizmoFrame& frame,
    GizmoPlane plane,
    const PointerRay& ray,
    const GizmoCameraFrame& camera) noexcept;
[[nodiscard]] std::optional<RotationConstraintSample> sampleRotationConstraint(
    const GizmoFrame& frame,
    GizmoHandle handle,
    const PointerRay& ray,
    const GizmoCameraFrame& camera) noexcept;
[[nodiscard]] float snapConstraintValue(float value, float step) noexcept;
[[nodiscard]] std::optional<ObjectTransform> composeTransformDelta(
    const ObjectTransform& start,
    const TransformDelta& delta,
    const TransformComposeContext& context) noexcept;

} // namespace vortex
