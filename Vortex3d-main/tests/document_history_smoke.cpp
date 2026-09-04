#include "vortex/core/command.hpp"
#include "vortex/core/document.hpp"
#include "vortex/core/document_commands.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

vortex::EditableMesh makeQuad(vortex::VertexId& firstVertex, vortex::FaceId& face) {
    vortex::EditableMesh mesh;
    const auto v0 = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    const auto v1 = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    const auto v2 = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    const auto v3 = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    face = mesh.addFace({v0, v1, v2, v3});
    firstVertex = v0;
    assert(face && mesh.validate());
    return mesh;
}

void testMetadataHistoryAndBudget() {
    vortex::Document document;
    const auto meshA = document.createMesh("A");
    const auto meshB = document.createMesh("B");
    const auto parent = document.createObject("Parent", meshA);
    const auto child = document.createObject("Child", meshA);
    assert(meshA && meshB && parent && child);

    vortex::DocumentHistory history(4096);
    vortex::RenameObjectCommand rename{child, "Child Renamed"};
    vortex::SetObjectParentCommand setParent{child, parent};
    vortex::SetObjectMeshCommand setMesh{child, meshB};
    assert(history.execute(document, rename));
    assert(history.execute(document, setParent));
    assert(history.execute(document, setMesh));
    assert(document.object(child)->name == "Child Renamed");
    assert(document.object(child)->parentId == parent);
    assert(document.object(child)->meshId == meshB);
    assert(history.undoCount() == 3);
    assert(history.retainedBytes() <= history.budgetBytes());

    assert(history.undo(document));
    assert(document.object(child)->meshId == meshA);
    assert(history.undo(document));
    assert(!document.object(child)->parentId);
    assert(history.undo(document));
    assert(document.object(child)->name == "Child");

    assert(history.redo(document));
    assert(history.redo(document));
    assert(history.redo(document));
    assert(document.object(child)->name == "Child Renamed");
    assert(document.object(child)->parentId == parent);
    assert(document.object(child)->meshId == meshB);

    assert(history.undo(document));
    vortex::RenameObjectCommand divergent{child, "Different Branch"};
    assert(history.execute(document, divergent));
    assert(history.redoCount() == 0);

    const std::string before = document.object(child)->name;
    const std::uint64_t beforeRevision = document.revision();
    vortex::DocumentHistory tinyHistory(1);
    vortex::RenameObjectCommand oversized{child, std::string(256, 'x')};
    assert(!tinyHistory.execute(document, oversized));
    assert(document.object(child)->name == before);
    assert(document.revision() == beforeRevision);
}

void testMakeUniqueHistoryMovesOwnership() {
    vortex::VertexId firstVertex;
    vortex::FaceId face;
    vortex::Document document;
    const auto shared = document.createMesh("Shared", makeQuad(firstVertex, face));
    const auto objectA = document.createObject("A", shared);
    const auto objectB = document.createObject("B", shared);
    assert(shared && objectA && objectB);

    vortex::DocumentHistory history(2U * 1024U * 1024U);
    vortex::MakeObjectMeshUniqueCommand makeUnique{objectB};
    assert(history.execute(document, makeUnique));
    const auto unique = makeUnique.result();
    assert(unique && unique != shared);
    assert(document.hasMesh(unique));
    assert(document.authoredMesh(unique)->hasVertex(firstVertex));
    assert(document.authoredMesh(unique)->hasFace(face));

    assert(history.undo(document));
    assert(document.object(objectB)->meshId == shared);
    assert(!document.hasMesh(unique));
    assert(history.retainedBytes() <= history.budgetBytes());

    assert(history.redo(document));
    assert(document.object(objectB)->meshId == unique);
    assert(document.hasMesh(unique));
    assert(document.authoredMesh(unique)->hasVertex(firstVertex));
    assert(document.authoredMesh(unique)->hasFace(face));
    assert(document.authoredMesh(unique) != document.authoredMesh(shared));
    assert(document.validate());
}


void testRandomizedUndoRedoCycles() {
    vortex::Document document;
    const auto meshA = document.createMesh("A");
    const auto meshB = document.createMesh("B");
    const auto parentA = document.createObject("Parent A", meshA);
    const auto parentB = document.createObject("Parent B", meshB);
    const auto child = document.createObject("Child", meshA);
    assert(meshA && meshB && parentA && parentB && child);

    const std::string baselineName = document.object(child)->name;
    const vortex::ObjectId baselineParent = document.object(child)->parentId;
    const vortex::MeshId baselineMesh = document.object(child)->meshId;

    vortex::DocumentHistory history(2U * 1024U * 1024U);
    std::mt19937 rng(0x50334834U);
    std::size_t applied = 0;

    for (std::size_t step = 0; step < 160U; ++step) {
        const std::uint32_t action = rng() % 3U;
        bool ok = false;
        if (action == 0U) {
            vortex::RenameObjectCommand command{child, "Child-" + std::to_string(step)};
            ok = history.execute(document, command);
        } else if (action == 1U) {
            const vortex::ObjectId target = (rng() & 1U) != 0U ? parentA : parentB;
            vortex::SetObjectParentCommand command{child, target};
            ok = history.execute(document, command);
        } else {
            const vortex::MeshId target = (rng() & 1U) != 0U ? meshA : meshB;
            vortex::SetObjectMeshCommand command{child, target};
            ok = history.execute(document, command);
        }
        assert(ok);
        assert(document.validate());
        applied = history.undoCount();
    }

    assert(applied >= 100U);
    const std::string finalName = document.object(child)->name;
    const vortex::ObjectId finalParent = document.object(child)->parentId;
    const vortex::MeshId finalMesh = document.object(child)->meshId;

    while (history.undoCount() != 0U) {
        assert(history.undo(document));
        assert(document.validate());
    }
    assert(document.object(child)->name == baselineName);
    assert(document.object(child)->parentId == baselineParent);
    assert(document.object(child)->meshId == baselineMesh);

    while (history.redoCount() != 0U) {
        assert(history.redo(document));
        assert(document.validate());
    }
    assert(document.object(child)->name == finalName);
    assert(document.object(child)->parentId == finalParent);
    assert(document.object(child)->meshId == finalMesh);
}

void testHistoryIsBoundToDocumentInstance() {
    vortex::Document first;
    vortex::Document second;
    const auto firstObject = first.createObject("First");
    const auto secondObject = second.createObject("Second");
    assert(firstObject && secondObject);

    vortex::DocumentHistory history;
    vortex::RenameObjectCommand firstRename{firstObject, "First Changed"};
    assert(history.execute(first, firstRename));

    vortex::RenameObjectCommand secondRename{secondObject, "Second Changed"};
    assert(!history.execute(second, secondRename));
    assert(second.object(secondObject)->name == "Second");
}

} // namespace

int main() {
    testMetadataHistoryAndBudget();
    testMakeUniqueHistoryMovesOwnership();
    testHistoryIsBoundToDocumentInstance();
    testRandomizedUndoRedoCycles();
    std::cout << "Vortex3D Document history smoke test passed\n";
    return 0;
}
