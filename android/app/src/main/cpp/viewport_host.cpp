#include "viewport_host.hpp"

#include <string>
#include <utility>

namespace vortex::android {
namespace {

[[nodiscard]] bool buildCube(EditableMesh& authored, const float halfExtent) {
    const auto v0 = authored.addVertex({-halfExtent, -halfExtent, -halfExtent});
    const auto v1 = authored.addVertex({ halfExtent, -halfExtent, -halfExtent});
    const auto v2 = authored.addVertex({ halfExtent,  halfExtent, -halfExtent});
    const auto v3 = authored.addVertex({-halfExtent,  halfExtent, -halfExtent});
    const auto v4 = authored.addVertex({-halfExtent, -halfExtent,  halfExtent});
    const auto v5 = authored.addVertex({ halfExtent, -halfExtent,  halfExtent});
    const auto v6 = authored.addVertex({ halfExtent,  halfExtent,  halfExtent});
    const auto v7 = authored.addVertex({-halfExtent,  halfExtent,  halfExtent});
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

bool ViewportHost::appendObjectSnapshot(const ObjectId objectId, const MeshId meshId) {
    const ObjectBlock* object = document_.object(objectId);
    const MeshBlock* block = document_.mesh(meshId);
    if (!objectId || !meshId || object == nullptr || block == nullptr || object->meshId != meshId) {
        return false;
    }

    const auto worldMatrix = document_.objectWorldMatrix(objectId);
    if (!worldMatrix) {
        return false;
    }

    const MeshEvaluationResult evaluated = MeshEvaluator::evaluate(*block);
    if (!evaluated || !evaluated.mesh) {
        return false;
    }
    RenderExtractResult extracted = RenderExtractor::extract(*evaluated.mesh);
    if (!extracted || !extracted.mesh ||
        extracted.mesh->sourceDocumentRuntimeId != document_.runtimeId() ||
        extracted.mesh->sourceMeshId != meshId) {
        return false;
    }

    viewportObjects_.push_back(ViewportObjectSnapshot{
        objectId,
        std::move(*extracted.mesh),
        *worldMatrix,
    });
    return true;
}

bool ViewportHost::initializeScene() {
    EditableMesh cubeMesh;
    if (!buildCube(cubeMesh, 1.0F)) {
        return false;
    }
    const MeshId cubeMeshId = document_.createMesh("Cube Mesh", std::move(cubeMesh));
    cubeObject_ = document_.createObject("Cube", cubeMeshId);
    if (!cubeMeshId || !cubeObject_) {
        return false;
    }

    // Phase 6B proves object placement is authored on ObjectBlock rather than baked into
    // topology. Both test meshes are centered at local origin; only engine transform state
    // places the second cube in world space.
    constexpr float testHalfExtent = 0.45F;
    EditableMesh testMesh;
    if (!buildCube(testMesh, testHalfExtent)) {
        return false;
    }
    const MeshId testMeshId = document_.createMesh("Test Cube Mesh", std::move(testMesh));
    testCubeObject_ = document_.createObject("Test Cube", testMeshId);
    if (!testMeshId || !testCubeObject_) {
        return false;
    }

    ObjectTransform testTransform;
    testTransform.translation = {1.55F, 0.30F, 0.0F};
    if (!document_.setObjectTransform(testCubeObject_, testTransform) || !document_.validate()) {
        return false;
    }

    viewportObjects_.clear();
    viewportObjects_.reserve(2U);
    if (!appendObjectSnapshot(cubeObject_, cubeMeshId) ||
        !appendObjectSnapshot(testCubeObject_, testMeshId)) {
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

    const ObjectId previousObject = editor_.activeObject();
    const FaceId previousFace = lastPickedFace_;
    const auto restoreEditorSelection = [this, previousObject, previousFace]() noexcept {
        lastPickedFace_ = previousFace;
        return editor_.setActiveObject(previousObject);
    };

    const auto picked = renderer_.pickObject(xPixels, yPixels);
    if (picked) {
        if (!editor_.setActiveObject(picked->objectId)) {
            return false;
        }
        lastPickedFace_ = picked->sourceFace;
        if (renderer_.setSelectedObject(picked->objectId)) {
            return true;
        }
        (void)restoreEditorSelection();
        return false;
    }

    if (!editor_.setActiveObject({})) {
        return false;
    }
    lastPickedFace_ = {};
    if (renderer_.setSelectedObject({})) {
        return true;
    }
    (void)restoreEditorSelection();
    return false;
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
