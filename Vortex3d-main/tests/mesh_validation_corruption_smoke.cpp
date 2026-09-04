#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace vortex {

struct MeshValidationTestAccess final {
    static void breakFaceNext(EditableMesh& mesh, const CornerId cornerId) {
        mesh.corners_.at(cornerId).next = cornerId;
    }

    static void breakRadialNext(EditableMesh& mesh, const CornerId cornerId) {
        mesh.corners_.at(cornerId).radialNext = CornerId{999'999};
    }

    static void invalidateEdgeEndpoint(EditableMesh& mesh, const EdgeId edgeId) {
        mesh.edges_.at(edgeId).vertexA = VertexId{999'999};
    }

    static void shrinkFaceBelowPolygonMinimum(EditableMesh& mesh, const FaceId faceId) {
        mesh.faces_.at(faceId).cornerCount = 2;
    }

    [[nodiscard]] static CornerId addOrphanCorner(
        EditableMesh& mesh,
        const FaceId faceId,
        const VertexId vertexId) {
        const CornerId id = mesh.allocateId<CornerId>();
        const std::size_t packedIndex = mesh.cornerOrder_.size();
        mesh.cornerOrder_.push_back(id);
        mesh.cornerIndex_.emplace(id, packedIndex);
        mesh.corners_.emplace(id, MeshCorner{id, faceId, vertexId, {}, id, id, id, id});
        mesh.attributes_.setDomainSize(AttributeDomain::Corner, mesh.cornerOrder_.size());
        return id;
    }
};

} // namespace vortex

namespace {

struct QuadFixture final {
    vortex::EditableMesh mesh;
    vortex::VertexId a;
    vortex::VertexId b;
    vortex::VertexId c;
    vortex::VertexId d;
    vortex::FaceId face;
    vortex::EdgeId edgeAB;
    vortex::CornerId firstCorner;
};

[[nodiscard]] QuadFixture makeQuad() {
    QuadFixture fixture;
    fixture.a = fixture.mesh.addVertex({0.0F, 0.0F, 0.0F});
    fixture.b = fixture.mesh.addVertex({1.0F, 0.0F, 0.0F});
    fixture.c = fixture.mesh.addVertex({1.0F, 0.0F, 1.0F});
    fixture.d = fixture.mesh.addVertex({0.0F, 0.0F, 1.0F});
    fixture.face = fixture.mesh.addFace({fixture.a, fixture.b, fixture.c, fixture.d});
    fixture.edgeAB = fixture.mesh.edgeBetween(fixture.a, fixture.b);
    const vortex::MeshFace* face = fixture.mesh.face(fixture.face);
    assert(fixture.face && fixture.edgeAB && face != nullptr);
    fixture.firstCorner = face->firstCorner;
    assert(fixture.firstCorner && fixture.mesh.validate());
    return fixture;
}

[[nodiscard]] bool hasIssue(
    const vortex::MeshValidationResult& result,
    const vortex::MeshValidationCode code) {
    return std::any_of(result.issues.begin(), result.issues.end(), [code](const vortex::MeshValidationIssue& issue) {
        return issue.code == code;
    });
}

void testBrokenFaceCycleDiagnostic() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::breakFaceNext(fixture.mesh, fixture.firstCorner);
    const auto result = fixture.mesh.validate();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::BrokenFaceCycle));
}

void testBrokenRadialCycleDiagnostic() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::breakRadialNext(fixture.mesh, fixture.firstCorner);
    const auto result = fixture.mesh.validate();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::BrokenRadialCycle));
}

void testInvalidEdgeEndpointDiagnostic() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::invalidateEdgeEndpoint(fixture.mesh, fixture.edgeAB);
    const auto result = fixture.mesh.validate();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::InvalidEdgeEndpoints));
}

void testInvalidFaceSizeDiagnostic() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::shrinkFaceBelowPolygonMinimum(fixture.mesh, fixture.face);
    const auto result = fixture.mesh.validate();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::InvalidFaceSize));
}

void testOrphanCornerDiagnostic() {
    auto fixture = makeQuad();
    const vortex::CornerId orphan =
        vortex::MeshValidationTestAccess::addOrphanCorner(fixture.mesh, fixture.face, fixture.a);
    assert(orphan);
    const auto result = fixture.mesh.validate();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::UnreachableCorner));
}

void testAttributeSizeDiagnostic() {
    auto fixture = makeQuad();
    fixture.mesh.attributes().setDomainSize(
        vortex::AttributeDomain::Vertex,
        fixture.mesh.vertexCount() + 1U);
    const auto result = fixture.mesh.validate();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::AttributeSizeMismatch));
}

void testDuplicateAndInvalidEdgeCreationIsRejected() {
    auto fixture = makeQuad();
    const std::size_t edgeCount = fixture.mesh.edgeCount();

    const vortex::EdgeId reverseDuplicate = fixture.mesh.addEdge(fixture.b, fixture.a);
    assert(reverseDuplicate == fixture.edgeAB);
    assert(fixture.mesh.edgeCount() == edgeCount);

    assert(!fixture.mesh.addEdge(fixture.a, fixture.a));
    assert(!fixture.mesh.addEdge(fixture.a, vortex::VertexId{999'999}));
    assert(fixture.mesh.edgeCount() == edgeCount);
    assert(fixture.mesh.validate());
}

void testDeletionSequenceGuards() {
    auto fixture = makeQuad();
    const std::vector<vortex::EdgeId> edges(fixture.mesh.edgeIds().begin(), fixture.mesh.edgeIds().end());
    const std::vector<vortex::VertexId> vertices(fixture.mesh.vertexIds().begin(), fixture.mesh.vertexIds().end());

    assert(!fixture.mesh.removeEdge(fixture.edgeAB));
    assert(!fixture.mesh.removeVertex(fixture.a));
    assert(!fixture.mesh.removeFace(vortex::FaceId{999'999}));
    assert(fixture.mesh.validate());

    assert(fixture.mesh.removeFace(fixture.face, false));
    assert(fixture.mesh.validate());
    for (const vortex::EdgeId edgeId : edges) {
        assert(fixture.mesh.removeEdge(edgeId));
    }
    for (const vortex::VertexId vertexId : vertices) {
        assert(fixture.mesh.removeVertex(vertexId));
    }
    assert(fixture.mesh.vertexCount() == 0);
    assert(fixture.mesh.edgeCount() == 0);
    assert(fixture.mesh.faceCount() == 0);
    assert(fixture.mesh.cornerCount() == 0);
    assert(fixture.mesh.validate());
}

} // namespace

int main() {
    testBrokenFaceCycleDiagnostic();
    testBrokenRadialCycleDiagnostic();
    testInvalidEdgeEndpointDiagnostic();
    testInvalidFaceSizeDiagnostic();
    testOrphanCornerDiagnostic();
    testAttributeSizeDiagnostic();
    testDuplicateAndInvalidEdgeCreationIsRejected();
    testDeletionSequenceGuards();

    std::cout << "Vortex3D deliberate topology corruption diagnostics passed\n";
    return 0;
}
