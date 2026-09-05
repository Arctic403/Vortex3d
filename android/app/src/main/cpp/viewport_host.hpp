#pragma once

#include "vulkan_viewport.hpp"

#include "vortex/engine.hpp"

#include <android/native_window.h>

#include <string>

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

    [[nodiscard]] std::string info() const;

private:
    [[nodiscard]] bool initializeScene();

    Document document_;
    EditorHistory history_;
    EditorContext editor_;
    ObjectId cubeObject_;
    ViewportMesh viewportMesh_;
    VulkanViewport renderer_;
    bool initialized_ = false;
};

} // namespace vortex::android
