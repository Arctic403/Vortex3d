#pragma once

#include <cstdint>
#include <optional>

namespace vortex::android {

// Editor tool selection. This is host/session state, not persistent document state.
enum class TransformToolMode : std::uint8_t {
    Move = 0U,
    Rotate = 1U,
    Scale = 2U,
};

// Transform basis selection. Local is the current behavior; Global and View are reserved
// here so future Blender-style orientation work extends this contract instead of replacing it.
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

enum class GizmoMode : std::uint8_t {
    Move = 0U,
    Rotate = 1U,
    Scale = 2U,
};

// A handle identifies the interaction surface independently from the active transform mode.
// Axis handles are the only interactive handles today. Plane/center/view handles are reserved
// now so rendering, hit testing and host interaction can grow without another identity rewrite.
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
    AxisRotation = 1U,
    AxisScale = 2U,
    PlaneTranslation = 3U,
    PlaneScale = 4U,
    UniformScale = 5U,
    ViewRotation = 6U,
};

struct GizmoConstraint final {
    TransformToolMode mode = TransformToolMode::Move;
    GizmoHandle handle = GizmoHandle::AxisX;
    TransformOrientation orientation = TransformOrientation::Local;
};

struct AxisConstraintSample final {
    float parameter = 0.0F;
};

struct RotationConstraintSample final {
    float deltaRadians = 0.0F;
    float previousRingRadians = 0.0F;
    float currentRingRadians = 0.0F;
};

struct GizmoInteractionFeedback final {
    bool active = false;
    GizmoMode mode = GizmoMode::Move;
    GizmoHandle handle = GizmoHandle::AxisX;
    float rotationRadians = 0.0F;
    float rotationStartRingRadians = 0.0F;
    float rotationCurrentRingRadians = 0.0F;
    bool hasRotationReference = false;
};

[[nodiscard]] constexpr std::optional<GizmoConstraintKind> gizmoConstraintKind(
    const GizmoConstraint& constraint) noexcept {
    switch (constraint.handle) {
        case GizmoHandle::AxisX:
        case GizmoHandle::AxisY:
        case GizmoHandle::AxisZ:
            switch (constraint.mode) {
                case TransformToolMode::Move:
                    return GizmoConstraintKind::AxisTranslation;
                case TransformToolMode::Rotate:
                    return GizmoConstraintKind::AxisRotation;
                case TransformToolMode::Scale:
                    return GizmoConstraintKind::AxisScale;
            }
            break;
        case GizmoHandle::PlaneXY:
        case GizmoHandle::PlaneXZ:
        case GizmoHandle::PlaneYZ:
            if (constraint.mode == TransformToolMode::Move) {
                return GizmoConstraintKind::PlaneTranslation;
            }
            if (constraint.mode == TransformToolMode::Scale) {
                return GizmoConstraintKind::PlaneScale;
            }
            break;
        case GizmoHandle::UniformScale:
            if (constraint.mode == TransformToolMode::Scale) {
                return GizmoConstraintKind::UniformScale;
            }
            break;
        case GizmoHandle::ViewRing:
            if (constraint.mode == TransformToolMode::Rotate) {
                return GizmoConstraintKind::ViewRotation;
            }
            break;
        case GizmoHandle::Center:
            break;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr GizmoMode gizmoMode(const TransformToolMode mode) noexcept {
    switch (mode) {
        case TransformToolMode::Move:
            return GizmoMode::Move;
        case TransformToolMode::Rotate:
            return GizmoMode::Rotate;
        case TransformToolMode::Scale:
            return GizmoMode::Scale;
    }
    return GizmoMode::Move;
}

[[nodiscard]] constexpr GizmoHandle axisGizmoHandle(const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X:
            return GizmoHandle::AxisX;
        case GizmoAxis::Y:
            return GizmoHandle::AxisY;
        case GizmoAxis::Z:
            return GizmoHandle::AxisZ;
    }
    return GizmoHandle::AxisX;
}

[[nodiscard]] constexpr std::optional<GizmoAxis> gizmoAxis(const GizmoHandle handle) noexcept {
    switch (handle) {
        case GizmoHandle::AxisX:
            return GizmoAxis::X;
        case GizmoHandle::AxisY:
            return GizmoAxis::Y;
        case GizmoHandle::AxisZ:
            return GizmoAxis::Z;
        case GizmoHandle::PlaneXY:
        case GizmoHandle::PlaneXZ:
        case GizmoHandle::PlaneYZ:
        case GizmoHandle::Center:
        case GizmoHandle::ViewRing:
        case GizmoHandle::UniformScale:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace vortex::android
