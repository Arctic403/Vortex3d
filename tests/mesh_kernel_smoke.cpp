#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <iostream>

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

    const auto p0 = mesh.position(v0);
    assert(p0 && p0->x == -1.0F && p0->z == -1.0F);
    assert(mesh.setPosition(v0, {-2.0F, 0.0F, -1.0F}));
    const auto moved = mesh.position(v0);
    assert(moved && moved->x == -2.0F);
    assert(mesh.validate());

    // A second face sharing an edge must reuse that edge and form a radial cycle.
    const vortex::VertexId v4 = mesh.addVertex({1.0F, 1.0F, -1.0F});
    const vortex::VertexId v5 = mesh.addVertex({-1.0F, 1.0F, -1.0F});
    const std::size_t beforeEdges = mesh.edgeCount();
    const vortex::FaceId side = mesh.addFace({v1, v0, v5, v4});
    assert(side);
    assert(mesh.edgeCount() == beforeEdges + 3);
    assert(mesh.cornerCount() == 8);
    assert(mesh.validate());

    // Generic attributes track topology-domain growth automatically.
    assert(mesh.attributes().contains("position"));
    assert(mesh.attributes().contains("crease"));
    assert(mesh.attributes().contains("material_index"));
    assert(mesh.attributes().contains("uv:Map"));
    assert(mesh.attributes().domainSize(vortex::AttributeDomain::Vertex) == mesh.vertexCount());
    assert(mesh.attributes().domainSize(vortex::AttributeDomain::Corner) == mesh.cornerCount());
    assert(mesh.attributes().validateSizes());

    // Invalid topology requests fail without damaging the valid mesh.
    const std::size_t faceCount = mesh.faceCount();
    assert(!mesh.addFace({v0, v1}));
    assert(!mesh.addFace({v0, v1, v1}));
    assert(mesh.faceCount() == faceCount);
    assert(mesh.validate());

    std::cout << "Vortex3D Mesh Kernel v2 smoke test passed\n";
    return 0;
}
