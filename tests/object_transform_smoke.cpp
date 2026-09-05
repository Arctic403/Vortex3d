#include "vortex/core/document_commands.hpp"
#include "vortex/core/editor_history.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>

namespace {

[[nodiscard]] bool near(const float a, const float b, const float epsilon = 1.0e-5F) {
    return std::abs(a - b) <= epsilon;
}

[[nodiscard]] bool nearVec3(const vortex::Vec3 a, const vortex::Vec3 b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

} // namespace

int main() {
    vortex::EditableMesh mesh;
    const vortex::VertexId vertex = mesh.addVertex({0.0F, 0.0F, 0.0F});
    assert(vertex);

    vortex::Document document;
    const vortex::MeshId meshId = document.createMesh("Mesh", std::move(mesh));
    const vortex::ObjectId parent = document.createObject("Parent", meshId);
    const vortex::ObjectId child = document.createObject("Child", meshId);
    assert(meshId && parent && child);
    assert(document.setObjectParent(child, parent));

    const vortex::ObjectTransform identity{};
    assert(document.object(parent)->transform == identity);
    assert(document.object(child)->transform == identity);

    const std::uint64_t revisionBeforeNoOp = document.revision();
    assert(document.setObjectTransform(parent, identity));
    assert(document.revision() == revisionBeforeNoOp);

    vortex::ObjectTransform invalid = identity;
    invalid.translation.x = std::numeric_limits<float>::quiet_NaN();
    assert(!vortex::isFiniteObjectTransform(invalid));
    assert(!document.setObjectTransform(parent, invalid));
    assert(document.object(parent)->transform == identity);
    assert(document.revision() == revisionBeforeNoOp);

    vortex::ObjectTransform parentTransform;
    parentTransform.translation = {2.0F, 0.0F, 0.0F};
    parentTransform.rotationRadians = {0.0F, 0.0F, std::numbers::pi_v<float> * 0.5F};
    parentTransform.scale = {2.0F, 2.0F, 2.0F};
    assert(document.setObjectTransform(parent, parentTransform));

    vortex::ObjectTransform childTransform;
    childTransform.translation = {1.0F, 0.0F, 0.0F};
    assert(document.setObjectTransform(child, childTransform));

    const auto childWorld = document.objectWorldMatrix(child);
    assert(childWorld.has_value());
    const vortex::Vec3 childOrigin = vortex::transformPoint(*childWorld, {});
    assert(nearVec3(childOrigin, {2.0F, 2.0F, 0.0F}));
    assert(document.validate());

    // Reset to identity so the unified-history sequence has an unambiguous baseline.
    assert(document.setObjectTransform(parent, identity));
    assert(document.setObjectTransform(child, identity));

    vortex::EditorHistory history;
    vortex::SetObjectTransformCommand noOpTransform{child, identity};
    assert(history.execute(document, noOpTransform));
    assert(history.undoCount() == 0U);

    vortex::ObjectTransform moved;
    moved.translation = {3.0F, 4.0F, 5.0F};
    moved.rotationRadians = {0.1F, -0.2F, 0.3F};
    moved.scale = {1.5F, 0.75F, 2.0F};
    vortex::SetObjectTransformCommand transformCommand{child, moved};
    assert(history.execute(document, transformCommand));
    assert(document.object(child)->transform == moved);

    vortex::MoveVerticesCommand moveVertices({vortex::VertexPositionTarget{vertex, {7.0F, 0.0F, 0.0F}}});
    assert(history.executeMesh(document, meshId, moveVertices));
    const auto movedPosition = document.authoredMesh(meshId)->position(vertex);
    assert(movedPosition && nearVec3(*movedPosition, {7.0F, 0.0F, 0.0F}));

    vortex::RenameObjectCommand rename{child, "Child Renamed"};
    assert(history.execute(document, rename));
    assert(document.object(child)->name == "Child Renamed");
    assert(history.undoCount() == 3U);

    // Unified EditorHistory must unwind document and mesh mutations in exact chronology.
    assert(history.undo(document));
    assert(document.object(child)->name == "Child");
    assert(document.object(child)->transform == moved);

    assert(history.undo(document));
    const auto originalPosition = document.authoredMesh(meshId)->position(vertex);
    assert(originalPosition && nearVec3(*originalPosition, {0.0F, 0.0F, 0.0F}));
    assert(document.object(child)->transform == moved);

    assert(history.undo(document));
    assert(document.object(child)->transform == identity);

    assert(history.redo(document));
    assert(document.object(child)->transform == moved);
    assert(history.redo(document));
    const auto redonePosition = document.authoredMesh(meshId)->position(vertex);
    assert(redonePosition && nearVec3(*redonePosition, {7.0F, 0.0F, 0.0F}));
    assert(history.redo(document));
    assert(document.object(child)->name == "Child Renamed");
    assert(document.validate());

    std::cout << "Vortex3D object transform smoke passed\n";
    return 0;
}
