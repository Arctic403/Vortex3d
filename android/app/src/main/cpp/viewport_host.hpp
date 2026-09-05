#pragma once

#include "vortex/editor/gizmo.hpp"
#include "vulkan_viewport.hpp"

#include "vortex/engine.hpp"

#include <android/native_window.h>

#include <optional>
#include <string>
#include <vector>

namespace vortex::android {

class ViewportHost final {
public:
    ViewportHost();
    ~ViewportHost() = default;

    ViewportHost(const ViewportHost&) = delete;
    ViewportHost& operator=(const ViewportHost&) = delete;

    [[nodiscard]] bool ready() const noexcept { return initialized_; }

    [[nodiscard]] bool attach(ANativeWindow* window);
    [[nodiscard]] bool resize();
    void detach() noexcept;
    [[nodiscard]] bool render();

    [[nodiscard]] bool orbitCamera(float deltaXPixels, float deltaYPixels) noexcept;
    [[nodiscard]] bool panCamera(float deltaXPixels, float deltaYPixels) noexcept;
    [[nodiscard]] bool zoomCamera(float scaleFactor) noexcept;
    [[nodiscard]] bool tap(float xPixels, float yPixels) noexcept;

    [[nodiscard]] bool setTransformTool(const TransformToolMode mode) noexcept {
        if (transformDrag_.active || !renderer_.setGizmoInteractionFeedback({})) {
            return false;
        }

        // Scale is evaluated in object-local space because the authored document stores strict
        // TRS. Keep the user's Move/Rotate orientation preference separate so a temporary Scale
        // operation does not silently overwrite Global/View/Local for the next tool.
        const TransformOrientation previousOrientation = transformOrientation_;
        TransformOrientation nextOrientation = preferredTransformOrientation_;
        if (mode == TransformToolMode::Scale) {
            nextOrientation = TransformOrientation::Local;
            if (!renderer_.setGizmoOrientation(TransformOrientation::Local)) {
                return false;
            }
        } else if (!renderer_.setGizmoOrientation(nextOrientation)) {
            return false;
        }

        if (!renderer_.setGizmoMode(gizmoMode(mode))) {
            (void)renderer_.setGizmoOrientation(previousOrientation);
            return false;
        }

        transformToolMode_ = mode;
        transformOrientation_ = nextOrientation;
        return true;
    }
    [[nodiscard]] bool setTransformOrientation(const TransformOrientation orientation) noexcept {
        if (transformDrag_.active || transformToolMode_ == TransformToolMode::Scale ||
            !renderer_.setGizmoOrientation(orientation)) {
            return false;
        }
        transformOrientation_ = orientation;
        preferredTransformOrientation_ = orientation;
        return true;
    }
    [[nodiscard]] bool setDisplayDensity(const float density) noexcept {
        return renderer_.setDisplayDensity(density);
    }

    [[nodiscard]] bool beginTransformGesture(
        TransformToolMode mode,
        float xPixels,
        float yPixels);
    [[nodiscard]] bool updateTransformGesture(float xPixels, float yPixels);
    [[nodiscard]] bool endTransformGesture(bool commit);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] std::string info() const;

private:
    struct TransformDragState final {
        bool active = false;
        GizmoConstraint constraint{};
        ObjectId objectId;
        ObjectTransform before{};
        ObjectTransform preview{};
        GizmoFrame frame{};
        GizmoCameraFrame camera{};
        TransformMatrix parentWorldMatrix{};
        Quaternion parentWorldRotation{};
        float startAxisParameter = 0.0F;
        PlaneConstraintSample startPlaneSample{};
        float startUniformProjection = 1.0F;
        float rotationStartPhase = 0.0F;
        float rotationPreviousPhase = 0.0F;
        float accumulatedRotationRadians = 0.0F;
        bool hasAxisSample = false;
        bool hasPlaneSample = false;
        bool hasRotationSample = false;
    };

    [[nodiscard]] bool initializeScene();
    [[nodiscard]] bool appendObjectSnapshot(ObjectId objectId, MeshId meshId);
    [[nodiscard]] bool syncRendererObjectTransforms();
    [[nodiscard]] std::optional<TransformMatrix> previewWorldMatrix(
        ObjectId objectId,
        const ObjectTransform& transform) const;
    [[nodiscard]] std::optional<Quaternion> previewWorldRotation(
        ObjectId objectId,
        const ObjectTransform& transform) const;

    Document document_;
    EditorHistory history_;
    EditorContext editor_;
    ObjectId cubeObject_;
    ObjectId testCubeObject_;
    FaceId lastPickedFace_;
    std::vector<ViewportObjectSnapshot> viewportObjects_;
    VulkanViewport renderer_;
    TransformDragState transformDrag_{};
    TransformToolMode transformToolMode_ = TransformToolMode::Move;
    TransformOrientation transformOrientation_ = TransformOrientation::Global;
    TransformOrientation preferredTransformOrientation_ = TransformOrientation::Global;
    bool initialized_ = false;
};

} // namespace vortex::android
