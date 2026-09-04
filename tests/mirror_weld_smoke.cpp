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

    for (const EvaluatedFace& face : mesh.faces()) {
        if (face.cornerCount < 3U || face.firstCorner >= mesh.cornerCount()) {
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

[[nodiscard]] std::size_t radialUseCount(const EvaluatedMesh& mesh, const EvaluatedMesh::Index edgeIndex) {
    EvaluatedMesh::Index first = std::numeric_limits<EvaluatedMesh::Index>::max();
    for (EvaluatedMesh::Index index = 0; index < mesh.cornerCount(); ++index) {
        if (mesh.corners()[index].edge == edgeIndex) {
            first = index;
            break;
        }
    }
    if (first == std::numeric_limits<EvaluatedMesh::Index>::max()) {
        return 0U;
    }

    std::size_t count = 0U;
    EvaluatedMesh::Index cursor = first;
    do {
        ++count;
        cursor = mesh.corners()[cursor].radialNext;
        assert(count <= mesh.cornerCount());
    } while (cursor != first);
    return count;
}

[[nodiscard]] EvaluatedMesh::Index edgeBetween(
    const EvaluatedMesh& mesh,
    const EvaluatedMesh::Index vertexA,
    const EvaluatedMesh::Index vertexB) {
    for (EvaluatedMesh::Index index = 0; index < mesh.edgeCount(); ++index) {
        const EvaluatedEdge& edge = mesh.edges()[index];
        const bool forward = edge.vertexA == vertexA && edge.vertexB == vertexB;
        const bool reverse = edge.vertexA == vertexB && edge.vertexB == vertexA;
        if (forward || reverse) {
            return index;
        }
    }
    return std::numeric_limits<EvaluatedMesh::Index>::max();
}

} // namespace

int main() {
    using namespace vortex;

    EditableMesh authored;
    const VertexId a = authored.addVertex({0.0005F, 0.0F, 0.0F});
    const VertexId b = authored.addVertex({-0.0004F, 1.0F, 0.0F});
    const VertexId c = authored.addVertex({1.0F, 0.0F, 0.0F});
    const FaceId face = authored.addFace({a, b, c});
    assert(face);

    auto* materials = authored.attributes().values<std::int32_t>("material_index", AttributeDomain::Face);
    auto* uvs = authored.attributes().values<Vec2>("uv:Map", AttributeDomain::Corner);
    auto* normals = authored.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(materials != nullptr && materials->size() == 1U);
    assert(uvs != nullptr && uvs->size() == 3U);
    assert(normals != nullptr && normals->size() == 3U);
    (*materials)[0] = 7;
    (*uvs)[0] = {0.0F, 0.0F};
    (*uvs)[1] = {1.0F, 0.0F};
    (*uvs)[2] = {0.0F, 1.0F};
    (*normals)[0] = {1.0F, 0.0F, 0.0F};
    assert(authored.validate());

    Document document;
    const MeshId meshId = document.createMesh("Mirror Weld Fixture", std::move(authored));
    assert(meshId);
    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr && source->authoredMesh != nullptr);

    const auto sourceA = source->authoredMesh->position(a);
    const auto sourceB = source->authoredMesh->position(b);
    assert(sourceA.has_value() && sourceB.has_value());
    assert(sourceA->x == 0.0005F);
    assert(sourceB->x == -0.0004F);

    MeshEvaluationResult baselineResult = MeshEvaluator::evaluate(*source);
    assert(baselineResult && baselineResult.mesh.has_value());
    const EvaluatedMesh& baseline = *baselineResult.mesh;
    assert(validateGeneratedTopology(baseline));

    MirrorModifier weldX(MirrorAxis::X, 0.0F, MirrorWeldSettings{true, 0.001F});
    const MeshModifier* weldStack[] = {&weldX};
    MeshEvaluationResult weldedResult = MeshEvaluator::evaluate(*source, weldStack);
    assert(weldedResult && weldedResult.mesh.has_value());
    const EvaluatedMesh& welded = *weldedResult.mesh;

    assert(welded.vertexCount() == 4U);
    assert(welded.edgeCount() == 5U);
    assert(welded.faceCount() == 2U);
    assert(welded.cornerCount() == 6U);
    assert(validateGeneratedTopology(welded));

    const auto weldedA = welded.position(0U);
    const auto weldedB = welded.position(1U);
    const auto mirroredC = welded.position(3U);
    assert(weldedA.has_value() && weldedB.has_value() && mirroredC.has_value());
    assert(weldedA->x == 0.0F);
    assert(weldedB->x == 0.0F);
    assert(mirroredC->x == -1.0F);
    assert(welded.vertices()[3].sourceId == welded.vertices()[2].sourceId);

    assert(source->authoredMesh->position(a)->x == 0.0005F);
    assert(source->authoredMesh->position(b)->x == -0.0004F);

    const EvaluatedMesh::Index seamEdge = edgeBetween(welded, 0U, 1U);
    assert(seamEdge != std::numeric_limits<EvaluatedMesh::Index>::max());
    assert(radialUseCount(welded, seamEdge) == 2U);

    const auto* weldedMaterials = welded.attributes().values<std::int32_t>("material_index", AttributeDomain::Face);
    const auto* weldedUvs = welded.attributes().values<Vec2>("uv:Map", AttributeDomain::Corner);
    const auto* weldedNormals = welded.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(weldedMaterials != nullptr && weldedMaterials->size() == 2U);
    assert((*weldedMaterials)[0] == 7 && (*weldedMaterials)[1] == 7);
    assert(weldedUvs != nullptr && weldedUvs->size() == 6U);
    assert((*weldedUvs)[3].x == 0.0F && (*weldedUvs)[3].y == 0.0F);
    assert((*weldedUvs)[4].x == 0.0F && (*weldedUvs)[4].y == 1.0F);
    assert((*weldedUvs)[5].x == 1.0F && (*weldedUvs)[5].y == 0.0F);
    assert(weldedNormals != nullptr && weldedNormals->size() == 6U);
    assert((*weldedNormals)[3].x == -1.0F);

    MirrorModifier noWeld(MirrorAxis::X, 0.0F);
    const MeshModifier* noWeldStack[] = {&noWeld};
    MeshEvaluationResult noWeldResult = MeshEvaluator::evaluate(*source, noWeldStack);
    assert(noWeldResult && noWeldResult.mesh.has_value());
    assert(noWeldResult.mesh->vertexCount() == 6U);
    assert(noWeldResult.mesh->cacheKey() != welded.cacheKey());

    MirrorModifier exactOnly(MirrorAxis::X, 0.0F, MirrorWeldSettings{true, 0.0F});
    const MeshModifier* exactOnlyStack[] = {&exactOnly};
    MeshEvaluationResult exactOnlyResult = MeshEvaluator::evaluate(*source, exactOnlyStack);
    assert(exactOnlyResult && exactOnlyResult.mesh.has_value());
    assert(exactOnlyResult.mesh->vertexCount() == 6U);
    assert(exactOnlyResult.mesh->cacheKey() != welded.cacheKey());

    EditableMesh planarAuthored;
    const VertexId p0 = planarAuthored.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId p1 = planarAuthored.addVertex({0.0F, 1.0F, 0.0F});
    const VertexId p2 = planarAuthored.addVertex({0.0F, 0.0F, 1.0F});
    assert(planarAuthored.addFace({p0, p1, p2}));
    const MeshId planarMeshId = document.createMesh("Planar Weld Fixture", std::move(planarAuthored));
    const MeshBlock* planarSource = document.mesh(planarMeshId);
    assert(planarSource != nullptr);
    MeshEvaluationResult planarResult = MeshEvaluator::evaluate(*planarSource, weldStack);
    assert(planarResult && planarResult.mesh.has_value());
    assert(planarResult.mesh->vertexCount() == 3U);
    assert(planarResult.mesh->edgeCount() == 3U);
    assert(planarResult.mesh->faceCount() == 1U);
    assert(planarResult.mesh->cornerCount() == 3U);
    assert(validateGeneratedTopology(*planarResult.mesh));

    EditableMesh nonManifoldAuthored;
    const VertexId n0 = nonManifoldAuthored.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId n1 = nonManifoldAuthored.addVertex({0.0F, 1.0F, 0.0F});
    const VertexId n2 = nonManifoldAuthored.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId n3 = nonManifoldAuthored.addVertex({1.0F, 1.0F, 1.0F});
    assert(nonManifoldAuthored.addFace({n0, n1, n2}));
    assert(nonManifoldAuthored.addFace({n1, n0, n3}));
    assert(nonManifoldAuthored.validate());

    const MeshId nonManifoldMeshId = document.createMesh("Non-Manifold Weld Fixture", std::move(nonManifoldAuthored));
    const MeshBlock* nonManifoldSource = document.mesh(nonManifoldMeshId);
    assert(nonManifoldSource != nullptr);
    MeshEvaluationResult nonManifoldResult = MeshEvaluator::evaluate(*nonManifoldSource, weldStack);
    assert(nonManifoldResult && nonManifoldResult.mesh.has_value());
    const EvaluatedMesh& nonManifold = *nonManifoldResult.mesh;
    assert(validateGeneratedTopology(nonManifold));
    const EvaluatedMesh::Index nonManifoldSeam = edgeBetween(nonManifold, 0U, 1U);
    assert(nonManifoldSeam != std::numeric_limits<EvaluatedMesh::Index>::max());
    assert(radialUseCount(nonManifold, nonManifoldSeam) == 4U);

    MirrorModifier negativeTolerance(MirrorAxis::X, 0.0F, MirrorWeldSettings{true, -0.001F});
    const MeshModifier* negativeStack[] = {&negativeTolerance};
    MeshEvaluationResult negativeResult = MeshEvaluator::evaluate(*source, negativeStack);
    assert(!negativeResult);
    assert(negativeResult.error == MeshEvaluationError::ModifierFailed);
    assert(negativeResult.modifierError == ModifierApplyError::InvalidMirrorWeld);

    MirrorModifier nanTolerance(
        MirrorAxis::X,
        0.0F,
        MirrorWeldSettings{true, std::numeric_limits<float>::quiet_NaN()});
    const MeshModifier* nanStack[] = {&nanTolerance};
    MeshEvaluationResult nanResult = MeshEvaluator::evaluate(*source, nanStack);
    assert(!nanResult);
    assert(nanResult.modifierError == ModifierApplyError::InvalidMirrorWeld);

    return 0;
}
