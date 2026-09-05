#include "viewport_host.hpp"

#include "vortex/core/document_commands.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace vortex::android {
namespace {

constexpr float kMathEpsilon = 1.0e-6F;
constexpr float kMaxScaleMagnitude = 20.0F;

[[nodiscard]] Vec3 axisVector(const GizmoAxis axis) noexcept {
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

[[nodiscard]] float length3(const Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] float component(const Vec3 value, const GizmoAxis axis) noexcept {
    switch (axis) {
        case GizmoAxis::X:
            return value.x;
        case GizmoAxis::Y:
            return value.y;
        case GizmoAxis::Z:
            return value.z;
    }
    return value.x;
}

void setComponent(Vec3& value, const GizmoAxis axis, const float componentValue) noexcept {
    switch (axis) {
        case GizmoAxis::X:
            value.x = componentValue;
            return;
        case GizmoAxis::Y:
            value.y = componentValue;
            return;
        case GizmoAxis::Z:
            value.z = componentValue;
            return;
    }
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
    if (!hit) {
        return false;
    }
    const auto hitAxis = gizmoAxis(hit->handle);
    if (!hitAxis) {
        return false;
    }
    const GizmoAxis axis = *hitAxis;

    const auto interactionWorld = previewWorldMatrix(objectId, object->transform);
    if (!interactionWorld) {
        return false;
    }

    float startAxisParameter = 0.0F;
    if (mode == TransformToolMode::Move || mode == TransformToolMode::Scale) {
        const auto parameter = renderer_.axisDragParameter(
            *interactionWorld, axis, xPixels, yPixels);
        if (!parameter ||
            (mode == TransformToolMode::Scale && std::abs(*parameter) <= kMathEpsilon)) {
            return false;
        }
        startAxisParameter = *parameter;
    }

    Vec3 translationAxisParent{};
    float worldUnitsPerTranslationUnit = 1.0F;
    if (mode == TransformToolMode::Move) {
        const Vec3 basis = axisVector(axis);
        const TransformMatrix localMatrix = objectTransformMatrix(object->transform);
        translationAxisParent = transformVector(localMatrix, basis);
        const float parentAxisLength = length3(translationAxisParent);
        if (!std::isfinite(parentAxisLength) || parentAxisLength <= kMathEpsilon) {
            return false;
        }
        translationAxisParent.x /= parentAxisLength;
        translationAxisParent.y /= parentAxisLength;
        translationAxisParent.z /= parentAxisLength;

        TransformMatrix parentWorld = identityTransformMatrix();
        if (object->parentId) {
            const auto resolvedParentWorld = document_.objectWorldMatrix(object->parentId);
            if (!resolvedParentWorld) {
                return false;
            }
            parentWorld = *resolvedParentWorld;
        }
        const Vec3 worldTranslationUnit = transformVector(parentWorld, translationAxisParent);
        worldUnitsPerTranslationUnit = length3(worldTranslationUnit);
        if (!std::isfinite(worldUnitsPerTranslationUnit) ||
            worldUnitsPerTranslationUnit <= kMathEpsilon) {
            return false;
        }
    }

    transformDrag_.active = true;
    transformDrag_.mode = mode;
    transformDrag_.axis = axis;
    transformDrag_.objectId = objectId;
    transformDrag_.before = object->transform;
    transformDrag_.preview = object->transform;
    transformDrag_.translationAxisParent = translationAxisParent;
    transformDrag_.interactionWorldMatrix = *interactionWorld;
    transformDrag_.worldUnitsPerTranslationUnit = worldUnitsPerTranslationUnit;
    transformDrag_.startAxisParameter = startAxisParameter;
    transformDrag_.previousX = xPixels;
    transformDrag_.previousY = yPixels;
    transformDrag_.accumulatedRotationRadians = 0.0F;
    return true;
}

bool ViewportHost::updateTransformGesture(const float xPixels, const float yPixels) {
    if (!initialized_ || !transformDrag_.active ||
        !std::isfinite(xPixels) || !std::isfinite(yPixels)) {
        return false;
    }

    ObjectTransform preview = transformDrag_.before;
    float nextAccumulatedRotation = transformDrag_.accumulatedRotationRadians;
    bool commitRotationStep = false;

    switch (transformDrag_.mode) {
        case TransformToolMode::Move: {
            const auto currentParameter = renderer_.axisDragParameter(
                transformDrag_.interactionWorldMatrix,
                transformDrag_.axis,
                xPixels,
                yPixels);
            if (!currentParameter) {
                // A view-aligned axis is geometrically ambiguous. Keep the last valid preview
                // instead of changing interaction models or cancelling the gesture.
                return true;
            }
            const float worldDistance = *currentParameter - transformDrag_.startAxisParameter;
            const float localDistance = worldDistance / transformDrag_.worldUnitsPerTranslationUnit;
            preview.translation.x += transformDrag_.translationAxisParent.x * localDistance;
            preview.translation.y += transformDrag_.translationAxisParent.y * localDistance;
            preview.translation.z += transformDrag_.translationAxisParent.z * localDistance;
            break;
        }
        case TransformToolMode::Rotate: {
            const auto angularDelta = renderer_.rotationDragRadians(
                transformDrag_.interactionWorldMatrix,
                transformDrag_.axis,
                transformDrag_.previousX,
                transformDrag_.previousY,
                xPixels,
                yPixels);
            if (!angularDelta) {
                // Keep the previous valid angular state. Once the ray/plane geometry becomes
                // valid again, rotation resumes from that same pointer sample with no mode swap.
                return true;
            }

            nextAccumulatedRotation += *angularDelta;
            if (!std::isfinite(nextAccumulatedRotation)) {
                return false;
            }

            // The ring solver returns a signed delta around the selected *local* gizmo axis.
            // Compose that delta with the touch-down orientation as a quaternion instead of
            // editing one Euler component. Euler angles remain only the current document-format
            // boundary, so mixed-axis rotations preserve the same orientation the gizmo shows.
            if (std::abs(nextAccumulatedRotation) <= kMathEpsilon) {
                preview.rotationRadians = transformDrag_.before.rotationRadians;
            } else {
                const auto startOrientation = quaternionFromEulerRadians(
                    transformDrag_.before.rotationRadians);
                const auto localDelta = quaternionFromAxisAngle(
                    axisVector(transformDrag_.axis),
                    nextAccumulatedRotation);
                if (!startOrientation || !localDelta) {
                    return false;
                }
                const auto composed = multiplyQuaternions(*startOrientation, *localDelta);
                if (!composed) {
                    return false;
                }
                const auto euler = eulerRadiansFromQuaternionNearest(
                    *composed,
                    transformDrag_.before.rotationRadians);
                if (!euler) {
                    return false;
                }
                preview.rotationRadians = *euler;
            }
            commitRotationStep = true;
            break;
        }
        case TransformToolMode::Scale: {
            const auto currentParameter = renderer_.axisDragParameter(
                transformDrag_.interactionWorldMatrix,
                transformDrag_.axis,
                xPixels,
                yPixels);
            if (!currentParameter) {
                return true;
            }

            const float ratio = *currentParameter / transformDrag_.startAxisParameter;
            if (!std::isfinite(ratio)) {
                return false;
            }
            const float beforeScale = component(preview.scale, transformDrag_.axis);
            const float scaled = std::clamp(
                beforeScale * ratio,
                -kMaxScaleMagnitude,
                kMaxScaleMagnitude);
            setComponent(preview.scale, transformDrag_.axis, scaled);
            break;
        }
    }

    if (!isFiniteObjectTransform(preview)) {
        return false;
    }
    const auto world = previewWorldMatrix(transformDrag_.objectId, preview);
    if (!world || !renderer_.updateObjectWorldMatrix(transformDrag_.objectId, *world)) {
        return false;
    }

    if (commitRotationStep) {
        transformDrag_.accumulatedRotationRadians = nextAccumulatedRotation;
        transformDrag_.previousX = xPixels;
        transformDrag_.previousY = yPixels;
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

    if (!commit || completed.preview == completed.before) {
        const auto world = previewWorldMatrix(completed.objectId, completed.before);
        return world && renderer_.updateObjectWorldMatrix(completed.objectId, *world);
    }

    SetObjectTransformCommand command{completed.objectId, completed.preview};
    if (!history_.execute(document_, command)) {
        const auto beforeWorld = previewWorldMatrix(completed.objectId, completed.before);
        if (beforeWorld) {
            (void)renderer_.updateObjectWorldMatrix(completed.objectId, *beforeWorld);
        }
        return false;
    }

    return syncRendererObjectTransforms();
}

bool ViewportHost::syncRendererObjectTransforms() {
    for (ViewportObjectSnapshot& snapshot : viewportObjects_) {
        const auto world = document_.objectWorldMatrix(snapshot.objectId);
        if (!world || !renderer_.updateObjectWorldMatrix(snapshot.objectId, *world)) {
            return false;
        }
        snapshot.worldMatrix = *world;
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
