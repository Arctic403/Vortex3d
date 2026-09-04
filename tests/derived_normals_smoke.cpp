#include "vortex/core/document.hpp"
#include "vortex/eval/evaluation_cache.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using namespace vortex;

[[nodiscard]] bool near(const float left, const float right, const float tolerance = 1.0e-4F) noexcept {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool nearVector(const Vec3 value, const Vec3 expected, const float tolerance = 1.0e-4F) noexcept {
    return near(value.x, expected.x, tolerance) && near(value.y, expected.y, tolerance) &&
           near(value.z, expected.z, tolerance);
}

[[nodiscard]] Vec3 normalForFaceVertex(const EvaluatedMesh& mesh, const FaceId faceId, const VertexId vertexId) {
    const auto* normals = mesh.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(normals != nullptr && normals->size() == mesh.cornerCount());

    for (const EvaluatedFace& face : mesh.faces()) {
        if (face.sourceId != faceId) {
            continue;
        }
        EvaluatedMesh::Index cursor = face.firstCorner;
        for (std::uint32_t step = 0; step < face.cornerCount; ++step) {
            const EvaluatedCorner& corner = mesh.corners()[cursor];
            assert(corner.vertex < mesh.vertexCount());
            if (mesh.vertices()[corner.vertex].sourceId == vertexId) {
                return (*normals)[cursor];
            }
            cursor = corner.next;
        }
    }
    assert(false);
    return {};
}

void makeAllFacesSmooth(EditableMesh& mesh) {
    if (!mesh.attributes().contains("sharp_face", AttributeDomain::Face)) {
        assert(mesh.attributes().create<bool>("sharp_face", AttributeDomain::Face, false));
        return;
    }
    auto* flags = mesh.attributes().values<bool>("sharp_face", AttributeDomain::Face);
    assert(flags != nullptr);
    for (std::size_t index = 0; index < flags->size(); ++index) {
        (*flags)[index] = false;
    }
}

void testFlatAndReversedWinding() {
    EditableMesh forward;
    const VertexId a = forward.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = forward.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = forward.addVertex({0.0F, 1.0F, 0.0F});
    const FaceId face = forward.addFace({a, b, c});
    assert(face);

    Document document;
    const MeshId forwardId = document.createMesh("Forward", std::move(forward));
    const MeshEvaluationResult forwardResult = MeshEvaluator::evaluate(*document.mesh(forwardId));
    assert(forwardResult && forwardResult.mesh.has_value());
    assert(nearVector(normalForFaceVertex(*forwardResult.mesh, face, a), {0.0F, 0.0F, 1.0F}));

    EditableMesh reversed;
    const VertexId ra = reversed.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId rb = reversed.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId rc = reversed.addVertex({0.0F, 1.0F, 0.0F});
    const FaceId reversedFace = reversed.addFace({ra, rc, rb});
    assert(reversedFace);
    const MeshId reversedId = document.createMesh("Reversed", std::move(reversed));
    const MeshEvaluationResult reversedResult = MeshEvaluator::evaluate(*document.mesh(reversedId));
    assert(reversedResult && reversedResult.mesh.has_value());
    assert(nearVector(normalForFaceVertex(*reversedResult.mesh, reversedFace, ra), {0.0F, 0.0F, -1.0F}));
}

void testSmoothCube() {
    EditableMesh cube;
    const VertexId v000 = cube.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId v100 = cube.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId v110 = cube.addVertex({1.0F, 1.0F, 0.0F});
    const VertexId v010 = cube.addVertex({0.0F, 1.0F, 0.0F});
    const VertexId v001 = cube.addVertex({0.0F, 0.0F, 1.0F});
    const VertexId v101 = cube.addVertex({1.0F, 0.0F, 1.0F});
    const VertexId v111 = cube.addVertex({1.0F, 1.0F, 1.0F});
    const VertexId v011 = cube.addVertex({0.0F, 1.0F, 1.0F});

    assert(cube.addFace({v000, v010, v110, v100}));
    assert(cube.addFace({v001, v101, v111, v011}));
    assert(cube.addFace({v000, v100, v101, v001}));
    assert(cube.addFace({v010, v011, v111, v110}));
    assert(cube.addFace({v000, v001, v011, v010}));
    assert(cube.addFace({v100, v110, v111, v101}));
    makeAllFacesSmooth(cube);
    assert(cube.validate());

    Document document;
    const MeshId meshId = document.createMesh("Smooth Cube", std::move(cube));
    const MeshEvaluationResult result = MeshEvaluator::evaluate(*document.mesh(meshId));
    assert(result && result.mesh.has_value());
    const EvaluatedMesh& evaluated = *result.mesh;
    const auto* normals = evaluated.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(normals != nullptr && normals->size() == 24U);

    constexpr float diagonal = 0.57735026919F;
    std::size_t matchingCorners = 0U;
    for (std::size_t index = 0; index < evaluated.cornerCount(); ++index) {
        const EvaluatedCorner& corner = evaluated.corners()[index];
        if (evaluated.vertices()[corner.vertex].sourceId == v000) {
            assert(nearVector((*normals)[index], {-diagonal, -diagonal, -diagonal}));
            ++matchingCorners;
        }
    }
    assert(matchingCorners == 3U);
}

void testShadingCommandsAndCache() {
    EditableMesh authored;
    const VertexId a = authored.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = authored.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = authored.addVertex({0.0F, 1.0F, 0.0F});
    const VertexId d = authored.addVertex({0.0F, 0.0F, 1.0F});
    const FaceId faceZ = authored.addFace({a, b, c});
    const FaceId faceY = authored.addFace({b, a, d});
    const EdgeId shared = authored.edgeBetween(a, b);
    assert(faceZ && faceY && shared);

    Document document;
    const MeshId meshId = document.createMesh("Shading Commands", std::move(authored));
    MeshHistory history;

    SetFaceSharpCommand smoothZ(faceZ, false);
    SetFaceSharpCommand smoothY(faceY, false);
    assert(document.executeMeshCommand(meshId, history, smoothZ));
    assert(document.executeMeshCommand(meshId, history, smoothY));

    EvaluationCache cache;
    const CachedEvaluationResult smoothResult = cache.evaluate(*document.mesh(meshId));
    assert(smoothResult && !smoothResult.cacheHit);
    const CachedEvaluationResult smoothHit = cache.evaluate(*document.mesh(meshId));
    assert(smoothHit && smoothHit.cacheHit);
    assert(smoothHit.mesh.get() == smoothResult.mesh.get());

    constexpr float diagonal = 0.70710678118F;
    assert(nearVector(normalForFaceVertex(*smoothResult.mesh, faceZ, a), {0.0F, diagonal, diagonal}));
    assert(nearVector(normalForFaceVertex(*smoothResult.mesh, faceY, a), {0.0F, diagonal, diagonal}));

    const std::uint64_t smoothRevision = document.mesh(meshId)->revision;
    SetEdgeSharpCommand hardEdge(shared, true);
    MeshCommandResult commandResult;
    assert(document.executeMeshCommand(meshId, history, hardEdge, &commandResult));
    assert(commandResult.touchedEdges.size() == 1U && commandResult.touchedEdges[0] == shared);
    assert(document.mesh(meshId)->revision > smoothRevision);

    const CachedEvaluationResult hardResult = cache.evaluate(*document.mesh(meshId));
    assert(hardResult && !hardResult.cacheHit);
    assert(hardResult.mesh->cacheKey() != smoothResult.mesh->cacheKey());
    assert(nearVector(normalForFaceVertex(*hardResult.mesh, faceZ, a), {0.0F, 0.0F, 1.0F}));
    assert(nearVector(normalForFaceVertex(*hardResult.mesh, faceY, a), {0.0F, 1.0F, 0.0F}));

    // The old immutable snapshot remains smooth even after the authored shading state changes.
    assert(nearVector(normalForFaceVertex(*smoothResult.mesh, faceZ, a), {0.0F, diagonal, diagonal}));

    assert(document.undoMeshCommand(meshId, history));
    const CachedEvaluationResult undoResult = cache.evaluate(*document.mesh(meshId));
    assert(undoResult && !undoResult.cacheHit);
    assert(nearVector(normalForFaceVertex(*undoResult.mesh, faceZ, a), {0.0F, diagonal, diagonal}));

    assert(document.redoMeshCommand(meshId, history));
    const CachedEvaluationResult redoResult = cache.evaluate(*document.mesh(meshId));
    assert(redoResult && !redoResult.cacheHit);
    assert(nearVector(normalForFaceVertex(*redoResult.mesh, faceZ, a), {0.0F, 0.0F, 1.0F}));
}

void testMirrorWeldAndTriangulate() {
    EditableMesh halfQuad;
    const VertexId a = halfQuad.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = halfQuad.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = halfQuad.addVertex({1.0F, 1.0F, 0.0F});
    const VertexId d = halfQuad.addVertex({0.0F, 1.0F, 0.0F});
    assert(halfQuad.addFace({a, b, c, d}));
    makeAllFacesSmooth(halfQuad);

    Document document;
    const MeshId meshId = document.createMesh("Mirror Triangulate", std::move(halfQuad));
    MirrorModifier mirror(MirrorAxis::X, 0.0F, MirrorWeldSettings{true, 0.0F});
    TriangulateModifier triangulate;
    const MeshModifier* stack[] = {&mirror, &triangulate};
    const MeshEvaluationResult result = MeshEvaluator::evaluate(*document.mesh(meshId), stack);
    assert(result && result.mesh.has_value());
    assert(result.mesh->faceCount() == 4U);
    const auto* normals = result.mesh->attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(normals != nullptr && normals->size() == result.mesh->cornerCount());
    for (const Vec3 normal : *normals) {
        assert(nearVector(normal, {0.0F, 0.0F, 1.0F}));
    }
}

void testNonManifoldBoundaryStopsSmoothing() {
    EditableMesh mesh;
    const VertexId a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = mesh.addVertex({0.0F, 1.0F, 0.0F});
    const VertexId d = mesh.addVertex({0.0F, 0.0F, 1.0F});
    const VertexId e = mesh.addVertex({0.0F, -1.0F, 0.0F});
    const FaceId first = mesh.addFace({a, b, c});
    const FaceId second = mesh.addFace({b, a, d});
    const FaceId third = mesh.addFace({a, b, e});
    assert(first && second && third);
    makeAllFacesSmooth(mesh);
    assert(mesh.radialCornerCount(mesh.edgeBetween(a, b)) == 3U);

    Document document;
    const MeshId meshId = document.createMesh("Non Manifold", std::move(mesh));
    const MeshEvaluationResult result = MeshEvaluator::evaluate(*document.mesh(meshId));
    assert(result && result.mesh.has_value());
    assert(nearVector(normalForFaceVertex(*result.mesh, first, a), {0.0F, 0.0F, 1.0F}));
    assert(nearVector(normalForFaceVertex(*result.mesh, second, a), {0.0F, 1.0F, 0.0F}));
    assert(nearVector(normalForFaceVertex(*result.mesh, third, a), {0.0F, 0.0F, -1.0F}));
}

void testNonUniformTransformAndDegenerateFailure() {
    EditableMesh mesh;
    const VertexId a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = mesh.addVertex({0.0F, 1.0F, 1.0F});
    const FaceId face = mesh.addFace({a, b, c});
    assert(face);

    Document document;
    const MeshId meshId = document.createMesh("Scaled Normal", std::move(mesh));
    TransformModifier transform({}, {}, {2.0F, 1.0F, 0.5F});
    const MeshModifier* stack[] = {&transform};
    const MeshEvaluationResult transformed = MeshEvaluator::evaluate(*document.mesh(meshId), stack);
    assert(transformed && transformed.mesh.has_value());
    constexpr float invSqrt5 = 0.4472135955F;
    assert(nearVector(normalForFaceVertex(*transformed.mesh, face, a), {0.0F, -invSqrt5, 2.0F * invSqrt5}));

    EditableMesh degenerate;
    const VertexId d0 = degenerate.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId d1 = degenerate.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId d2 = degenerate.addVertex({2.0F, 0.0F, 0.0F});
    assert(degenerate.addFace({d0, d1, d2}));
    const MeshId degenerateId = document.createMesh("Degenerate", std::move(degenerate));
    const MeshEvaluationResult failed = MeshEvaluator::evaluate(*document.mesh(degenerateId));
    assert(!failed);
    assert(failed.error == MeshEvaluationError::NormalGenerationFailed);
    assert(failed.normalError == NormalGenerationError::DegenerateFace);
}

} // namespace

int main() {
    testFlatAndReversedWinding();
    testSmoothCube();
    testShadingCommandsAndCache();
    testMirrorWeldAndTriangulate();
    testNonManifoldBoundaryStopsSmoothing();
    testNonUniformTransformAndDegenerateFailure();
    return 0;
}
