#include "vortex/mesh/command.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

vortex::EditableMesh makeQuad(vortex::VertexId& v0, vortex::VertexId& v1, vortex::VertexId& v2, vortex::VertexId& v3, vortex::FaceId& face) {
    vortex::EditableMesh mesh;
    v0 = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    v1 = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    v2 = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    v3 = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    face = mesh.addFace({v0, v1, v2, v3});
    assert(face && mesh.validate());
    return mesh;
}

void testMoveUndoRedo() {
    vortex::VertexId v0;
    vortex::VertexId v1;
    vortex::VertexId v2;
    vortex::VertexId v3;
    vortex::FaceId face;
    auto mesh = makeQuad(v0, v1, v2, v3, face);
    (void)v2;
    (void)v3;
    (void)face;

    vortex::MeshHistory history(4096);
    vortex::MoveVerticesCommand move({
        {v0, {-2.0F, 0.5F, -1.0F}},
        {v1, { 2.0F, 0.5F, -1.0F}},
    });

    vortex::MeshCommandResult result;
    assert(history.execute(mesh, move, &result));
    assert(result.touchedVertices.size() == 2);
    assert(history.undoCount() == 1);
    assert(history.redoCount() == 0);
    assert(history.retainedBytes() > 0 && history.retainedBytes() <= history.budgetBytes());
    assert(mesh.position(v0)->x == -2.0F);
    assert(mesh.position(v1)->x == 2.0F);

    assert(history.undo(mesh));
    assert(history.undoCount() == 0);
    assert(history.redoCount() == 1);
    assert(mesh.position(v0)->x == -1.0F);
    assert(mesh.position(v1)->x == 1.0F);
    assert(mesh.validate());

    assert(history.redo(mesh));
    assert(history.undoCount() == 1);
    assert(history.redoCount() == 0);
    assert(mesh.position(v0)->x == -2.0F);
    assert(mesh.position(v1)->x == 2.0F);
    assert(mesh.validate());

    // Diverging after an undo clears the redo branch.
    assert(history.undo(mesh));
    vortex::MoveVerticesCommand divergent({{{v0, {-3.0F, 0.0F, -1.0F}}}});
    assert(history.execute(mesh, divergent));
    assert(history.redoCount() == 0);
    assert(mesh.position(v0)->x == -3.0F);
}

void testInvalidMoveIsAtomic() {
    vortex::VertexId v0;
    vortex::VertexId v1;
    vortex::VertexId v2;
    vortex::VertexId v3;
    vortex::FaceId face;
    auto mesh = makeQuad(v0, v1, v2, v3, face);
    (void)v1;
    (void)v2;
    (void)v3;
    (void)face;

    const auto before = mesh.position(v0);
    vortex::MeshHistory history;
    vortex::MoveVerticesCommand invalid({
        {v0, {-5.0F, 0.0F, 0.0F}},
        {vortex::VertexId{999999}, {5.0F, 0.0F, 0.0F}},
    });

    assert(!history.execute(mesh, invalid));
    assert(history.undoCount() == 0);
    assert(mesh.position(v0) == before);
    assert(mesh.validate());
}

void testMemoryBudget() {
    vortex::VertexId v0;
    vortex::VertexId v1;
    vortex::VertexId v2;
    vortex::VertexId v3;
    vortex::FaceId face;
    auto mesh = makeQuad(v0, v1, v2, v3, face);
    (void)v1;
    (void)v2;
    (void)v3;
    (void)face;

    vortex::MeshHistory history(1024);
    for (int index = 0; index < 100; ++index) {
        const float x = -1.0F - static_cast<float>(index + 1) * 0.01F;
        vortex::MoveVerticesCommand move({{{v0, {x, 0.0F, -1.0F}}}});
        assert(history.execute(mesh, move));
        assert(history.retainedBytes() <= history.budgetBytes());
        assert(mesh.validate());
    }
    assert(history.undoCount() > 0);
    assert(history.undoCount() < 100);

    // A step larger than the allowed budget is rewound instead of becoming permanent.
    const auto before = mesh.position(v0);
    vortex::MeshHistory tinyHistory(1);
    vortex::MoveVerticesCommand oversized({{{v0, {-99.0F, 0.0F, -1.0F}}}});
    assert(!tinyHistory.execute(mesh, oversized));
    assert(mesh.position(v0) == before);
    assert(tinyHistory.undoCount() == 0);
}

void testExtrudeCommandBoundary() {
    vortex::VertexId v0;
    vortex::VertexId v1;
    vortex::VertexId v2;
    vortex::VertexId v3;
    vortex::FaceId face;
    auto mesh = makeQuad(v0, v1, v2, v3, face);
    (void)v0;
    (void)v1;
    (void)v2;
    (void)v3;

    vortex::ExtrudeFaceCommand extrude(face, {0.0F, 1.0F, 0.0F});
    assert(!extrude.undoable());

    // The history pipeline refuses a command with no reversible topology record before mutation.
    vortex::MeshHistory history;
    const std::size_t beforeVertices = mesh.vertexCount();
    const std::size_t beforeFaces = mesh.faceCount();
    assert(!history.execute(mesh, extrude));
    assert(mesh.vertexCount() == beforeVertices);
    assert(mesh.faceCount() == beforeFaces);
    assert(mesh.hasFace(face));

    // The same operation is already command-routed for headless/kernel callers.
    const auto execution = extrude.apply(mesh);
    assert(execution);
    assert(execution->result.extrusion);
    assert(execution->result.extrusion->sourceFace == face);
    assert(!mesh.hasFace(face));
    assert(mesh.hasFace(execution->result.extrusion->capFace));
    assert(mesh.validate());
}

} // namespace

int main() {
    testMoveUndoRedo();
    testInvalidMoveIsAtomic();
    testMemoryBudget();
    testExtrudeCommandBoundary();

    std::cout << "Vortex3D mesh command/history smoke test passed\n";
    return 0;
}
