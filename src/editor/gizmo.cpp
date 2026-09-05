#include "vortex/editor/gizmo.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace vortex {
namespace {

constexpr float kEpsilon = 1.0e-6F;
constexpr float kParallelEpsilon = 1.0e-4F;

constexpr std::array<GizmoHandleDescriptor, 7> kMoveHandles{{
    {TransformToolMode::Move, GizmoHandle::AxisX, GizmoConstraintKind::AxisTranslation, GizmoAxis::X, std::nullopt, 1.0F},
    {TransformToolMode::Move, GizmoHandle::AxisY, GizmoConstraintKind::AxisTranslation, GizmoAxis::Y, std::nullopt, 1.0F},
    {TransformToolMode::Move, GizmoHandle::AxisZ, GizmoConstraintKind::AxisTranslation, GizmoAxis::Z, std::nullopt, 1.0F},
    {TransformToolMode::Move, GizmoHandle::PlaneXY, GizmoConstraintKind::PlaneTranslation, std::nullopt, GizmoPlane::XY, 1.15F},
    {TransformToolMode::Move, GizmoHandle::PlaneXZ, GizmoConstraintKind::PlaneTranslation, std::nullopt, GizmoPlane::XZ, 1.15F},
    {TransformToolMode::Move, GizmoHandle::PlaneYZ, GizmoConstraintKind::PlaneTranslation, std::nullopt, GizmoPlane::YZ, 1.15F},
    {TransformToolMode::Move, GizmoHandle::Center, GizmoConstraintKind::ViewPlaneTranslation, std::nullopt, GizmoPlane::View, 1.35F},
}};

constexpr std::array<GizmoHandleDescriptor, 4> kRotateHandles{{
    {TransformToolMode::Rotate, GizmoHandle::AxisX, GizmoConstraintKind::AxisRotation, GizmoAxis::X, std::nullopt, 1.0F},
    {TransformToolMode::Rotate, GizmoHandle::AxisY, GizmoConstraintKind::AxisRotation, GizmoAxis::Y, std::nullopt, 1.0F},
    {TransformToolMode::Rotate, GizmoHandle::AxisZ, GizmoConstraintKind::AxisRotation, GizmoAxis::Z, std::nullopt, 1.0F},
    {TransformToolMode::Rotate, GizmoHandle::ViewRing, GizmoConstraintKind::ViewRotation, std::nullopt, std::nullopt, 1.2F},
}};

constexpr std::array<GizmoHandleDescriptor, 7> kScaleHandles{{
    {TransformToolMode::Scale, GizmoHandle::AxisX, GizmoConstraintKind::AxisScale, GizmoAxis::X, std::nullopt, 1.0F},
    {TransformToolMode::Scale, GizmoHandle::AxisY, GizmoConstraintKind::AxisScale, GizmoAxis::Y, std::nullopt, 1.0F},
    {TransformToolMode::Scale, GizmoHandle::AxisZ, GizmoConstraintKind::AxisScale, GizmoAxis::Z, std::nullopt, 1.0F},
    {TransformToolMode::Scale, GizmoHandle::PlaneXY, GizmoConstraintKind::PlaneScale, std::nullopt, GizmoPlane::XY, 1.15F},
    {TransformToolMode::Scale, GizmoHandle::PlaneXZ, GizmoConstraintKind::PlaneScale, std::nullopt, GizmoPlane::XZ, 1.15F},
    {TransformToolMode::Scale, GizmoHandle::PlaneYZ, GizmoConstraintKind::PlaneScale, std::nullopt, GizmoPlane::YZ, 1.15F},
    {TransformToolMode::Scale, GizmoHandle::UniformScale, GizmoConstraintKind::UniformScale, std::nullopt, GizmoPlane::View, 1.35F},
}};

[[nodiscard]] Vec3 add(const Vec3 a, const Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 subtract(const Vec3 a, const Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 scale(const Vec3 value, const float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] float dot(const Vec3 a, const Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 cross(const Vec3 a, const Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] std::optional<Vec3> normalized(const Vec3 value) noexcept {
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon) {
        return std::nullopt;
    }
    return scale(value, 1.0F / std::sqrt(lengthSquared));
}

[[nodiscard]] constexpr Vec3 localAxis(const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X: return {1.0F, 0.0F, 0.0F};
        case GizmoAxis::Y: return {0.0F, 1.0F, 0.0F};
        case GizmoAxis::Z: return {0.0F, 0.0F, 1.0F};
    }
    return {1.0F, 0.0F, 0.0F};
}

[[nodiscard]] constexpr Vec3 localBasisA(const GizmoAxis axis) noexcept {
    // X rotates in the YZ plane, while Y and Z can share +X as a stable in-plane
    // phase reference. Expressing the shared case directly avoids duplicated switch branches.
    return axis == GizmoAxis::X ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
}

struct PlaneBasis final {
    Vec3 u{};
    Vec3 v{};
    Vec3 normal{};
};

[[nodiscard]] std::optional<PlaneBasis> planeBasis(
    const GizmoFrame& frame,
    const GizmoPlane plane,
    const GizmoCameraFrame& camera) noexcept {
    if (plane == GizmoPlane::View) {
        const auto u = normalized(camera.right);
        const auto v = normalized(camera.up);
        const auto n = normalized(camera.forward);
        if (!u || !v || !n) return std::nullopt;
        return PlaneBasis{*u, *v, *n};
    }

    GizmoAxis uAxis = GizmoAxis::X;
    GizmoAxis vAxis = GizmoAxis::Y;
    switch (plane) {
        case GizmoPlane::XY: uAxis = GizmoAxis::X; vAxis = GizmoAxis::Y; break;
        case GizmoPlane::XZ: uAxis = GizmoAxis::X; vAxis = GizmoAxis::Z; break;
        case GizmoPlane::YZ: uAxis = GizmoAxis::Y; vAxis = GizmoAxis::Z; break;
        case GizmoPlane::View: break;
    }
    const auto u = gizmoAxisDirection(frame, uAxis);
    const auto v = gizmoAxisDirection(frame, vAxis);
    if (!u || !v) return std::nullopt;
    const auto n = normalized(cross(*u, *v));
    if (!n) return std::nullopt;
    return PlaneBasis{*u, *v, *n};
}

[[nodiscard]] std::optional<Vec3> intersectPlane(
    const PointerRay& ray,
    const Vec3 planePoint,
    const Vec3 planeNormal) noexcept {
    const auto direction = normalized(ray.direction);
    const auto normal = normalized(planeNormal);
    if (!direction || !normal) return std::nullopt;
    const float denominator = dot(*normal, *direction);
    if (!std::isfinite(denominator) || std::abs(denominator) <= kParallelEpsilon) {
        return std::nullopt;
    }
    const float distance = dot(*normal, subtract(planePoint, ray.origin)) / denominator;
    if (!std::isfinite(distance) || distance < 0.0F) return std::nullopt;
    return add(ray.origin, scale(*direction, distance));
}

} // namespace

std::optional<GizmoConstraintKind> gizmoConstraintKind(
    const GizmoConstraint& constraint) noexcept {
    for (const GizmoHandleDescriptor& descriptor : gizmoHandleDescriptors(constraint.mode)) {
        if (descriptor.handle == constraint.handle) return descriptor.kind;
    }
    return std::nullopt;
}

std::span<const GizmoHandleDescriptor> gizmoHandleDescriptors(
    const TransformToolMode mode) noexcept {
    switch (mode) {
        case TransformToolMode::Move: return kMoveHandles;
        case TransformToolMode::Rotate: return kRotateHandles;
        case TransformToolMode::Scale: return kScaleHandles;
    }
    return {};
}

std::optional<Vec3> gizmoAxisDirection(
    const GizmoFrame& frame,
    const GizmoAxis axis) noexcept {
    return rotateVectorByQuaternion(frame.orientationWorld, localAxis(axis));
}

float axisConstraintConditioning(
    const GizmoFrame& frame,
    const GizmoAxis axis,
    const PointerRay& ray) noexcept {
    const auto direction = normalized(ray.direction);
    const auto axisDirection = gizmoAxisDirection(frame, axis);
    if (!direction || !axisDirection) return 0.0F;
    const float parallel = std::clamp(std::abs(dot(*direction, *axisDirection)), 0.0F, 1.0F);
    return std::sqrt(std::max(0.0F, 1.0F - parallel * parallel));
}

float planeConstraintConditioning(
    const GizmoFrame& frame,
    const GizmoPlane plane,
    const PointerRay& ray,
    const GizmoCameraFrame& camera) noexcept {
    const auto basis = planeBasis(frame, plane, camera);
    const auto direction = normalized(ray.direction);
    if (!basis || !direction) return 0.0F;
    return std::abs(dot(basis->normal, *direction));
}

std::optional<AxisConstraintSample> sampleAxisConstraint(
    const GizmoFrame& frame,
    const GizmoAxis axis,
    const PointerRay& ray) noexcept {
    const auto axisDirection = gizmoAxisDirection(frame, axis);
    const auto rayDirection = normalized(ray.direction);
    if (!axisDirection || !rayDirection) return std::nullopt;

    const float b = dot(*axisDirection, *rayDirection);
    const Vec3 originDelta = subtract(frame.pivotWorld, ray.origin);
    const float d = dot(*axisDirection, originDelta);
    const float e = dot(*rayDirection, originDelta);
    const float denominator = 1.0F - b * b;
    if (!std::isfinite(denominator) || denominator <= kParallelEpsilon) {
        return std::nullopt;
    }

    const float axisParameter = (b * e - d) / denominator;
    const float rayParameter = (e - b * d) / denominator;
    if (!std::isfinite(axisParameter) || !std::isfinite(rayParameter) || rayParameter < 0.0F) {
        return std::nullopt;
    }
    return AxisConstraintSample{axisParameter};
}

std::optional<PlaneConstraintSample> samplePlaneConstraint(
    const GizmoFrame& frame,
    const GizmoPlane plane,
    const PointerRay& ray,
    const GizmoCameraFrame& camera) noexcept {
    const auto basis = planeBasis(frame, plane, camera);
    if (!basis) return std::nullopt;
    const auto point = intersectPlane(ray, frame.pivotWorld, basis->normal);
    if (!point) return std::nullopt;
    const Vec3 relative = subtract(*point, frame.pivotWorld);
    return PlaneConstraintSample{*point, dot(relative, basis->u), dot(relative, basis->v)};
}

std::optional<RotationConstraintSample> sampleRotationConstraint(
    const GizmoFrame& frame,
    const GizmoHandle handle,
    const PointerRay& ray,
    const GizmoCameraFrame& camera) noexcept {
    Vec3 normal{};
    Vec3 basisA{};
    Vec3 basisB{};

    if (handle == GizmoHandle::ViewRing) {
        const auto n = normalized(camera.forward);
        const auto a = normalized(camera.right);
        const auto b = normalized(camera.up);
        if (!n || !a || !b) return std::nullopt;
        normal = *n;
        basisA = *a;
        basisB = *b;
    } else {
        const auto axis = gizmoAxis(handle);
        if (!axis) return std::nullopt;
        const auto n = gizmoAxisDirection(frame, *axis);
        const auto a = rotateVectorByQuaternion(frame.orientationWorld, localBasisA(*axis));
        if (!n || !a) return std::nullopt;
        const auto normalizedA = normalized(*a);
        if (!normalizedA) return std::nullopt;
        const auto b = normalized(cross(*n, *normalizedA));
        if (!b) return std::nullopt;
        normal = *n;
        basisA = *normalizedA;
        basisB = *b;
    }

    const auto point = intersectPlane(ray, frame.pivotWorld, normal);
    if (!point) return std::nullopt;
    const auto radial = normalized(subtract(*point, frame.pivotWorld));
    if (!radial) return std::nullopt;

    const float phase = std::atan2(dot(*radial, basisB), dot(*radial, basisA));
    if (!std::isfinite(phase)) return std::nullopt;
    return RotationConstraintSample{phase};
}

float snapConstraintValue(const float value, const float step) noexcept {
    if (!std::isfinite(value)) {
        return value;
    }
    if (!std::isfinite(step) || step <= 0.0F) {
        return value;
    }
    const float snapped = std::round(value / step) * step;
    return std::isfinite(snapped) ? snapped : value;
}

std::optional<ObjectTransform> composeTransformDelta(
    const ObjectTransform& start,
    const TransformDelta& delta,
    const TransformComposeContext& context) noexcept {
    if (!isFiniteObjectTransform(start) ||
        !isFiniteQuaternion(context.parentWorldRotation)) {
        return std::nullopt;
    }

    ObjectTransform result = start;
    const bool ok = std::visit([&](const auto& value) -> bool {
        using Delta = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Delta, TranslateDelta>) {
            const auto& m = context.parentWorldMatrix.values;
            const float a00 = m[0], a01 = m[4], a02 = m[8];
            const float a10 = m[1], a11 = m[5], a12 = m[9];
            const float a20 = m[2], a21 = m[6], a22 = m[10];
            const float c00 = a11*a22 - a12*a21;
            const float c01 = a02*a21 - a01*a22;
            const float c02 = a01*a12 - a02*a11;
            const float c10 = a12*a20 - a10*a22;
            const float c11 = a00*a22 - a02*a20;
            const float c12 = a02*a10 - a00*a12;
            const float c20 = a10*a21 - a11*a20;
            const float c21 = a01*a20 - a00*a21;
            const float c22 = a00*a11 - a01*a10;
            const float determinant = a00*c00 + a01*c10 + a02*c20;
            if (!std::isfinite(determinant) || std::abs(determinant) <= kEpsilon) {
                return false;
            }
            const float inv = 1.0F / determinant;
            const Vec3 parentDelta{
                (c00*value.worldDelta.x + c01*value.worldDelta.y + c02*value.worldDelta.z) * inv,
                (c10*value.worldDelta.x + c11*value.worldDelta.y + c12*value.worldDelta.z) * inv,
                (c20*value.worldDelta.x + c21*value.worldDelta.y + c22*value.worldDelta.z) * inv,
            };
            result.translation = add(start.translation, parentDelta);
            return true;
        } else if constexpr (std::is_same_v<Delta, RotateDelta>) {
            const auto normalizedDelta = normalizedQuaternion(value.delta);
            if (!normalizedDelta) return false;
            if (value.space == RotationComposeSpace::Local) {
                const auto composed = multiplyQuaternions(start.rotation, *normalizedDelta);
                if (!composed) return false;
                result.rotation = *composed;
                return true;
            }
            const auto startWorld = multiplyQuaternions(context.parentWorldRotation, start.rotation);
            const auto parentInverse = conjugateQuaternion(context.parentWorldRotation);
            if (!startWorld || !parentInverse) return false;
            const auto newWorld = multiplyQuaternions(*normalizedDelta, *startWorld);
            if (!newWorld) return false;
            const auto newLocal = multiplyQuaternions(*parentInverse, *newWorld);
            if (!newLocal) return false;
            result.rotation = *newLocal;
            return true;
        } else if constexpr (std::is_same_v<Delta, ScaleDelta>) {
            if (!std::isfinite(value.factor.x) || !std::isfinite(value.factor.y) ||
                !std::isfinite(value.factor.z)) {
                return false;
            }
            result.scale = {
                start.scale.x * value.factor.x,
                start.scale.y * value.factor.y,
                start.scale.z * value.factor.z,
            };
            return true;
        }
        return false;
    }, delta);

    if (!ok || !isFiniteObjectTransform(result)) {
        return std::nullopt;
    }
    return result;
}

} // namespace vortex
