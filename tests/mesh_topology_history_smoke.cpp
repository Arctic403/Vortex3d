#include "vortex/mesh/command.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

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

void testStackedExtrusions() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    const auto b = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    const auto c = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    const auto d = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    const auto source = mesh.addFace({a, b, c, d});
    assert(source && mesh.validate());
    const auto sourceCorners = faceCorners(mesh, source);

    vortex::MeshHistory history(256U * 1024U);
    vortex::ExtrudeFaceCommand first(source, {0.0F, 1.0F, 0.0F});
    vortex::MeshCommandResult firstResult;
    assert(history.execute(mesh, first, &firstResult));
    assert(firstResult.extrusion);
    const auto firstIds = *firstResult.extrusion;

    vortex::ExtrudeFaceCommand second(firstIds.capFace, {0.0F, 1.0F, 0.0F});
    vortex::MeshCommandResult secondResult;
    assert(history.execute(mesh, second, &secondResult));
    assert(secondResult.extrusion);
    const auto secondIds = *secondResult.extrusion;
    assert(history.undoCount() == 2);
    assert(mesh.hasFace(secondIds.capFace));
    assert(!mesh.hasFace(firstIds.capFace));
    assert(!mesh.hasFace(source));
    assert(mesh.validate());

    assert(history.undo(mesh));
    assert(mesh.hasFace(firstIds.capFace));
    assert(!mesh.hasFace(secondIds.capFace));
    for (const auto vertex : firstIds.newVertices) {
        assert(mesh.hasVertex(vertex));
    }
    assert(mesh.validate());

    assert(history.undo(mesh));
    assert(mesh.hasFace(source));
    assert(faceCorners(mesh, source) == sourceCorners);
    assert(!mesh.hasFace(firstIds.capFace));
    assert(!mesh.hasFace(secondIds.capFace));
    assert(mesh.vertexCount() == 4);
    assert(mesh.edgeCount() == 4);
    assert(mesh.faceCount() == 1);
    assert(mesh.cornerCount() == 4);
    assert(mesh.validate());

    assert(history.redo(mesh));
    assert(mesh.hasFace(firstIds.capFace));
    assert(!mesh.hasFace(source));
    assert(history.redo(mesh));
    assert(mesh.hasFace(secondIds.capFace));
    assert(!mesh.hasFace(firstIds.capFace));
    for (const auto vertex : secondIds.newVertices) {
        assert(mesh.hasVertex(vertex));
    }
    assert(mesh.validate());
}

void testAttachedCubeExtrudeHistory() {
    vortex::EditableMesh mesh;
    const auto v0 = mesh.addVertex({-1, -1, -1});
    const auto v1 = mesh.addVertex({ 1, -1, -1});
    const auto v2 = mesh.addVertex({ 1,  1, -1});
    const auto v3 = mesh.addVertex({-1,  1, -1});
    const auto v4 = mesh.addVertex({-1, -1,  1});
    const auto v5 = mesh.addVertex({ 1, -1,  1});
    const auto v6 = mesh.addVertex({ 1,  1,  1});
    const auto v7 = mesh.addVertex({-1,  1,  1});

    const auto back = mesh.addFace({v0, v1, v2, v3});
    const auto front = mesh.addFace({v5, v4, v7, v6});
    const auto left = mesh.addFace({v4, v0, v3, v7});
    const auto right = mesh.addFace({v1, v5, v6, v2});
    const auto top = mesh.addFace({v3, v2, v6, v7});
    const auto bottom = mesh.addFace({v4, v5, v1, v0});
    assert(back && front && left && right && top && bottom && mesh.validate());

    const std::vector<vortex::FaceId> unaffected{back, front, left, right, bottom};
    const auto sourceCorners = faceCorners(mesh, top);
    const auto boundaryEdge = mesh.edgeBetween(v3, v2);
    assert(boundaryEdge && mesh.radialCornerCount(boundaryEdge) == 2);

    vortex::MeshHistory history(256U * 1024U);
    vortex::ExtrudeFaceCommand extrude(top, {0.0F, 1.5F, 0.0F});
    vortex::MeshCommandResult result;
    assert(history.execute(mesh, extrude, &result));
    assert(result.extrusion);
    const auto created = *result.extrusion;
    assert(mesh.radialCornerCount(boundaryEdge) == 2);
    for (const auto face : unaffected) {
        assert(mesh.hasFace(face));
    }
    assert(mesh.validate());

    for (int cycle = 0; cycle < 16; ++cycle) {
        assert(history.undo(mesh));
        assert(mesh.hasFace(top));
        assert(faceCorners(mesh, top) == sourceCorners);
        assert(mesh.radialCornerCount(boundaryEdge) == 2);
        for (const auto face : unaffected) {
            assert(mesh.hasFace(face));
        }
        assert(mesh.validate());

        assert(history.redo(mesh));
        assert(!mesh.hasFace(top));
        assert(mesh.hasFace(created.capFace));
        assert(mesh.radialCornerCount(boundaryEdge) == 2);
        for (const auto face : unaffected) {
            assert(mesh.hasFace(face));
        }
        assert(mesh.validate());
    }
}

void testConcaveNgonHistory() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto b = mesh.addVertex({2.0F, 0.0F, 0.0F});
    const auto c = mesh.addVertex({2.0F, 0.0F, 2.0F});
    const auto d = mesh.addVertex({1.0F, 0.0F, 1.0F});
    const auto e = mesh.addVertex({0.0F, 0.0F, 2.0F});
    const auto source = mesh.addFace({a, b, c, d, e});
    assert(source && mesh.validate());
    const auto sourceCorners = faceCorners(mesh, source);

    vortex::MeshHistory history(128U * 1024U);
    vortex::ExtrudeFaceCommand extrude(source, {0.0F, 1.0F, 0.0F});
    vortex::MeshCommandResult result;
    assert(history.execute(mesh, extrude, &result));
    assert(result.extrusion && result.extrusion->sideFaces.size() == 5);
    const auto cap = result.extrusion->capFace;
    assert(mesh.validate());

    assert(history.undo(mesh));
    assert(mesh.hasFace(source));
    assert(faceCorners(mesh, source) == sourceCorners);
    assert(mesh.vertexCount() == 5);
    assert(mesh.edgeCount() == 5);
    assert(mesh.faceCount() == 1);
    assert(mesh.cornerCount() == 5);
    assert(mesh.validate());

    assert(history.redo(mesh));
    assert(!mesh.hasFace(source));
    assert(mesh.hasFace(cap));
    assert(mesh.vertexCount() == 10);
    assert(mesh.edgeCount() == 15);
    assert(mesh.faceCount() == 6);
    assert(mesh.cornerCount() == 25);
    assert(mesh.validate());
}

} // namespace

int main() {
    testStackedExtrusions();
    testAttachedCubeExtrudeHistory();
    testConcaveNgonHistory();

    std::cout << "Vortex3D topology history smoke test passed\n";
    return 0;
}
