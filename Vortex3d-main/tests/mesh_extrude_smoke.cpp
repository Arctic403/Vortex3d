#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

void testIsolatedQuadExtrude() {
    vortex::EditableMesh mesh;
    const auto v0 = mesh.addVertex({-1.0F, 0.0F, -1.0F});
    const auto v1 = mesh.addVertex({ 1.0F, 0.0F, -1.0F});
    const auto v2 = mesh.addVertex({ 1.0F, 0.0F,  1.0F});
    const auto v3 = mesh.addVertex({-1.0F, 0.0F,  1.0F});
    const auto face = mesh.addFace({v0, v1, v2, v3});
    assert(face);

    auto* materials = mesh.attributes().values<std::int32_t>("material_index", vortex::AttributeDomain::Face);
    assert(materials != nullptr && materials->size() == 1);
    (*materials)[0] = 7;

    auto* uvs = mesh.attributes().values<vortex::Vec2>("uv:Map", vortex::AttributeDomain::Corner);
    assert(uvs != nullptr && uvs->size() == 4);
    (*uvs)[0] = {0.0F, 0.0F};
    (*uvs)[1] = {1.0F, 0.0F};
    (*uvs)[2] = {1.0F, 1.0F};
    (*uvs)[3] = {0.0F, 1.0F};

    const auto result = mesh.extrudeFace(face, {0.0F, 2.0F, 0.0F});
    assert(result);
    assert(result->sourceFace == face);
    assert(!mesh.hasFace(face));
    assert(mesh.hasFace(result->capFace));
    assert(result->newVertices.size() == 4);
    assert(result->sideFaces.size() == 4);

    assert(mesh.vertexCount() == 8);
    assert(mesh.edgeCount() == 12);
    assert(mesh.faceCount() == 5);
    assert(mesh.cornerCount() == 20);
    assert(mesh.validate());

    for (const auto vertexId : result->newVertices) {
        const auto position = mesh.position(vertexId);
        assert(position && position->y == 2.0F);
    }

    const auto* extrudedMaterials = mesh.attributes().values<std::int32_t>("material_index", vortex::AttributeDomain::Face);
    assert(extrudedMaterials != nullptr && extrudedMaterials->size() == 5);
    for (const std::int32_t material : *extrudedMaterials) {
        assert(material == 7);
    }

    // Source corner UVs are inherited by the new cap before the side-wall policy is applied.
    const auto* extrudedUvs = mesh.attributes().values<vortex::Vec2>("uv:Map", vortex::AttributeDomain::Corner);
    assert(extrudedUvs != nullptr && extrudedUvs->size() == 20);
    assert(((*extrudedUvs)[0] == vortex::Vec2{0.0F, 0.0F}));
    assert(((*extrudedUvs)[1] == vortex::Vec2{1.0F, 0.0F}));
    assert(((*extrudedUvs)[2] == vortex::Vec2{1.0F, 1.0F}));
    assert(((*extrudedUvs)[3] == vortex::Vec2{0.0F, 1.0F}));
}

void testAttachedCubeFaceExtrude() {
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
    const auto top = mesh.addFace({v3, v2, v6, v7});
    assert(top);
    assert(mesh.addFace({v4, v5, v1, v0}));
    assert(mesh.validate());

    const auto originalBoundary = mesh.edgeBetween(v3, v2);
    assert(originalBoundary && mesh.radialCornerCount(originalBoundary) == 2);

    const auto result = mesh.extrudeFace(top, {0.0F, 1.5F, 0.0F});
    assert(result);
    assert(!mesh.hasFace(top));
    assert(mesh.hasFace(result->capFace));
    assert(result->sideFaces.size() == 4);

    assert(mesh.vertexCount() == 12);
    assert(mesh.edgeCount() == 20);
    assert(mesh.faceCount() == 10);
    assert(mesh.cornerCount() == 40);
    assert(mesh.hasEdge(originalBoundary));
    assert(mesh.radialCornerCount(originalBoundary) == 2);
    assert(mesh.validate());
}

void testConcaveNgonExtrude() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto b = mesh.addVertex({2.0F, 0.0F, 0.0F});
    const auto c = mesh.addVertex({2.0F, 0.0F, 2.0F});
    const auto d = mesh.addVertex({1.0F, 0.0F, 1.0F});
    const auto e = mesh.addVertex({0.0F, 0.0F, 2.0F});
    const auto face = mesh.addFace({a, b, c, d, e});
    assert(face && mesh.validate());

    const auto result = mesh.extrudeFace(face, {0.0F, 1.0F, 0.0F});
    assert(result);
    assert(result->newVertices.size() == 5);
    assert(result->sideFaces.size() == 5);
    assert(mesh.vertexCount() == 10);
    assert(mesh.edgeCount() == 15);
    assert(mesh.faceCount() == 6);
    assert(mesh.cornerCount() == 25);
    assert(mesh.validate());
}

} // namespace

int main() {
    testIsolatedQuadExtrude();
    testAttachedCubeFaceExtrude();
    testConcaveNgonExtrude();

    std::cout << "Vortex3D face extrude smoke test passed\n";
    return 0;
}
