#include "viewport_host.hpp"

#include <utility>

namespace vortex::android {

ViewportHost::ViewportHost()
    : editor_(document_, history_) {
    initialized_ = initializeScene();
}

bool ViewportHost::initializeScene() {
    EditableMesh authored;
    const auto v0 = authored.addVertex({-1.0F, -1.0F, -1.0F});
    const auto v1 = authored.addVertex({ 1.0F, -1.0F, -1.0F});
    const auto v2 = authored.addVertex({ 1.0F,  1.0F, -1.0F});
    const auto v3 = authored.addVertex({-1.0F,  1.0F, -1.0F});
    const auto v4 = authored.addVertex({-1.0F, -1.0F,  1.0F});
    const auto v5 = authored.addVertex({ 1.0F, -1.0F,  1.0F});
    const auto v6 = authored.addVertex({ 1.0F,  1.0F,  1.0F});
    const auto v7 = authored.addVertex({-1.0F,  1.0F,  1.0F});
    if (!v0 || !v1 || !v2 || !v3 || !v4 || !v5 || !v6 || !v7) {
        return false;
    }

    if (!authored.addFace({v0, v3, v2, v1}) ||
        !authored.addFace({v4, v5, v6, v7}) ||
        !authored.addFace({v0, v1, v5, v4}) ||
        !authored.addFace({v1, v2, v6, v5}) ||
        !authored.addFace({v2, v3, v7, v6}) ||
        !authored.addFace({v3, v0, v4, v7}) ||
        !authored.validateStrict()) {
        return false;
    }

    const MeshId meshId = document_.createMesh("Cube", std::move(authored));
    cubeObject_ = document_.createObject("Cube", meshId);
    const MeshBlock* block = document_.mesh(meshId);
    if (!meshId || !cubeObject_ || block == nullptr || !document_.validate()) {
        return false;
    }

    const MeshEvaluationResult evaluated = MeshEvaluator::evaluate(*block);
    if (!evaluated || !evaluated.mesh) {
        return false;
    }
    RenderExtractResult extracted = RenderExtractor::extract(*evaluated.mesh);
    if (!extracted || !extracted.mesh) {
        return false;
    }

    viewportMesh_ = std::move(*extracted.mesh);
    if (!renderer_.setViewportMesh(viewportMesh_)) {
        return false;
    }
    return editor_.setActiveObject({}) && renderer_.setSelectionVisible(false);
}

bool ViewportHost::attach(ANativeWindow* window) {
    if (!initialized_) {
        if (window != nullptr) {
            ANativeWindow_release(window);
        }
        return false;
    }
    return renderer_.attach(window);
}

bool ViewportHost::resize() {
    return initialized_ && renderer_.resize();
}

void ViewportHost::detach() noexcept {
    renderer_.detach();
}

bool ViewportHost::render() {
    return initialized_ && renderer_.render();
}

bool ViewportHost::orbitCamera(const float deltaXPixels, const float deltaYPixels) noexcept {
    return initialized_ && renderer_.orbitCamera(deltaXPixels, deltaYPixels);
}

bool ViewportHost::panCamera(const float deltaXPixels, const float deltaYPixels) noexcept {
    return initialized_ && renderer_.panCamera(deltaXPixels, deltaYPixels);
}

bool ViewportHost::zoomCamera(const float scaleFactor) noexcept {
    return initialized_ && renderer_.zoomCamera(scaleFactor);
}

bool ViewportHost::tap(const float xPixels, const float yPixels) noexcept {
    if (!initialized_) {
        return false;
    }

    const auto pickedFace = renderer_.pickFace(xPixels, yPixels);
    if (pickedFace) {
        if (!editor_.setActiveObject(cubeObject_)) {
            return false;
        }
        return renderer_.setSelectionVisible(true);
    }

    if (!editor_.setActiveObject({})) {
        return false;
    }
    return renderer_.setSelectionVisible(false);
}

std::string ViewportHost::info() const {
    std::string result = renderer_.info();
    if (!initialized_) {
        return result + " | editor session init failed";
    }
    if (editor_.activeObject()) {
        result += " | selected Cube";
    } else {
        result += " | no selection";
    }
    return result;
}

} // namespace vortex::android
