#include "vortex/core/document.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using namespace vortex;

[[nodiscard]] bool edgeConnectsCornerBoundary(const EvaluatedMesh& mesh, const EvaluatedMesh::Index cornerIndex) {
    const EvaluatedCorner& corner = mesh.corners()[cornerIndex];
    if (corner.next >= mesh.cornerCount() || corner.edge >= mesh.edgeCount()) {
        return false;
    }
    const EvaluatedCorner& next = mesh.corners()[corner.next];
    const EvaluatedEdge& edge = mesh.edges()[corner.edge];
    const bool forward = edge.vertexA == corner.vertex && edge.vertexB == next.vertex;
    const bool reverse = edge.vertexB == corner.vertex && edge.vertexA == next.vertex;
    return forward || reverse;
}

[[nodiscard]] bool validateGeneratedTopology(const EvaluatedMesh& mesh) {
    std::vector<bool> faceVisited(mesh.cornerCount(), false);

    for (const EvaluatedEdge& edge : mesh.edges()) {
        if (edge.vertexA >= mesh.vertexCount() || edge.vertexB >= mesh.vertexCount() || edge.vertexA == edge.vertexB) {
            return false;
        }
    }

    for (const EvaluatedFace& face : mesh.faces()) {
        if (face.cornerCount != 3U || face.firstCorner >= mesh.cornerCount()) {
            return false;
        }

        EvaluatedMesh::Index cursor = face.firstCorner;
        for (std::uint32_t step = 0; step < face.cornerCount; ++step) {
            if (cursor >= mesh.cornerCount() || faceVisited[cursor]) {
                return false;
            }
            const EvaluatedCorner& corner = mesh.corners()[cursor];
            if (corner.vertex >= mesh.vertexCount() || corner.edge >= mesh.edgeCount() ||
                corner.next >= mesh.cornerCount() || corner.prev >= mesh.cornerCount() ||
                corner.radialNext >= mesh.cornerCount() || corner.radialPrev >= mesh.cornerCount()) {
                return false;
            }
            if (mesh.corners()[corner.next].prev != cursor || mesh.corners()[corner.prev].next != cursor ||
                !edgeConnectsCornerBoundary(mesh, cursor)) {
                return false;
            }
            faceVisited[cursor] = true;
            cursor = corner.next;
        }
        if (cursor != face.firstCorner) {
            return false;
        }
    }

    for (const bool visited : faceVisited) {
        if (!visited) {
            return false;
        }
    }

    for (EvaluatedMesh::Index index = 0; index < mesh.cornerCount(); ++index) {
        const EvaluatedCorner& corner = mesh.corners()[index];
        if (mesh.corners()[corner.radialNext].radialPrev != index ||
            mesh.corners()[corner.radialPrev].radialNext != index ||
            mesh.corners()[corner.radialNext].edge != corner.edge ||
            mesh.corners()[corner.radialPrev].edge != corner.edge) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] double triangleAreaXY(const EvaluatedMesh& mesh, const EvaluatedFace& face) {
    const EvaluatedCorner& c0 = mesh.corners()[face.firstCorner];
    const EvaluatedCorner& c1 = mesh.corners()[c0.next];
    const EvaluatedCorner& c2 = mesh.corners()[c1.next];
    const Vec3 p0 = *mesh.position(c0.vertex);
    const Vec3 p1 = *mesh.position(c1.vertex);
    const Vec3 p2 = *mesh.position(c2.vertex);
    const double twice = static_cast<double>(p1.x - p0.x) * static_cast<double>(p2.y - p0.y) -
                         static_cast<double>(p1.y - p0.y) * static_cast<double>(p2.x - p0.x);
    return std::abs(twice) * 0.5;
}

[[nodiscard]] Vec2 sourceUvForCorner(const EvaluatedMesh& baseline, const CornerId sourceId) {
    const auto* uvs = baseline.attributes().values<Vec2>("uv:Map", AttributeDomain::Corner);
    assert(uvs != nullptr && uvs->size() == baseline.cornerCount());
    for (std::size_t index = 0; index < baseline.cornerCount(); ++index) {
        if (baseline.corners()[index].sourceId == sourceId) {
            return (*uvs)[index];
        }
    }
    assert(false);
    return {};
}

} // namespace

int main() {
    using namespace vortex;

    // Concave pentagon where a naive fan from vertex 0 produces triangles outside the polygon.
    EditableMesh authored;
    const VertexId v0 = authored.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId v1 = authored.addVertex({3.0F, 0.0F, 0.0F});
    const VertexId v2 = authored.addVertex({3.0F, 3.0F, 0.0F});
    const VertexId v3 = authored.addVertex({2.0F, 1.0F, 0.0F});
    const VertexId v4 = authored.addVertex({0.0F, 3.0F, 0.0F});
    const FaceId polygon = authored.addFace({v0, v1, v2, v3, v4});
    assert(polygon);

    auto* materials = authored.attributes().values<std::int32_t>("material_index", AttributeDomain::Face);
    auto* uvs = authored.attributes().values<Vec2>("uv:Map", AttributeDomain::Corner);
    assert(materials != nullptr && materials->size() == 1U);
    assert(uvs != nullptr && uvs->size() == 5U);
    (*materials)[0] = 11;
    for (std::size_t index = 0; index < uvs->size(); ++index) {
        (*uvs)[index] = {static_cast<float>(index), static_cast<float>(index + 10U)};
    }
    assert(authored.validate());

    Document document;
    const MeshId meshId = document.createMesh("Concave Triangulate Fixture", std::move(authored));
    assert(meshId);
    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr && source->authoredMesh != nullptr);
    assert(source->authoredMesh->face(polygon)->cornerCount == 5U);

    MeshEvaluationResult baselineResult = MeshEvaluator::evaluate(*source);
    assert(baselineResult && baselineResult.mesh.has_value());
    const EvaluatedMesh& baseline = *baselineResult.mesh;
    assert(baseline.faceCount() == 1U);
    assert(baseline.cornerCount() == 5U);

    TriangulateModifier triangulate;
    const MeshModifier* triangulateStack[] = {&triangulate};
    MeshEvaluationResult triangulatedResult = MeshEvaluator::evaluate(*source, triangulateStack);
    assert(triangulatedResult && triangulatedResult.mesh.has_value());
    const EvaluatedMesh& triangulated = *triangulatedResult.mesh;

    assert(triangulated.vertexCount() == 5U);
    assert(triangulated.edgeCount() == 7U);
    assert(triangulated.faceCount() == 3U);
    assert(triangulated.cornerCount() == 9U);
    assert(validateGeneratedTopology(triangulated));

    double coveredArea = 0.0;
    for (const EvaluatedFace& face : triangulated.faces()) {
        assert(face.cornerCount == 3U);
        assert(face.sourceId == polygon);
        coveredArea += triangleAreaXY(triangulated, face);
    }
    assert(coveredArea == 6.0);

    std::size_t generatedDiagonalCount = 0U;
    for (const EvaluatedEdge& edge : triangulated.edges()) {
        if (!edge.sourceId) {
            ++generatedDiagonalCount;
        }
    }
    assert(generatedDiagonalCount == 2U);

    const auto* triangulatedMaterials =
        triangulated.attributes().values<std::int32_t>("material_index", AttributeDomain::Face);
    const auto* triangulatedUvs = triangulated.attributes().values<Vec2>("uv:Map", AttributeDomain::Corner);
    assert(triangulatedMaterials != nullptr && triangulatedMaterials->size() == 3U);
    assert(triangulatedUvs != nullptr && triangulatedUvs->size() == 9U);
    for (const std::int32_t material : *triangulatedMaterials) {
        assert(material == 11);
    }
    for (std::size_t index = 0; index < triangulated.cornerCount(); ++index) {
        const CornerId sourceCorner = triangulated.corners()[index].sourceId;
        assert(sourceCorner);
        const Vec2 expected = sourceUvForCorner(baseline, sourceCorner);
        assert((*triangulatedUvs)[index] == expected);
    }

    // Evaluation never destroys the authored n-gon.
    assert(source->authoredMesh->face(polygon)->cornerCount == 5U);
    assert(source->authoredMesh->faceCount() == 1U);

    // An already-triangular face remains one triangle and does not gain a diagonal.
    EditableMesh triangleAuthored;
    const VertexId t0 = triangleAuthored.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId t1 = triangleAuthored.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId t2 = triangleAuthored.addVertex({0.0F, 1.0F, 0.0F});
    assert(triangleAuthored.addFace({t0, t1, t2}));
    const MeshId triangleMeshId = document.createMesh("Triangle Fixture", std::move(triangleAuthored));
    const MeshBlock* triangleSource = document.mesh(triangleMeshId);
    assert(triangleSource != nullptr);
    MeshEvaluationResult triangleResult = MeshEvaluator::evaluate(*triangleSource, triangulateStack);
    assert(triangleResult && triangleResult.mesh.has_value());
    assert(triangleResult.mesh->vertexCount() == 3U);
    assert(triangleResult.mesh->edgeCount() == 3U);
    assert(triangleResult.mesh->faceCount() == 1U);
    assert(triangleResult.mesh->cornerCount() == 3U);
    assert(validateGeneratedTopology(*triangleResult.mesh));

    // Full current modifier chain: Transform -> welded Mirror -> Triangulate.
    EditableMesh halfQuad;
    const VertexId q0 = halfQuad.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId q1 = halfQuad.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId q2 = halfQuad.addVertex({1.0F, 1.0F, 0.0F});
    const VertexId q3 = halfQuad.addVertex({0.0F, 1.0F, 0.0F});
    const FaceId quad = halfQuad.addFace({q0, q1, q2, q3});
    assert(quad);
    const MeshId quadMeshId = document.createMesh("Modifier Chain Fixture", std::move(halfQuad));
    const MeshBlock* quadSource = document.mesh(quadMeshId);
    assert(quadSource != nullptr && quadSource->authoredMesh != nullptr);

    TransformModifier transform({0.0F, 0.0F, 2.0F});
    MirrorModifier mirror(MirrorAxis::X, 0.0F, MirrorWeldSettings{true, 0.0F});
    const MeshModifier* beforeTriangulateStack[] = {&transform, &mirror};
    MeshEvaluationResult beforeTriangulate = MeshEvaluator::evaluate(*quadSource, beforeTriangulateStack);
    assert(beforeTriangulate && beforeTriangulate.mesh.has_value());
    assert(beforeTriangulate.mesh->faceCount() == 2U);
    assert(beforeTriangulate.mesh->faces()[0].cornerCount == 4U);

    const MeshModifier* fullStack[] = {&transform, &mirror, &triangulate};
    MeshEvaluationResult fullResult = MeshEvaluator::evaluate(*quadSource, fullStack);
    assert(fullResult && fullResult.mesh.has_value());
    assert(fullResult.mesh->faceCount() == 4U);
    assert(fullResult.mesh->cornerCount() == 12U);
    assert(validateGeneratedTopology(*fullResult.mesh));
    assert(fullResult.mesh->cacheKey() != beforeTriangulate.mesh->cacheKey());
    assert(quadSource->authoredMesh->face(quad)->cornerCount == 4U);
    assert(quadSource->authoredMesh->position(q1)->z == 0.0F);

    // Geometrically degenerate polygons fail explicitly rather than emitting junk triangles.
    EditableMesh degenerate;
    const VertexId d0 = degenerate.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId d1 = degenerate.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId d2 = degenerate.addVertex({2.0F, 0.0F, 0.0F});
    const VertexId d3 = degenerate.addVertex({3.0F, 0.0F, 0.0F});
    assert(degenerate.addFace({d0, d1, d2, d3}));
    const MeshId degenerateMeshId = document.createMesh("Degenerate Fixture", std::move(degenerate));
    const MeshBlock* degenerateSource = document.mesh(degenerateMeshId);
    assert(degenerateSource != nullptr);
    MeshEvaluationResult degenerateResult = MeshEvaluator::evaluate(*degenerateSource, triangulateStack);
    assert(!degenerateResult);
    assert(degenerateResult.error == MeshEvaluationError::ModifierFailed);
    assert(degenerateResult.modifierError == ModifierApplyError::TriangulationFailed);
    assert(degenerateResult.modifierIndex == 0U);

    return 0;
}
