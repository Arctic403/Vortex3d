#include "viewport_host.hpp"

#include "vortex/core/document_commands.hpp"

#include <algorithm>
#include <cmath>

namespace vortex::android {
namespace {

constexpr float kMathEpsilon = 1.0e-6F;
constexpr float kRotateRadiansPerPixel = 0.0085F;
constexpr float kScaleExponentPerPixel = 0.0080F;
constexpr float kMinScaleMagnitude = 0.05F;
constexpr float kMaxScaleMagnitude = 20.0F;
constexpr float kTwoPi = 6.2831853071795864769F;

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

[[nodiscard]] GizmoMode gizmoMode(const TransformToolMode mode) noexcept {
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

[[nodiscard]] float clampedScale(const float before, const float factor) noexcept {
    const float sign = before < 0.0F ? -1.0F : 1.0F;
    const float magnitude = std::clamp(
        std::max(std::abs(before), kMinScaleMagnitude) * factor,
        kMinScaleMagnitude,
        kMaxScaleMagnitude);
    return sign * magnitude;
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
    if (!hit || hit->pixelsPerWorldUnit <= kMathEpsilon) {
        return false;
    }

    const Vec3 basis = axisVector(hit->axis);
    const TransformMatrix localMatrix = objectTransformMatrix(object->transform);
    Vec3 translationAxisParent = transformVector(localMatrix, basis);
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
    const float worldUnitsPerTranslationUnit = length3(worldTranslationUnit);
    if (!std::isfinite(worldUnitsPerTranslationUnit) || worldUnitsPerTranslationUnit <= kMathEpsilon) {
        return false;
    }

    transformDrag_.active = true;
    transformDrag_.mode = mode;
    transformDrag_.axis = hit->axis;
    transformDrag_.objectId = objectId;
    transformDrag_.before = object->transform;
    transformDrag_.preview = object->transform;
    transformDrag_.translationAxisParent = translationAxisParent;
    transformDrag_.worldUnitsPerTranslationUnit = worldUnitsPerTranslationUnit;
    transformDrag_.startX = xPixels;
    transformDrag_.startY = yPixels;
    transformDrag_.screenDirectionX = hit->screenDirectionX;
    transformDrag_.screenDirectionY = hit->screenDirectionY;
    transformDrag_.pixelsPerWorldUnit = hit->pixelsPerWorldUnit;
    return true;
}

bool ViewportHost::updateTransformGesture(const float xPixels, const float yPixels) {
    if (!initialized_ || !transformDrag_.active ||
        !std::isfinite(xPixels) || !std::isfinite(yPixels)) {
        return false;
    }

    const float deltaX = xPixels - transformDrag_.startX;
    const float deltaY = yPixels - transformDrag_.startY;
    const float projectedPixels =
        deltaX * transformDrag_.screenDirectionX +
        deltaY * transformDrag_.screenDirectionY;
    if (!std::isfinite(projectedPixels)) {
        return false;
    }

    ObjectTransform preview = transformDrag_.before;
    switch (transformDrag_.mode) {
        case TransformToolMode::Move: {
            const float worldDistance = projectedPixels / transformDrag_.pixelsPerWorldUnit;
            const float localDistance = worldDistance / transformDrag_.worldUnitsPerTranslationUnit;
            preview.translation.x += transformDrag_.translationAxisParent.x * localDistance;
            preview.translation.y += transformDrag_.translationAxisParent.y * localDistance;
            preview.translation.z += transformDrag_.translationAxisParent.z * localDistance;
            break;
        }
        case TransformToolMode::Rotate: {
            const float beforeAngle = component(preview.rotationRadians, transformDrag_.axis);
            const float angle = std::remainder(
                beforeAngle + projectedPixels * kRotateRadiansPerPixel,
                kTwoPi);
            setComponent(preview.rotationRadians, transformDrag_.axis, angle);
            break;
        }
        case TransformToolMode::Scale: {
            const float exponent = std::clamp(
                projectedPixels * kScaleExponentPerPixel,
                -6.0F,
                6.0F);
            const float factor = std::exp(exponent);
            const float beforeScale = component(preview.scale, transformDrag_.axis);
            setComponent(preview.scale, transformDrag_.axis, clampedScale(beforeScale, factor));
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
