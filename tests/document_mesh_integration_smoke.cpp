#include "vortex/core/document.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <iostream>

int main() {
    vortex::EditableMesh authored;
    const auto v0 = authored.addVertex({-1.0F, 0.0F, -1.0F});
    const auto v1 = authored.addVertex({ 1.0F, 0.0F, -1.0F});
    const auto v2 = authored.addVertex({ 1.0F, 0.0F,  1.0F});
    const auto v3 = authored.addVertex({-1.0F, 0.0F,  1.0F});
    const auto sourceFace = authored.addFace({v0, v1, v2, v3});
    assert(sourceFace && authored.validate());

    vortex::Document document;
    const vortex::MeshId sharedMesh = document.createMesh("Shared Quad", std::move(authored));
    assert(sharedMesh);
    const vortex::ObjectId objectA = document.createObject("A", sharedMesh);
    const vortex::ObjectId objectB = document.createObject("B", sharedMesh);
    assert(objectA && objectB);
    assert(document.meshUserCount(sharedMesh) == 2);
    assert(document.authoredMesh(sharedMesh) != nullptr);
    assert(document.authoredMesh(sharedMesh)->hasVertex(v0));
    assert(document.authoredMesh(sharedMesh)->hasFace(sourceFace));
    assert(document.validate());

    // A metadata copy shares the authored payload instead of deep-copying all geometry.
    const vortex::Document metadataSnapshot = document;
    assert(metadataSnapshot.authoredMesh(sharedMesh) == document.authoredMesh(sharedMesh));

    // Make Unique creates a new Mesh datablock with a deep-cloned authored payload.
    const vortex::MeshId uniqueMesh = document.makeObjectMeshUnique(objectB);
    assert(uniqueMesh && uniqueMesh != sharedMesh);
    assert(document.object(objectA)->meshId == sharedMesh);
    assert(document.object(objectB)->meshId == uniqueMesh);
    assert(document.meshUserCount(sharedMesh) == 1);
    assert(document.meshUserCount(uniqueMesh) == 1);
    assert(document.authoredMesh(uniqueMesh) != nullptr);
    assert(document.authoredMesh(uniqueMesh) != document.authoredMesh(sharedMesh));
    assert(document.authoredMesh(uniqueMesh)->hasVertex(v0));
    assert(document.authoredMesh(uniqueMesh)->hasFace(sourceFace));
    assert(document.authoredMesh(uniqueMesh)->vertexCount() == document.authoredMesh(sharedMesh)->vertexCount());
    assert(document.authoredMesh(uniqueMesh)->edgeCount() == document.authoredMesh(sharedMesh)->edgeCount());
    assert(document.authoredMesh(uniqueMesh)->faceCount() == document.authoredMesh(sharedMesh)->faceCount());
    assert(document.validate());

    // Edit only the unique payload through the Document command bridge.
    vortex::MeshHistory uniqueHistory(64U * 1024U);
    const std::uint64_t revisionBeforeMove = document.revision();
    const std::uint64_t meshRevisionBeforeMove = document.mesh(uniqueMesh)->revision;
    vortex::MoveVerticesCommand moveUnique({{v0, {-3.0F, 0.5F, -1.0F}}});
    assert(document.executeMeshCommand(uniqueMesh, uniqueHistory, moveUnique));
    assert(document.mesh(uniqueMesh)->revision == meshRevisionBeforeMove + 1);
    assert(document.revision() == revisionBeforeMove + 1);
    assert(document.authoredMesh(uniqueMesh)->position(v0)->x == -3.0F);
    assert(document.authoredMesh(sharedMesh)->position(v0)->x == -1.0F);

    const auto changes = document.changesSince(revisionBeforeMove);
    assert(changes.size() == 1);
    assert(changes[0].dataKind == vortex::DataKind::Mesh);
    assert(changes[0].changeKind == vortex::ChangeKind::Updated);
    assert(changes[0].entityId == uniqueMesh.value());

    const std::uint64_t revisionBeforeUndo = document.revision();
    assert(document.undoMeshCommand(uniqueMesh, uniqueHistory));
    assert(document.revision() == revisionBeforeUndo + 1);
    assert(document.authoredMesh(uniqueMesh)->position(v0)->x == -1.0F);
    assert(document.authoredMesh(sharedMesh)->position(v0)->x == -1.0F);

    assert(document.redoMeshCommand(uniqueMesh, uniqueHistory));
    assert(document.authoredMesh(uniqueMesh)->position(v0)->x == -3.0F);
    assert(document.authoredMesh(sharedMesh)->position(v0)->x == -1.0F);

    // Topology commands also execute through the datablock bridge and stay undoable.
    vortex::ExtrudeFaceCommand extrude(sourceFace, {0.0F, 1.0F, 0.0F});
    vortex::MeshCommandResult extrusionResult;
    assert(document.executeMeshCommand(uniqueMesh, uniqueHistory, extrude, &extrusionResult));
    assert(extrusionResult.extrusion);
    const auto cap = extrusionResult.extrusion->capFace;
    assert(!document.authoredMesh(uniqueMesh)->hasFace(sourceFace));
    assert(document.authoredMesh(uniqueMesh)->hasFace(cap));
    assert(document.authoredMesh(sharedMesh)->hasFace(sourceFace));

    assert(document.undoMeshCommand(uniqueMesh, uniqueHistory));
    assert(document.authoredMesh(uniqueMesh)->hasFace(sourceFace));
    assert(!document.authoredMesh(uniqueMesh)->hasFace(cap));
    assert(document.authoredMesh(sharedMesh)->hasFace(sourceFace));

    // Bad MeshIds are rejected without a Document revision.
    const std::uint64_t beforeInvalid = document.revision();
    vortex::MoveVerticesCommand invalidTarget({{v0, {0.0F, 0.0F, 0.0F}}});
    assert(!document.executeMeshCommand(vortex::MeshId{999999}, uniqueHistory, invalidTarget));
    assert(document.revision() == beforeInvalid);
    assert(document.validate());

    std::cout << "Vortex3D Document/authored-mesh integration smoke test passed\n";
    return 0;
}
