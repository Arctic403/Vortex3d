#pragma once

#include "vulkan_viewport.hpp"

#include "vortex/engine.hpp"

#include <android/native_window.h>

#include <optional>
#include <string>
#include <vector>

namespace vortex::android {

enum class TransformToolMode : std::uint8_t {
    Move = 0U,
    Rotate = 1U,
    Scale = 2U,
};

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
        TransformToolMode mode = TransformToolMode::Move;
        GizmoAxis axis = GizmoAxis::X;
        ObjectId objectId;
        ObjectTransform before{};
        ObjectTransform preview{};
        Vec3 translationAxisParent{};
        float worldUnitsPerTranslationUnit = 1.0F;
        float startX = 0.0F;
        float startY = 0.0F;
        float screenDirectionX = 0.0F;
        float screenDirectionY = 0.0F;
        float pixelsPerWorldUnit = 1.0F;
    };

    [[nodiscard]] bool initializeScene();
    [[nodiscard]] bool appendObjectSnapshot(ObjectId objectId, MeshId meshId);
    [[nodiscard]] bool syncRendererObjectTransforms();
    [[nodiscard]] std::optional<TransformMatrix> previewWorldMatrix(
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
    bool initialized_ = false;
};

} // namespace vortex::android
