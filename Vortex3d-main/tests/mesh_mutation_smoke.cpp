#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool nearlyEqual(const float a, const float b) {
    return std::fabs(a - b) < 0.0001F;
}

void testVertexAndEdgeRemovalCompaction() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto b = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const auto c = mesh.addVertex({2.0F, 0.0F, 0.0F});

    assert(mesh.attributes().create<float>("weight", vortex::AttributeDomain::Vertex, -1.0F));
    auto* weights = mesh.attributes().values<float>("weight", vortex::AttributeDomain::Vertex);
    assert(weights != nullptr && weights->size() == 3);
    (*weights)[0] = 10.0F;
    (*weights)[1] = 20.0F;
    (*weights)[2] = 30.0F;

    assert(mesh.removeVertex(b));
    assert(mesh.hasVertex(a));
    assert(!mesh.hasVertex(b));
    assert(mesh.hasVertex(c));

    weights = mesh.attributes().values<float>("weight", vortex::AttributeDomain::Vertex);
    assert(weights != nullptr && weights->size() == 2);
    assert(nearlyEqual((*weights)[0], 10.0F));
    assert(nearlyEqual((*weights)[1], 30.0F));
    const auto cPosition = mesh.position(c);
    assert(cPosition && nearlyEqual(cPosition->x, 2.0F));

    const auto wire = mesh.addEdge(a, c);
    assert(wire);
    assert(!mesh.removeVertex(a));
    assert(!mesh.removeVertex(c));
    assert(mesh.removeEdge(wire));
    assert(mesh.removeVertex(a));
    assert(mesh.removeVertex(c));
    assert(mesh.vertexCount() == 0);
    assert(mesh.edgeCount() == 0);
    assert(mesh.validate());
}

void testFaceRemovalCompaction() {
    vortex::EditableMesh mesh;
    const auto a0 = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto a1 = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const auto a2 = mesh.addVertex({0.0F, 1.0F, 0.0F});
    const auto b0 = mesh.addVertex({3.0F, 0.0F, 0.0F});
    const auto b1 = mesh.addVertex({4.0F, 0.0F, 0.0F});
    const auto b2 = mesh.addVertex({3.0F, 1.0F, 0.0F});

    const auto first = mesh.addFace({a0, a1, a2});
    const auto second = mesh.addFace({b0, b1, b2});
    assert(first && second);
    assert(mesh.edgeCount() == 6);
    assert(mesh.cornerCount() == 6);

    auto* materials = mesh.attributes().values<std::int32_t>("material_index", vortex::AttributeDomain::Face);
    assert(materials != nullptr && materials->size() == 2);
    (*materials)[0] = 11;
    (*materials)[1] = 22;

    assert(mesh.removeFace(first));
    assert(!mesh.hasFace(first));
    assert(mesh.hasFace(second));
    assert(mesh.faceCount() == 1);
    assert(mesh.cornerCount() == 3);
    assert(mesh.edgeCount() == 3);

    materials = mesh.attributes().values<std::int32_t>("material_index", vortex::AttributeDomain::Face);
    assert(materials != nullptr && materials->size() == 1);
    assert((*materials)[0] == 22);
    assert(mesh.validate());
}

void testSharedEdgeSplit() {
    vortex::EditableMesh mesh;
    const auto v0 = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto v1 = mesh.addVertex({2.0F, 0.0F, 0.0F});
    const auto v2 = mesh.addVertex({2.0F, 1.0F, 0.0F});
    const auto v3 = mesh.addVertex({0.0F, 1.0F, 0.0F});
    const auto v4 = mesh.addVertex({1.0F, -1.0F, 0.0F});

    const auto top = mesh.addFace({v0, v1, v2, v3});
    const auto bottom = mesh.addFace({v1, v0, v4});
    assert(top && bottom);

    const vortex::EdgeId shared = mesh.edgeBetween(v0, v1);
    assert(shared);
    assert(mesh.radialCornerCount(shared) == 2);

    auto* creases = mesh.attributes().values<float>("crease", vortex::AttributeDomain::Edge);
    assert(creases != nullptr && !creases->empty());
    (*creases)[0] = 0.75F; // v0-v1 is the first edge created by the first face.

    assert(mesh.attributes().create<float>("weight", vortex::AttributeDomain::Vertex, 0.0F));
    auto* weights = mesh.attributes().values<float>("weight", vortex::AttributeDomain::Vertex);
    assert(weights != nullptr && weights->size() == 5);
    (*weights)[0] = 0.0F;
    (*weights)[1] = 10.0F;

    const std::size_t verticesBefore = mesh.vertexCount();
    const std::size_t edgesBefore = mesh.edgeCount();
    const std::size_t cornersBefore = mesh.cornerCount();

    assert(!mesh.splitEdge(shared, 0.0F));
    assert(!mesh.splitEdge(shared, 1.0F));

    const auto split = mesh.splitEdge(shared, 0.25F);
    assert(split);
    assert(split->retainedEdge == shared);
    assert(split->newVertex);
    assert(split->newEdge);
    assert(split->insertedCornerCount == 2);

    assert(mesh.vertexCount() == verticesBefore + 1);
    assert(mesh.edgeCount() == edgesBefore + 1);
    assert(mesh.cornerCount() == cornersBefore + 2);
    assert(mesh.face(top)->cornerCount == 5);
    assert(mesh.face(bottom)->cornerCount == 4);
    assert(mesh.radialCornerCount(split->retainedEdge) == 2);
    assert(mesh.radialCornerCount(split->newEdge) == 2);

    const auto splitPosition = mesh.position(split->newVertex);
    assert(splitPosition && nearlyEqual(splitPosition->x, 0.5F));

    const vortex::MeshEdge* retained = mesh.edge(split->retainedEdge);
    const vortex::MeshEdge* created = mesh.edge(split->newEdge);
    assert(retained != nullptr && created != nullptr);
    assert(retained->vertexA == v0);
    assert(retained->vertexB == split->newVertex);
    assert(created->vertexA == split->newVertex);
    assert(created->vertexB == v1);

    weights = mesh.attributes().values<float>("weight", vortex::AttributeDomain::Vertex);
    assert(weights != nullptr && nearlyEqual(weights->back(), 2.5F));
    creases = mesh.attributes().values<float>("crease", vortex::AttributeDomain::Edge);
    assert(creases != nullptr && nearlyEqual((*creases)[0], 0.75F));
    assert(nearlyEqual(creases->back(), 0.75F));
    assert(mesh.validate());
}

void testNonManifoldEdgeSplit() {
    vortex::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto b = mesh.addVertex({2.0F, 0.0F, 0.0F});
    const auto c0 = mesh.addVertex({1.0F, 1.0F, 0.0F});
    const auto c1 = mesh.addVertex({1.0F, -1.0F, 0.0F});
    const auto c2 = mesh.addVertex({1.0F, 0.0F, 1.0F});

    const auto f0 = mesh.addFace({a, b, c0});
    const auto f1 = mesh.addFace({b, a, c1});
    const auto f2 = mesh.addFace({a, b, c2});
    assert(f0 && f1 && f2);

    const auto shared = mesh.edgeBetween(a, b);
    assert(shared && mesh.radialCornerCount(shared) == 3);

    const auto split = mesh.splitEdge(shared, 0.5F);
    assert(split && split->insertedCornerCount == 3);
    assert(mesh.radialCornerCount(split->retainedEdge) == 3);
    assert(mesh.radialCornerCount(split->newEdge) == 3);
    assert(mesh.face(f0)->cornerCount == 4);
    assert(mesh.face(f1)->cornerCount == 4);
    assert(mesh.face(f2)->cornerCount == 4);
    assert(mesh.validate());
}

} // namespace

int main() {
    testVertexAndEdgeRemovalCompaction();
    testFaceRemovalCompaction();
    testSharedEdgeSplit();
    testNonManifoldEdgeSplit();
    std::cout << "Vortex3D Mesh Mutation smoke test passed\n";
    return 0;
}
