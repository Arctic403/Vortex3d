#include "vortex/core/document.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
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

} // namespace

int main() {
    using namespace vortex;

    EditableMesh authored;
    const VertexId a = authored.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId b = authored.addVertex({3.0F, 0.0F, 0.0F});
    const VertexId c = authored.addVertex({2.0F, 1.0F, 0.0F}); // exactly on the mirror plane
    const VertexId d = authored.addVertex({1.0F, 1.0F, 0.0F});
    assert(authored.addFace({a, b, c}));
    assert(authored.addFace({a, c, d}));
    assert(authored.validate());

    auto* cornerNormals = authored.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(cornerNormals != nullptr);
    assert(!cornerNormals->empty());
    (*cornerNormals)[0] = {1.0F, 0.0F, 0.0F};

    Document document;
    const MeshId meshId = document.createMesh("Mirror Fixture", std::move(authored));
    assert(meshId);
    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);

    MeshEvaluationResult baselineResult = MeshEvaluator::evaluate(*source);
    assert(baselineResult);
    assert(baselineResult.mesh.has_value());
    const EvaluatedMesh& baseline = *baselineResult.mesh;
    assert(validateGeneratedTopology(baseline));

    const std::size_t vertexCount = baseline.vertexCount();
    const std::size_t edgeCount = baseline.edgeCount();
    const std::size_t faceCount = baseline.faceCount();
    const std::size_t cornerCount = baseline.cornerCount();

    MirrorModifier mirrorX(MirrorAxis::X, 2.0F);
    const MeshModifier* modifiers[] = {&mirrorX};
    MeshEvaluationResult mirroredResult = MeshEvaluator::evaluate(*source, modifiers);
    assert(mirroredResult);
    assert(mirroredResult.mesh.has_value());
    const EvaluatedMesh& mirrored = *mirroredResult.mesh;

    assert(mirrored.vertexCount() == vertexCount * 2U);
    assert(mirrored.edgeCount() == edgeCount * 2U);
    assert(mirrored.faceCount() == faceCount * 2U);
    assert(mirrored.cornerCount() == cornerCount * 2U);
    assert(validateGeneratedTopology(mirrored));

    const auto baselineA = baseline.position(0U);
    const auto mirroredOriginalA = mirrored.position(0U);
    const auto mirroredCopyA = mirrored.position(static_cast<EvaluatedMesh::Index>(vertexCount));
    assert(baselineA.has_value() && mirroredOriginalA.has_value() && mirroredCopyA.has_value());
    assert(baselineA->x == 1.0F);
    assert(mirroredOriginalA->x == 1.0F);
    assert(mirroredCopyA->x == 3.0F);

    const auto mirroredPlaneVertex = mirrored.position(static_cast<EvaluatedMesh::Index>(vertexCount + 2U));
    assert(mirroredPlaneVertex.has_value());
    assert(mirroredPlaneVertex->x == 2.0F);
    assert(mirrored.vertices()[vertexCount + 2U].sourceId == mirrored.vertices()[2U].sourceId);
    assert(vertexCount * 2U == mirrored.vertexCount()); // plane vertices are duplicated, not welded in v0.1

    for (std::size_t index = 0; index < vertexCount; ++index) {
        assert(mirrored.vertices()[vertexCount + index].sourceId == mirrored.vertices()[index].sourceId);
    }
    for (std::size_t index = 0; index < edgeCount; ++index) {
        assert(mirrored.edges()[edgeCount + index].sourceId == mirrored.edges()[index].sourceId);
    }
    for (std::size_t index = 0; index < faceCount; ++index) {
        assert(mirrored.faces()[faceCount + index].sourceId == mirrored.faces()[index].sourceId);
    }
    for (std::size_t index = 0; index < cornerCount; ++index) {
        assert(mirrored.corners()[cornerCount + index].sourceId == mirrored.corners()[index].sourceId);
    }

    const EvaluatedFace& sourceFace = mirrored.faces()[0];
    const EvaluatedFace& mirroredFace = mirrored.faces()[faceCount];
    const EvaluatedCorner& sourceFirst = mirrored.corners()[sourceFace.firstCorner];
    const EvaluatedCorner& mirroredFirst = mirrored.corners()[mirroredFace.firstCorner];
    assert(mirroredFace.firstCorner == cornerCount + sourceFace.firstCorner);
    assert(mirroredFirst.next == cornerCount + sourceFirst.prev);
    assert(mirroredFirst.prev == cornerCount + sourceFirst.next);
    assert(mirroredFirst.edge == edgeCount + mirrored.corners()[sourceFirst.prev].edge);

    const auto* mirroredNormals = mirrored.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(mirroredNormals != nullptr);
    assert(mirroredNormals->size() == cornerCount * 2U);
    assert((*mirroredNormals)[0].x == 1.0F);
    assert((*mirroredNormals)[cornerCount].x == -1.0F);

    MirrorModifier mirrorY(MirrorAxis::Y, 2.0F);
    const MeshModifier* yStack[] = {&mirrorY};
    MeshEvaluationResult yResult = MeshEvaluator::evaluate(*source, yStack);
    assert(yResult);
    assert(yResult.mesh->cacheKey() != mirrored.cacheKey());

    MirrorModifier mirrorOffset(MirrorAxis::X, 3.0F);
    const MeshModifier* offsetStack[] = {&mirrorOffset};
    MeshEvaluationResult offsetResult = MeshEvaluator::evaluate(*source, offsetStack);
    assert(offsetResult);
    assert(offsetResult.mesh->cacheKey() != mirrored.cacheKey());

    MirrorModifier invalidOffset(MirrorAxis::X, std::numeric_limits<float>::quiet_NaN());
    const MeshModifier* invalidOffsetStack[] = {&invalidOffset};
    MeshEvaluationResult invalidOffsetResult = MeshEvaluator::evaluate(*source, invalidOffsetStack);
    assert(!invalidOffsetResult);
    assert(invalidOffsetResult.error == MeshEvaluationError::ModifierFailed);
    assert(invalidOffsetResult.modifierError == ModifierApplyError::InvalidMirror);
    assert(invalidOffsetResult.modifierIndex == 0U);

    MirrorModifier invalidAxis(static_cast<MirrorAxis>(255U), 0.0F);
    const MeshModifier* invalidAxisStack[] = {&invalidAxis};
    MeshEvaluationResult invalidAxisResult = MeshEvaluator::evaluate(*source, invalidAxisStack);
    assert(!invalidAxisResult);
    assert(invalidAxisResult.modifierError == ModifierApplyError::InvalidMirror);

    return 0;
}
