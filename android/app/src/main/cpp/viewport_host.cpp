#include "viewport_host.hpp"

#include <array>
#include <string>
#include <utility>

namespace vortex::android {
namespace {

[[nodiscard]] bool buildCube(
    EditableMesh& authored,
    const Vec3 center,
    const float halfExtent) {
    const auto v0 = authored.addVertex({center.x - halfExtent, center.y - halfExtent, center.z - halfExtent});
    const auto v1 = authored.addVertex({center.x + halfExtent, center.y - halfExtent, center.z - halfExtent});
    const auto v2 = authored.addVertex({center.x + halfExtent, center.y + halfExtent, center.z - halfExtent});
    const auto v3 = authored.addVertex({center.x - halfExtent, center.y + halfExtent, center.z - halfExtent});
    const auto v4 = authored.addVertex({center.x - halfExtent, center.y - halfExtent, center.z + halfExtent});
    const auto v5 = authored.addVertex({center.x + halfExtent, center.y - halfExtent, center.z + halfExtent});
    const auto v6 = authored.addVertex({center.x + halfExtent, center.y + halfExtent, center.z + halfExtent});
    const auto v7 = authored.addVertex({center.x - halfExtent, center.y + halfExtent, center.z + halfExtent});
    if (!v0 || !v1 || !v2 || !v3 || !v4 || !v5 || !v6 || !v7) {
        return false;
    }

    return authored.addFace({v0, v3, v2, v1}) &&
           authored.addFace({v4, v5, v6, v7}) &&
           authored.addFace({v0, v1, v5, v4}) &&
           authored.addFace({v1, v2, v6, v5}) &&
           authored.addFace({v2, v3, v7, v6}) &&
           authored.addFace({v3, v0, v4, v7}) &&
           authored.validateStrict();
}

} // namespace

ViewportHost::ViewportHost()
    : editor_(document_, history_) {
    initialized_ = initializeScene();
}

bool ViewportHost::appendObjectSnapshot(
    const ObjectId objectId,
    const MeshId meshId,
    const std::array<float, 3>& origin) {
    const MeshBlock* block = document_.mesh(meshId);
    if (!objectId || block == nullptr) {
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

    viewportObjects_.push_back(ViewportObjectSnapshot{
        objectId,
        std::move(*extracted.mesh),
        origin,
    });
    return true;
}

bool ViewportHost::initializeScene() {
    EditableMesh cubeMesh;
    if (!buildCube(cubeMesh, {0.0F, 0.0F, 0.0F}, 1.0F)) {
        return false;
    }
    const MeshId cubeMeshId = document_.createMesh("Cube Mesh", std::move(cubeMesh));
    cubeObject_ = document_.createObject("Cube", cubeMeshId);
    if (!cubeMeshId || !cubeObject_) {
        return false;
    }

    // Stage 5B intentionally uses a second authored mesh instead of pretending ObjectBlock
    // already owns transforms. Phase 6 will move placement into real engine transform state.
    constexpr Vec3 testCenter{1.55F, 0.30F, 0.0F};
    constexpr float testHalfExtent = 0.45F;
    EditableMesh testMesh;
    if (!buildCube(testMesh, testCenter, testHalfExtent)) {
        return false;
    }
    const MeshId testMeshId = document_.createMesh("Test Cube Mesh", std::move(testMesh));
    testCubeObject_ = document_.createObject("Test Cube", testMeshId);
    if (!testMeshId || !testCubeObject_ || !document_.validate()) {
        return false;
    }

    viewportObjects_.clear();
    viewportObjects_.reserve(2U);
    if (!appendObjectSnapshot(cubeObject_, cubeMeshId, {0.0F, 0.0F, 0.0F}) ||
        !appendObjectSnapshot(
            testCubeObject_,
            testMeshId,
            {testCenter.x, testCenter.y, testCenter.z})) {
        return false;
    }

    if (!renderer_.setViewportObjects(viewportObjects_)) {
        return false;
    }
    lastPickedFace_ = {};
    return editor_.setActiveObject({}) && renderer_.setSelectedObject({});
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

    const auto picked = renderer_.pickObject(xPixels, yPixels);
    if (picked) {
        if (!editor_.setActiveObject(picked->objectId)) {
            return false;
        }
        lastPickedFace_ = picked->sourceFace;
        return renderer_.setSelectedObject(picked->objectId);
    }

    if (!editor_.setActiveObject({})) {
        return false;
    }
    lastPickedFace_ = {};
    return renderer_.setSelectedObject({});
}

std::string ViewportHost::info() const {
    std::string result = renderer_.info();
    if (!initialized_) {
        return result + " | editor session init failed";
    }

    result += " | objects " + std::to_string(viewportObjects_.size());
    const ObjectId activeObject = editor_.activeObject();
    if (!activeObject) {
        return result + " | no selection";
    }

    const ObjectBlock* active = document_.object(activeObject);
    result += " | selected ";
    result += active != nullptr ? active->name : "unknown object";
    if (lastPickedFace_) {
        result += " | face " + std::to_string(lastPickedFace_.value());
    }
    return result;
}

} // namespace vortex::android
