#include "viewport_host.hpp"

#include "vortex/core/document_commands.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace vortex::android {
namespace {

constexpr float kScaleSampleEpsilon = 1.0e-4F;
constexpr float kMaxScaleMagnitude = 20.0F;
constexpr float kScaleZeroHysteresis = 0.02F;
constexpr float kTwoPi = 6.2831853071795864769F;

[[nodiscard]] Vec3 axisVector(const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X: return {1.0F, 0.0F, 0.0F};
        case GizmoAxis::Y: return {0.0F, 1.0F, 0.0F};
        case GizmoAxis::Z: return {0.0F, 0.0F, 1.0F};
    }
    return {1.0F, 0.0F, 0.0F};
}

void setComponent(Vec3& value, const GizmoAxis axis, const float componentValue) noexcept {
    switch (axis) {
        case GizmoAxis::X: value.x = componentValue; return;
        case GizmoAxis::Y: value.y = componentValue; return;
        case GizmoAxis::Z: value.z = componentValue; return;
    }
}

[[nodiscard]] Vec3 subtract(const Vec3 a, const Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 scale(const Vec3 value, const float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] float dot2(
    const float ax,
    const float ay,
    const float bx,
    const float by) noexcept {
    return ax * bx + ay * by;
}

[[nodiscard]] std::optional<std::pair<GizmoAxis, GizmoAxis>> planeAxes(
    const GizmoPlane plane) noexcept {
    switch (plane) {
        case GizmoPlane::XY: return std::pair{GizmoAxis::X, GizmoAxis::Y};
        case GizmoPlane::XZ: return std::pair{GizmoAxis::X, GizmoAxis::Z};
        case GizmoPlane::YZ: return std::pair{GizmoAxis::Y, GizmoAxis::Z};
        case GizmoPlane::View: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] float stabilizedScaleFactor(const float factor) noexcept {
    if (!std::isfinite(factor)) {
        return factor;
    }
    const float clamped = std::clamp(factor, -kMaxScaleMagnitude, kMaxScaleMagnitude);
    return std::abs(clamped) < kScaleZeroHysteresis ? 0.0F : clamped;
}


} // namespace

std::optional<TransformMatrix> ViewportHost::previewWorldMatrix(
    const ObjectId objectId,
    const ObjectTransform& transform) const {
    const ObjectBlock* object = document_.object(objectId);
    if (object == nullptr || !isFiniteObjectTransform(transform)) {
        return std::nullopt;
    }

    const TransformMatrix local = objectTransformMatrix(transform);
    if (!object->parentId) {
        return local;
    }
    const auto parentWorld = document_.objectWorldMatrix(object->parentId);
    if (!parentWorld) {
        return std::nullopt;
    }
    return multiplyTransformMatrices(*parentWorld, local);
}

std::optional<Quaternion> ViewportHost::previewWorldRotation(
    const ObjectId objectId,
    const ObjectTransform& transform) const {
    const ObjectBlock* object = document_.object(objectId);
    if (object == nullptr || !isFiniteObjectTransform(transform)) {
        return std::nullopt;
    }
    if (!object->parentId) {
        return normalizedQuaternion(transform.rotation);
    }
    const auto parentRotation = document_.objectWorldRotation(object->parentId);
    if (!parentRotation) {
        return std::nullopt;
    }
    return multiplyQuaternions(*parentRotation, transform.rotation);
}

bool ViewportHost::beginTransformGesture(
    const TransformToolMode mode,
    const float xPixels,
    const float yPixels) {
    if (!initialized_ || transformDrag_.active ||
        !std::isfinite(xPixels) || !std::isfinite(yPixels)) {
        return false;
    }

    const ObjectId objectId = editor_.activeObject();
    const ObjectBlock* object = document_.object(objectId);
    if (!objectId || object == nullptr) {
        return false;
    }

    const auto hit = renderer_.hitTestGizmo(objectId, gizmoMode(mode), xPixels, yPixels);
    const auto ray = renderer_.gizmoPointerRay(xPixels, yPixels);
    const auto camera = renderer_.gizmoCameraFrame();
    if (!hit || !ray || !camera) {
        return false;
    }

    const GizmoConstraint constraint{mode, hit->handle, transformOrientation_};
    const auto kind = gizmoConstraintKind(constraint);
    if (!kind) {
        return false;
    }

    const auto interactionWorld = previewWorldMatrix(objectId, object->transform);
    const auto localWorldRotation = previewWorldRotation(objectId, object->transform);
    if (!interactionWorld || !localWorldRotation) {
        return false;
    }

    Quaternion frameOrientation = *localWorldRotation;
    if (transformOrientation_ == TransformOrientation::Global) {
        frameOrientation = {};
    } else if (transformOrientation_ == TransformOrientation::View) {
        const auto viewOrientation = quaternionFromBasis(camera->right, camera->up, camera->forward);
        if (!viewOrientation) {
            return false;
        }
        frameOrientation = *viewOrientation;
    }

    const GizmoFrame frame{
        transformPoint(*interactionWorld, {}),
        frameOrientation,
    };

    TransformMatrix parentWorld = identityTransformMatrix();
    Quaternion parentWorldRotation{};
    if (object->parentId) {
        const auto parentMatrix = document_.objectWorldMatrix(object->parentId);
        const auto parentRotation = document_.objectWorldRotation(object->parentId);
        if (!parentMatrix || !parentRotation) {
            return false;
        }
        parentWorld = *parentMatrix;
        parentWorldRotation = *parentRotation;
    }

    TransformDragState next{};
    next.active = true;
    next.constraint = constraint;
    next.objectId = objectId;
    next.before = object->transform;
    next.preview = object->transform;
    next.frame = frame;
    next.camera = *camera;
    next.parentWorldMatrix = parentWorld;
    next.parentWorldRotation = parentWorldRotation;

    switch (*kind) {
        case GizmoConstraintKind::AxisTranslation:
        case GizmoConstraintKind::AxisScale: {
            const auto axis = gizmoAxis(hit->handle);
            if (!axis) return false;
            const auto sample = vortex::sampleAxisConstraint(frame, *axis, *ray);
            if (!sample || (*kind == GizmoConstraintKind::AxisScale &&
                            std::abs(sample->parameter) <= kScaleSampleEpsilon)) {
                return false;
            }
            next.startAxisParameter = sample->parameter;
            next.hasAxisSample = true;
            break;
        }
        case GizmoConstraintKind::PlaneTranslation:
        case GizmoConstraintKind::ViewPlaneTranslation:
        case GizmoConstraintKind::PlaneScale:
        case GizmoConstraintKind::UniformScale: {
            const auto plane = gizmoPlane(hit->handle);
            if (!plane) return false;
            const auto sample = vortex::samplePlaneConstraint(frame, *plane, *ray, *camera);
            if (!sample) return false;
            next.startPlaneSample = *sample;
            next.hasPlaneSample = true;
            if (*kind == GizmoConstraintKind::UniformScale) {
                next.startUniformProjection =
                    sample->u * sample->u + sample->v * sample->v;
                if (next.startUniformProjection <= kScaleSampleEpsilon) return false;
            } else if (*kind == GizmoConstraintKind::PlaneScale) {
                if (std::abs(sample->u) <= kScaleSampleEpsilon ||
                    std::abs(sample->v) <= kScaleSampleEpsilon) {
                    return false;
                }
            }
            break;
        }
        case GizmoConstraintKind::AxisRotation:
        case GizmoConstraintKind::ViewRotation: {
            const auto sample = vortex::sampleRotationConstraint(frame, hit->handle, *ray, *camera);
            if (!sample) return false;
            next.rotationStartPhase = sample->phaseRadians;
            next.rotationPreviousPhase = sample->phaseRadians;
            next.hasRotationSample = true;
            break;
        }
    }

    transformDrag_ = next;
    return renderer_.setGizmoInteractionFeedback(GizmoInteractionFeedback{
        true,
        gizmoMode(mode),
        hit->handle,
        0.0F,
        next.rotationStartPhase,
        next.rotationStartPhase,
        next.hasRotationSample,
    });
}

bool ViewportHost::updateTransformGesture(const float xPixels, const float yPixels) {
    if (!initialized_ || !transformDrag_.active ||
        !std::isfinite(xPixels) || !std::isfinite(yPixels)) {
        return false;
    }

    const auto ray = renderer_.gizmoPointerRay(xPixels, yPixels);
    if (!ray) {
        return true;
    }

    const auto kind = gizmoConstraintKind(transformDrag_.constraint);
    if (!kind) {
        return false;
    }

    ObjectTransform preview = transformDrag_.before;
    std::optional<TransformDelta> pendingDelta;
    const TransformComposeContext composeContext{
        transformDrag_.parentWorldMatrix,
        transformDrag_.parentWorldRotation,
    };
    float nextAccumulatedRotation = transformDrag_.accumulatedRotationRadians;
    float nextRotationPhase = transformDrag_.rotationPreviousPhase;
    bool updateRotationContinuity = false;

    switch (*kind) {
        case GizmoConstraintKind::AxisTranslation: {
            const auto axis = gizmoAxis(transformDrag_.constraint.handle);
            if (!axis || !transformDrag_.hasAxisSample) return false;
            const auto current = vortex::sampleAxisConstraint(transformDrag_.frame, *axis, *ray);
            const auto axisWorld = gizmoAxisDirection(transformDrag_.frame, *axis);
            if (!current || !axisWorld) return true;
            const Vec3 worldDelta = scale(
                *axisWorld,
                current->parameter - transformDrag_.startAxisParameter);
            pendingDelta = TranslateDelta{worldDelta};
            break;
        }
        case GizmoConstraintKind::PlaneTranslation:
        case GizmoConstraintKind::ViewPlaneTranslation: {
            const auto plane = gizmoPlane(transformDrag_.constraint.handle);
            if (!plane || !transformDrag_.hasPlaneSample) return false;
            const auto current = vortex::samplePlaneConstraint(
                transformDrag_.frame, *plane, *ray, transformDrag_.camera);
            if (!current) return true;
            const Vec3 worldDelta = subtract(
                current->pointWorld,
                transformDrag_.startPlaneSample.pointWorld);
            pendingDelta = TranslateDelta{worldDelta};
            break;
        }
        case GizmoConstraintKind::AxisRotation:
        case GizmoConstraintKind::ViewRotation: {
            if (!transformDrag_.hasRotationSample) return false;
            const auto current = vortex::sampleRotationConstraint(
                transformDrag_.frame,
                transformDrag_.constraint.handle,
                *ray,
                transformDrag_.camera);
            if (!current) {
                // Edge-on ring samples are genuinely undefined. Hold the last valid state rather
                // than changing rotation semantics; resume when the same constraint is valid again.
                return true;
            }
            const float step = std::remainder(
                current->phaseRadians - transformDrag_.rotationPreviousPhase,
                kTwoPi);
            if (!std::isfinite(step)) return false;
            nextAccumulatedRotation += step;
            nextRotationPhase = current->phaseRadians;
            updateRotationContinuity = true;

            if (*kind == GizmoConstraintKind::AxisRotation &&
                transformDrag_.constraint.orientation == TransformOrientation::Local) {
                const auto axis = gizmoAxis(transformDrag_.constraint.handle);
                if (!axis) return false;
                const auto delta = quaternionFromAxisAngle(axisVector(*axis), nextAccumulatedRotation);
                if (!delta) return false;
                pendingDelta = RotateDelta{*delta, RotationComposeSpace::Local};
            } else {
                Vec3 worldAxis = transformDrag_.camera.forward;
                if (*kind == GizmoConstraintKind::AxisRotation) {
                    const auto axis = gizmoAxis(transformDrag_.constraint.handle);
                    const auto resolved = axis ? gizmoAxisDirection(transformDrag_.frame, *axis) : std::nullopt;
                    if (!resolved) return false;
                    worldAxis = *resolved;
                }
                const auto delta = quaternionFromAxisAngle(worldAxis, nextAccumulatedRotation);
                if (!delta) return false;
                pendingDelta = RotateDelta{*delta, RotationComposeSpace::World};
            }
            break;
        }
        case GizmoConstraintKind::AxisScale: {
            const auto axis = gizmoAxis(transformDrag_.constraint.handle);
            if (!axis || !transformDrag_.hasAxisSample) return false;
            const auto current = vortex::sampleAxisConstraint(transformDrag_.frame, *axis, *ray);
            if (!current) return true;
            const float ratio = current->parameter / transformDrag_.startAxisParameter;
            if (!std::isfinite(ratio)) return false;
            Vec3 factor{1.0F, 1.0F, 1.0F};
            setComponent(factor, *axis, stabilizedScaleFactor(ratio));
            pendingDelta = ScaleDelta{factor};
            break;
        }
        case GizmoConstraintKind::PlaneScale: {
            const auto plane = gizmoPlane(transformDrag_.constraint.handle);
            const auto axes = plane ? planeAxes(*plane) : std::nullopt;
            if (!plane || !axes || !transformDrag_.hasPlaneSample) return false;
            const auto current = vortex::samplePlaneConstraint(
                transformDrag_.frame, *plane, *ray, transformDrag_.camera);
            if (!current) return true;
            const float uFactor = stabilizedScaleFactor(
                current->u / transformDrag_.startPlaneSample.u);
            const float vFactor = stabilizedScaleFactor(
                current->v / transformDrag_.startPlaneSample.v);
            if (!std::isfinite(uFactor) || !std::isfinite(vFactor)) return false;
            Vec3 factor{1.0F, 1.0F, 1.0F};
            setComponent(factor, axes->first, uFactor);
            setComponent(factor, axes->second, vFactor);
            pendingDelta = ScaleDelta{factor};
            break;
        }
        case GizmoConstraintKind::UniformScale: {
            if (!transformDrag_.hasPlaneSample) return false;
            const auto current = vortex::samplePlaneConstraint(
                transformDrag_.frame, GizmoPlane::View, *ray, transformDrag_.camera);
            if (!current) return true;
            const float numerator = dot2(
                current->u,
                current->v,
                transformDrag_.startPlaneSample.u,
                transformDrag_.startPlaneSample.v);
            const float factor = stabilizedScaleFactor(
                numerator / transformDrag_.startUniformProjection);
            if (!std::isfinite(factor)) return false;
            pendingDelta = ScaleDelta{{factor, factor, factor}};
            break;
        }
    }

    if (pendingDelta) {
        const auto composed = composeTransformDelta(
            transformDrag_.before, *pendingDelta, composeContext);
        if (!composed) {
            return false;
        }
        preview = *composed;
    }

    if (!isFiniteObjectTransform(preview)) {
        return false;
    }
    const auto world = previewWorldMatrix(transformDrag_.objectId, preview);
    const auto worldRotation = previewWorldRotation(transformDrag_.objectId, preview);
    if (!world || !worldRotation ||
        !renderer_.updateObjectTransform(transformDrag_.objectId, *world, *worldRotation)) {
        return false;
    }

    if (updateRotationContinuity) {
        transformDrag_.accumulatedRotationRadians = nextAccumulatedRotation;
        transformDrag_.rotationPreviousPhase = nextRotationPhase;
        if (!renderer_.setGizmoInteractionFeedback(GizmoInteractionFeedback{
                true,
                GizmoMode::Rotate,
                transformDrag_.constraint.handle,
                nextAccumulatedRotation,
                transformDrag_.rotationStartPhase,
                nextRotationPhase,
                true,
            })) {
            return false;
        }
    }
    transformDrag_.preview = preview;
    return true;
}

bool ViewportHost::endTransformGesture(const bool commit) {
    if (!transformDrag_.active) {
        return true;
    }

    const TransformDragState completed = transformDrag_;
    transformDrag_ = {};
    if (!renderer_.setGizmoInteractionFeedback({})) {
        return false;
    }

    if (!commit || completed.preview == completed.before) {
        const auto world = previewWorldMatrix(completed.objectId, completed.before);
        const auto rotation = previewWorldRotation(completed.objectId, completed.before);
        return world && rotation && renderer_.updateObjectTransform(completed.objectId, *world, *rotation);
    }

    SetObjectTransformCommand command{completed.objectId, completed.preview};
    if (!history_.execute(document_, command)) {
        const auto beforeWorld = previewWorldMatrix(completed.objectId, completed.before);
        const auto beforeRotation = previewWorldRotation(completed.objectId, completed.before);
        if (beforeWorld && beforeRotation) {
            (void)renderer_.updateObjectTransform(completed.objectId, *beforeWorld, *beforeRotation);
        }
        return false;
    }

    return syncRendererObjectTransforms();
}

bool ViewportHost::syncRendererObjectTransforms() {
    for (ViewportObjectSnapshot& snapshot : viewportObjects_) {
        const auto world = document_.objectWorldMatrix(snapshot.objectId);
        const auto rotation = document_.objectWorldRotation(snapshot.objectId);
        if (!world || !rotation ||
            !renderer_.updateObjectTransform(snapshot.objectId, *world, *rotation)) {
            return false;
        }
        snapshot.worldMatrix = *world;
        snapshot.worldRotation = *rotation;
    }
    return true;
}

bool ViewportHost::undo() {
    if (transformDrag_.active && !endTransformGesture(false)) {
        return false;
    }
    if (!history_.undo(document_)) {
        return false;
    }
    return syncRendererObjectTransforms();
}

bool ViewportHost::redo() {
    if (transformDrag_.active && !endTransformGesture(false)) {
        return false;
    }
    if (!history_.redo(document_)) {
        return false;
    }
    return syncRendererObjectTransforms();
}

} // namespace vortex::android
