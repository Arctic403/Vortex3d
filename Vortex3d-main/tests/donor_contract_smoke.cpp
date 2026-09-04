#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    vortex::EditableMesh mesh;
    const auto v0 = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    const auto v1 = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    const auto v2 = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    const auto v3 = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    const auto face = mesh.addFace({v0, v1, v2, v3});
    assert(face);

    // Donor behavior: a polygon remains a real authoring face with explicit corners/loops.
    assert(mesh.vertexCount() == 4);
    assert(mesh.edgeCount() == 4);
    assert(mesh.faceCount() == 1);
    assert(mesh.cornerCount() == 4);
    assert(mesh.face(face)->cornerCount == 4);

    const auto firstEdge = mesh.edgeBetween(v0, v1);
    assert(firstEdge);
    assert(mesh.radialCornerCount(firstEdge) == 1);

    // Donor behavior: coordinate edits do not replace persistent topology identities.
    const auto originalVertexId = v0;
    const auto originalEdgeId = firstEdge;
    const auto originalFaceId = face;
    assert(mesh.setPosition(v0, {-1.25F, 0.0F, -1.0F}));
    assert(mesh.hasVertex(originalVertexId));
    assert(mesh.hasEdge(originalEdgeId));
    assert(mesh.hasFace(originalFaceId));

    // Donor behavior: edge flags, material assignment and corner UV authoring live on topology domains.
    auto* seam = mesh.attributes().values<bool>("seam", vortex::AttributeDomain::Edge);
    auto* sharp = mesh.attributes().values<bool>("sharp", vortex::AttributeDomain::Edge);
    auto* crease = mesh.attributes().values<float>("crease", vortex::AttributeDomain::Edge);
    auto* material = mesh.attributes().values<std::int32_t>("material_index", vortex::AttributeDomain::Face);
    auto* uv = mesh.attributes().values<vortex::Vec2>("uv:Map", vortex::AttributeDomain::Corner);
    assert(seam && sharp && crease && material && uv);
    assert(seam->size() == 4 && sharp->size() == 4 && crease->size() == 4);
    assert(material->size() == 1 && uv->size() == 4);

    (*seam)[0] = true;
    (*sharp)[0] = true;
    (*crease)[0] = 0.5F;
    (*material)[0] = 1;
    (*uv)[0] = {0.125F, 0.875F};
    assert(mesh.validate());

    // The new kernel goes further: splitting the authored edge preserves its ID and copies its flags.
    const auto split = mesh.splitEdge(firstEdge, 0.5F);
    assert(split);
    assert(split->retainedEdge == firstEdge);
    assert(mesh.hasEdge(firstEdge));
    assert(mesh.hasEdge(split->newEdge));

    seam = mesh.attributes().values<bool>("seam", vortex::AttributeDomain::Edge);
    sharp = mesh.attributes().values<bool>("sharp", vortex::AttributeDomain::Edge);
    crease = mesh.attributes().values<float>("crease", vortex::AttributeDomain::Edge);
    assert(seam && sharp && crease);
    assert(seam->back());
    assert(sharp->back());
    assert(crease->back() == 0.5F);
    assert(mesh.validate());

    std::cout << "Vortex3D donor behavior contract test passed\n";
    return 0;
}
