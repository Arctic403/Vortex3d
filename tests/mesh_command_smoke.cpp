#include "vortex/mesh/command.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

vortex::EditableMesh makeQuad(
    vortex::VertexId& v0,
    vortex::VertexId& v1,
    vortex::VertexId& v2,
    vortex::VertexId& v3,
    vortex::FaceId& face) {
    vortex::EditableMesh mesh;
    v0 = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    v1 = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    v2 = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    v3 = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    face = mesh.addFace({v0, v1, v2, v3});
    assert(face && mesh.validate());
    return mesh;
}

std::vector<vortex::CornerId> faceCorners(const vortex::EditableMesh& mesh, const vortex::FaceId faceId) {
    std::vector<vortex::CornerId> result;
    const vortex::MeshFace* face = mesh.face(faceId);
    assert(face != nullptr);
    vortex::CornerId cursor = face->firstCorner;
    for (std::uint32_t index = 0; index < face->cornerCount; ++index) {
        const vortex::MeshCorner* corner = mesh.corner(cursor);
        assert(corner != nullptr);
        result.push_back(cursor);
        cursor = corner->next;
    }
    assert(cursor == face->firstCorner);
    return result;
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

    assert(history.undo(mesh));
    vortex::MoveVerticesCommand divergent({{v0, {-3.0F, 0.0F, -1.0F}}});
    assert(history.execute(mesh, divergent));
    assert(history.redoCount() == 0);
    assert(mesh.position(v0)->x == -3.0F);
}

void testLongMoveRewind() {
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

    vortex::MeshHistory history(1024U * 1024U);
    for (int step = 0; step < 128; ++step) {
        const float x = -1.0F - static_cast<float>(step + 1) * 0.01F;
        vortex::MoveVerticesCommand move({{v0, {x, 0.0F, -1.0F}}});
        assert(history.execute(mesh, move));
    }
    assert(history.undoCount() == 128);
    const float finalX = mesh.position(v0)->x;

    for (int step = 0; step < 128; ++step) {
        assert(history.undo(mesh));
        assert(mesh.validate());
    }
    assert(history.undoCount() == 0);
    assert(history.redoCount() == 128);
    assert(mesh.position(v0)->x == -1.0F);

    for (int step = 0; step < 128; ++step) {
        assert(history.redo(mesh));
        assert(mesh.validate());
    }
    assert(history.redoCount() == 0);
    assert(history.undoCount() == 128);
    assert(mesh.position(v0)->x == finalX);
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
        vortex::MoveVerticesCommand move({{v0, {x, 0.0F, -1.0F}}});
        assert(history.execute(mesh, move));
        assert(history.retainedBytes() <= history.budgetBytes());
        assert(mesh.validate());
    }
    assert(history.undoCount() > 0);
    assert(history.undoCount() < 100);

    const auto before = mesh.position(v0);
    vortex::MeshHistory tinyHistory(1);
    vortex::MoveVerticesCommand oversized({{v0, {-99.0F, 0.0F, -1.0F}}});
    assert(!tinyHistory.execute(mesh, oversized));
    assert(mesh.position(v0) == before);
    assert(tinyHistory.undoCount() == 0);
}

void testExtrudeUndoRedoExactIds() {
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

    auto* materials = mesh.attributes().values<std::int32_t>("material_index", vortex::AttributeDomain::Face);
    assert(materials != nullptr && materials->size() == 1);
    (*materials)[0] = 9;

    const std::vector<vortex::CornerId> originalCorners = faceCorners(mesh, face);
    const std::size_t originalVertices = mesh.vertexCount();
    const std::size_t originalEdges = mesh.edgeCount();
    const std::size_t originalFaces = mesh.faceCount();
    const std::size_t originalCornerCount = mesh.cornerCount();

    vortex::MeshHistory history(64U * 1024U);
    vortex::ExtrudeFaceCommand extrude(face, {0.0F, 1.0F, 0.0F});
    assert(extrude.undoable());

    vortex::MeshCommandResult result;
    assert(history.execute(mesh, extrude, &result));
    assert(result.extrusion);
    const vortex::FaceExtrudeResult firstResult = *result.extrusion;
    assert(firstResult.sourceFace == face);
    assert(!mesh.hasFace(face));
    assert(mesh.hasFace(firstResult.capFace));
    for (const auto side : firstResult.sideFaces) {
        assert(mesh.hasFace(side));
    }
    for (const auto vertex : firstResult.newVertices) {
        assert(mesh.hasVertex(vertex));
    }
    assert(mesh.validate());
    assert(history.retainedBytes() > 0 && history.retainedBytes() <= history.budgetBytes());

    assert(history.undo(mesh));
    assert(mesh.hasFace(face));
    assert(mesh.vertexCount() == originalVertices);
    assert(mesh.edgeCount() == originalEdges);
    assert(mesh.faceCount() == originalFaces);
    assert(mesh.cornerCount() == originalCornerCount);
    for (const auto corner : originalCorners) {
        assert(mesh.hasCorner(corner));
    }
    assert(faceCorners(mesh, face) == originalCorners);
    assert(!mesh.hasFace(firstResult.capFace));
    for (const auto side : firstResult.sideFaces) {
        assert(!mesh.hasFace(side));
    }
    for (const auto vertex : firstResult.newVertices) {
        assert(!mesh.hasVertex(vertex));
    }
    materials = mesh.attributes().values<std::int32_t>("material_index", vortex::AttributeDomain::Face);
    assert(materials != nullptr && materials->size() == 1 && (*materials)[0] == 9);
    assert(mesh.validate());

    assert(history.redo(mesh));
    assert(!mesh.hasFace(face));
    assert(mesh.hasFace(firstResult.capFace));
    for (const auto side : firstResult.sideFaces) {
        assert(mesh.hasFace(side));
    }
    for (const auto vertex : firstResult.newVertices) {
        assert(mesh.hasVertex(vertex));
    }
    assert(mesh.validate());

    for (int cycle = 0; cycle < 32; ++cycle) {
        assert(history.undo(mesh));
        assert(mesh.hasFace(face));
        assert(faceCorners(mesh, face) == originalCorners);
        assert(mesh.validate());
        assert(history.redo(mesh));
        assert(!mesh.hasFace(face));
        assert(mesh.hasFace(firstResult.capFace));
        assert(mesh.validate());
    }
}

void testMixedMoveExtrudeHistory() {
    vortex::VertexId v0;
    vortex::VertexId v1;
    vortex::VertexId v2;
    vortex::VertexId v3;
    vortex::FaceId face;
    auto mesh = makeQuad(v0, v1, v2, v3, face);
    (void)v1;
    (void)v2;
    (void)v3;

    vortex::MeshHistory history(64U * 1024U);
    vortex::MoveVerticesCommand move({{v0, {-2.0F, 0.25F, -1.0F}}});
    assert(history.execute(mesh, move));
    assert(mesh.position(v0)->x == -2.0F);

    vortex::ExtrudeFaceCommand extrude(face, {0.0F, 1.5F, 0.0F});
    vortex::MeshCommandResult extrusionResult;
    assert(history.execute(mesh, extrude, &extrusionResult));
    assert(extrusionResult.extrusion);
    const auto created = *extrusionResult.extrusion;
    assert(history.undoCount() == 2);

    assert(history.undo(mesh));
    assert(mesh.hasFace(face));
    assert(mesh.position(v0)->x == -2.0F);
    assert(history.undo(mesh));
    assert(mesh.position(v0)->x == -1.0F);
    assert(mesh.hasFace(face));
    assert(mesh.validate());

    assert(history.redo(mesh));
    assert(mesh.position(v0)->x == -2.0F);
    assert(mesh.hasFace(face));
    assert(history.redo(mesh));
    assert(!mesh.hasFace(face));
    assert(mesh.hasFace(created.capFace));
    for (const auto vertex : created.newVertices) {
        assert(mesh.hasVertex(vertex));
    }
    assert(mesh.validate());
}

void testExtrudeOverBudgetRewinds() {
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

    const auto originalCorners = faceCorners(mesh, face);
    vortex::MeshHistory tinyHistory(1);
    vortex::ExtrudeFaceCommand extrude(face, {0.0F, 1.0F, 0.0F});
    assert(!tinyHistory.execute(mesh, extrude));
    assert(tinyHistory.undoCount() == 0);
    assert(mesh.hasFace(face));
    assert(faceCorners(mesh, face) == originalCorners);
    assert(mesh.vertexCount() == 4);
    assert(mesh.edgeCount() == 4);
    assert(mesh.faceCount() == 1);
    assert(mesh.cornerCount() == 4);
    assert(mesh.validate());
}

} // namespace

int main() {
    testMoveUndoRedo();
    testLongMoveRewind();
    testInvalidMoveIsAtomic();
    testMemoryBudget();
    testExtrudeUndoRedoExactIds();
    testMixedMoveExtrudeHistory();
    testExtrudeOverBudgetRewinds();

    std::cout << "Vortex3D mesh command/history smoke test passed\n";
    return 0;
}
