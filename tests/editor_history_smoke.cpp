#include "vortex/core/document_commands.hpp"
#include "vortex/core/editor_history.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <iostream>

int main() {
    vortex::EditableMesh authored;
    const auto v0 = authored.addVertex({0.0F, 0.0F, 0.0F});
    const auto v1 = authored.addVertex({1.0F, 0.0F, 0.0F});
    const auto v2 = authored.addVertex({0.0F, 1.0F, 0.0F});
    assert(authored.addFace({v0, v1, v2}));
    assert(authored.validate());

    vortex::Document document;
    const auto meshId = document.createMesh("Triangle", std::move(authored));
    const auto objectId = document.createObject("Object", meshId);
    assert(meshId && objectId);

    vortex::EditorHistory history(2U * 1024U * 1024U);

    vortex::RenameObjectCommand renameA{objectId, "Renamed"};
    assert(history.execute(document, renameA));
    assert(document.object(objectId)->name == "Renamed");

    vortex::MoveVerticesCommand move{{{v0, {2.0F, 0.0F, 0.0F}}}};
    assert(history.executeMesh(document, meshId, move));
    assert(document.authoredMesh(meshId)->position(v0)->x == 2.0F);

    vortex::RenameObjectCommand renameB{objectId, "Final"};
    assert(history.execute(document, renameB));
    assert(history.undoCount() == 3U);
    assert(document.object(objectId)->name == "Final");

    // One timeline means undo crosses document/mesh boundaries in exact editor order.
    assert(history.undo(document));
    assert(document.object(objectId)->name == "Renamed");
    assert(document.authoredMesh(meshId)->position(v0)->x == 2.0F);

    assert(history.undo(document));
    assert(document.authoredMesh(meshId)->position(v0)->x == 0.0F);
    assert(document.object(objectId)->name == "Renamed");

    assert(history.undo(document));
    assert(document.object(objectId)->name == "Object");
    assert(history.undoCount() == 0U);
    assert(history.redoCount() == 3U);

    assert(history.redo(document));
    assert(document.object(objectId)->name == "Renamed");
    assert(history.redo(document));
    assert(document.authoredMesh(meshId)->position(v0)->x == 2.0F);
    assert(history.redo(document));
    assert(document.object(objectId)->name == "Final");

    // Semantic no-ops do not consume history or destroy redo state.
    assert(history.undo(document));
    assert(history.redoCount() == 1U);
    const auto redoBeforeNoop = history.redoCount();
    vortex::RenameObjectCommand noOpRename{objectId, "Renamed"};
    assert(history.execute(document, noOpRename));
    assert(history.redoCount() == redoBeforeNoop);

    // A real new edit clears the redo branch.
    vortex::MoveVerticesCommand secondMove{{{v1, {3.0F, 0.0F, 0.0F}}}};
    assert(history.executeMesh(document, meshId, secondMove));
    assert(history.redoCount() == 0U);

    // Runtime lineage binding prevents replay into another Document.
    vortex::Document other;
    const auto otherMesh = other.createMesh("Other");
    const auto otherObject = other.createObject("Other Object", otherMesh);
    vortex::RenameObjectCommand crossDocument{otherObject, "Should Not Apply"};
    assert(!history.execute(other, crossDocument));
    assert(other.object(otherObject)->name == "Other Object");
    assert(!history.undo(other));

    // Shared budget can evict oldest mixed records while preserving a valid suffix.
    history.setBudgetBytes(1U);
    assert(history.undoCount() == 0U);
    assert(history.redoCount() == 0U);
    assert(history.retainedBytes() == 0U);

    assert(document.validate());
    std::cout << "Vortex3D unified editor history smoke test passed\n";
    return 0;
}
