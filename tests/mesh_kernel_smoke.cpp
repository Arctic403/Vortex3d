#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <iostream>
#include <unordered_set>

namespace {

std::size_t radialCount(const vortex::EditableMesh& mesh, const vortex::EdgeId edgeId) {
    const vortex::MeshEdge* edge = mesh.edge(edgeId);
    assert(edge != nullptr && edge->anyCorner);

    std::unordered_set<vortex::CornerId, vortex::IdHash<vortex::CornerId>> visited;
    vortex::CornerId cursor = edge->anyCorner;
    while (cursor && !visited.contains(cursor)) {
        visited.insert(cursor);
        const vortex::MeshCorner* corner = mesh.corner(cursor);
        assert(corner != nullptr && corner->edgeId == edgeId);
        cursor = corner->radialNext;
    }
    assert(cursor == edge->anyCorner);
    return visited.size();
}

void testSharedAndNonManifoldRadials() {
    vortex::EditableMesh mesh;
    const vortex::VertexId v0 = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    const vortex::VertexId v1 = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    const vortex::VertexId v2 = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    const vortex::VertexId v3 = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    assert(mesh.addFace({v0, v1, v2, v3}));

    const vortex::EdgeId shared = mesh.addEdge(v0, v1);
    assert(shared);
    assert(radialCount(mesh, shared) == 1);

    const vortex::VertexId v4 = mesh.addVertex({1.0F, 1.0F, -1.0F});
    const vortex::VertexId v5 = mesh.addVertex({-1.0F, 1.0F, -1.0F});
    assert(mesh.addFace({v1, v0, v5, v4}));
    assert(radialCount(mesh, shared) == 2);
    assert(mesh.validate());

    // Third polygon intentionally uses the same edge: legal non-manifold radial topology.
    const vortex::VertexId v6 = mesh.addVertex({0.0F, -1.0F, -2.0F});
    assert(mesh.addFace({v0, v1, v6}));
    assert(radialCount(mesh, shared) == 3);
    assert(mesh.validate());
}

void testConcaveNgon() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto b = mesh.addVertex({2.0F, 0.0F, 0.0F});
    const auto c = mesh.addVertex({2.0F, 0.0F, 2.0F});
    const auto d = mesh.addVertex({1.0F, 0.0F, 1.0F});
    const auto e = mesh.addVertex({0.0F, 0.0F, 2.0F});
    const auto face = mesh.addFace({a, b, c, d, e});
    assert(face);
    assert(mesh.face(face)->cornerCount == 5);
    assert(mesh.validate());
}

void testCubeFixture() {
    vortex::EditableMesh mesh;
    const auto v0 = mesh.addVertex({-1, -1, -1});
    const auto v1 = mesh.addVertex({ 1, -1, -1});
    const auto v2 = mesh.addVertex({ 1,  1, -1});
    const auto v3 = mesh.addVertex({-1,  1, -1});
    const auto v4 = mesh.addVertex({-1, -1,  1});
    const auto v5 = mesh.addVertex({ 1, -1,  1});
    const auto v6 = mesh.addVertex({ 1,  1,  1});
    const auto v7 = mesh.addVertex({-1,  1,  1});

    assert(mesh.addFace({v0, v1, v2, v3}));
    assert(mesh.addFace({v5, v4, v7, v6}));
    assert(mesh.addFace({v4, v0, v3, v7}));
    assert(mesh.addFace({v1, v5, v6, v2}));
    assert(mesh.addFace({v3, v2, v6, v7}));
    assert(mesh.addFace({v4, v5, v1, v0}));

    assert(mesh.vertexCount() == 8);
    assert(mesh.edgeCount() == 12);
    assert(mesh.faceCount() == 6);
    assert(mesh.cornerCount() == 24);
    assert(mesh.validate());
}

} // namespace

int main() {
    vortex::EditableMesh mesh;
    assert(mesh.validate());

    const vortex::VertexId v0 = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    const vortex::VertexId v1 = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    const vortex::VertexId v2 = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    const vortex::VertexId v3 = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    assert(v0 && v1 && v2 && v3);
    assert(mesh.vertexCount() == 4);

    const vortex::FaceId quad = mesh.addFace({v0, v1, v2, v3});
    assert(quad);
    assert(mesh.faceCount() == 1);
    assert(mesh.edgeCount() == 4);
    assert(mesh.cornerCount() == 4);
    assert(mesh.validate());

    // Pure coordinate edits retain topology identity.
    const auto p0 = mesh.position(v0);
    assert(p0 && p0->x == -1.0F && p0->z == -1.0F);
    assert(mesh.setPosition(v0, {-2.0F, 0.0F, -1.0F}));
    const auto moved = mesh.position(v0);
    assert(moved && moved->x == -2.0F);
    assert(mesh.vertex(v0) != nullptr);
    assert(mesh.validate());

    // Generic attributes track topology-domain growth automatically and allow the
    // same semantic name on different domains because domain is part of the key.
    assert(mesh.attributes().contains("position", vortex::AttributeDomain::Vertex));
    assert(mesh.attributes().contains("crease", vortex::AttributeDomain::Edge));
    assert(mesh.attributes().contains("material_index", vortex::AttributeDomain::Face));
    assert(mesh.attributes().contains("uv:Map", vortex::AttributeDomain::Corner));
    assert(mesh.attributes().create<vortex::Vec3>("normal", vortex::AttributeDomain::Vertex, {}));
    assert(mesh.attributes().contains("normal", vortex::AttributeDomain::Vertex));
    assert(mesh.attributes().contains("normal", vortex::AttributeDomain::Corner));
    assert(mesh.attributes().domainSize(vortex::AttributeDomain::Vertex) == mesh.vertexCount());
    assert(mesh.attributes().domainSize(vortex::AttributeDomain::Corner) == mesh.cornerCount());
    assert(mesh.attributes().validateSizes());

    // Invalid topology requests fail without changing the valid mesh.
    const std::size_t faceCount = mesh.faceCount();
    assert(!mesh.addFace({v0, v1}));
    assert(!mesh.addFace({v0, v1, v1}));
    assert(mesh.faceCount() == faceCount);
    assert(mesh.validate());

    testSharedAndNonManifoldRadials();
    testConcaveNgon();
    testCubeFixture();

    std::cout << "Vortex3D Mesh Kernel v2 smoke test passed\n";
    return 0;
}
