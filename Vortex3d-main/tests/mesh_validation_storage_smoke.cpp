#include "vortex/mesh/editable_mesh.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace vortex {

struct MeshValidationTestAccess final {
    static void duplicateVertexOrderId(EditableMesh& mesh) {
        assert(mesh.vertexOrder_.size() >= 2U);
        mesh.vertexOrder_[1] = mesh.vertexOrder_[0];
    }

    static void corruptVertexIndex(EditableMesh& mesh, const VertexId id) {
        mesh.vertexIndex_.at(id) = mesh.vertexOrder_.size() + 7U;
    }

    static void addRegistryOnlyVertex(EditableMesh& mesh) {
        const VertexId id{999'991};
        mesh.vertices_.emplace(id, MeshVertex{id});
    }

    static void mismatchVertexRecordIdentity(EditableMesh& mesh, const VertexId id) {
        mesh.vertices_.at(id).id = VertexId{999'992};
    }

    static void duplicateEdgeEndpoints(EditableMesh& mesh) {
        assert(mesh.edgeOrder_.size() >= 2U);
        MeshEdge& first = mesh.edges_.at(mesh.edgeOrder_[0]);
        MeshEdge& second = mesh.edges_.at(mesh.edgeOrder_[1]);
        second.vertexA = first.vertexA;
        second.vertexB = first.vertexB;
    }

    static void rewindAllocator(EditableMesh& mesh) {
        mesh.nextElementId_ = 1U;
    }

    static void makeShortFaceCycle(EditableMesh& mesh, const FaceId faceId) {
        const MeshFace& face = mesh.faces_.at(faceId);
        const CornerId first = face.firstCorner;
        const CornerId second = mesh.corners_.at(first).next;
        mesh.corners_.at(first).next = second;
        mesh.corners_.at(first).prev = second;
        mesh.corners_.at(second).next = first;
        mesh.corners_.at(second).prev = first;
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
};

[[nodiscard]] QuadFixture makeQuad() {
    QuadFixture fixture;
    fixture.a = fixture.mesh.addVertex({0.0F, 0.0F, 0.0F});
    fixture.b = fixture.mesh.addVertex({1.0F, 0.0F, 0.0F});
    fixture.c = fixture.mesh.addVertex({1.0F, 1.0F, 0.0F});
    fixture.d = fixture.mesh.addVertex({0.0F, 1.0F, 0.0F});
    fixture.face = fixture.mesh.addFace({fixture.a, fixture.b, fixture.c, fixture.d});
    fixture.edgeAB = fixture.mesh.edgeBetween(fixture.a, fixture.b);
    assert(fixture.face && fixture.edgeAB);
    assert(fixture.mesh.validateStrict());
    return fixture;
}

[[nodiscard]] bool hasIssue(
    const vortex::MeshValidationResult& result,
    const vortex::MeshValidationCode code) {
    return std::any_of(result.issues.begin(), result.issues.end(), [code](const vortex::MeshValidationIssue& issue) {
        return issue.code == code;
    });
}

void testPackedOrderDuplicate() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::duplicateVertexOrderId(fixture.mesh);
    const auto result = fixture.mesh.validateStrict();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::DuplicateElementId));
    assert(hasIssue(result, vortex::MeshValidationCode::IndexMapMismatch));
}

void testIndexMapMismatch() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::corruptVertexIndex(fixture.mesh, fixture.a);
    const auto result = fixture.mesh.validateStrict();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::IndexMapMismatch));
}

void testRegistrySizeMismatch() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::addRegistryOnlyVertex(fixture.mesh);
    const auto result = fixture.mesh.validateStrict();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::StorageSizeMismatch));
    assert(hasIssue(result, vortex::MeshValidationCode::IndexMapMismatch));
}

void testRegistryRecordIdentityMismatch() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::mismatchVertexRecordIdentity(fixture.mesh, fixture.a);
    const auto result = fixture.mesh.validateStrict();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::ElementIdentityMismatch));
}

void testDuplicateUndirectedEdgeDiagnostic() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::duplicateEdgeEndpoints(fixture.mesh);
    const auto result = fixture.mesh.validateStrict();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::DuplicateEdge));
}

void testAllocatorMonotonicityDiagnostic() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::rewindAllocator(fixture.mesh);
    const auto result = fixture.mesh.validateStrict();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::InvalidAllocatorState));
}

void testStrictFaceCycleUniqueness() {
    auto fixture = makeQuad();
    vortex::MeshValidationTestAccess::makeShortFaceCycle(fixture.mesh, fixture.face);
    const auto result = fixture.mesh.validateStrict();
    assert(!result);
    assert(hasIssue(result, vortex::MeshValidationCode::BrokenFaceCycle));
}

void testSupportedMutationRemainsStrictlyValid() {
    auto fixture = makeQuad();
    const auto split = fixture.mesh.splitEdge(fixture.edgeAB, 0.5F);
    assert(split.has_value());
    assert(fixture.mesh.validateStrict());

    const auto extrusion = fixture.mesh.extrudeFace(fixture.face, {0.0F, 0.0F, 1.0F});
    assert(extrusion.has_value());
    assert(fixture.mesh.validateStrict());
}

} // namespace

int main() {
    testPackedOrderDuplicate();
    testIndexMapMismatch();
    testRegistrySizeMismatch();
    testRegistryRecordIdentityMismatch();
    testDuplicateUndirectedEdgeDiagnostic();
    testAllocatorMonotonicityDiagnostic();
    testStrictFaceCycleUniqueness();
    testSupportedMutationRemainsStrictlyValid();

    std::cout << "Vortex3D strict authored mesh storage validation passed\n";
    return 0;
}
