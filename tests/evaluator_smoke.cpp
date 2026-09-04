#include "vortex/core/document.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using namespace vortex;

    EditableMesh authored;
    const VertexId a = authored.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = authored.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = authored.addVertex({1.0F, 1.0F, 0.0F});
    const VertexId d = authored.addVertex({0.0F, 1.0F, 0.0F});
    const FaceId faceId = authored.addFace({a, b, c, d});
    assert(faceId);

    auto* materialIndices = authored.attributes().values<std::int32_t>("material_index", AttributeDomain::Face);
    assert(materialIndices != nullptr);
    assert(materialIndices->size() == 1U);
    (*materialIndices)[0] = 7;
    assert(authored.validate());

    Document document;
    const MeshId meshId = document.createMesh("Quad", std::move(authored));
    assert(meshId);

    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);
    const std::uint64_t firstRevision = source->revision;

    MeshEvaluationResult firstResult = MeshEvaluator::evaluate(*source);
    assert(firstResult);
    assert(firstResult.mesh.has_value());
    const EvaluatedMesh& first = *firstResult.mesh;

    assert(first.sourceMeshId() == meshId);
    assert(first.sourceRevision() == firstRevision);
    assert(first.vertexCount() == 4U);
    assert(first.edgeCount() == 4U);
    assert(first.faceCount() == 1U);
    assert(first.cornerCount() == 4U);
    assert(first.vertices()[0].sourceId == a);
    assert(first.faces()[0].sourceId == faceId);
    assert(first.faces()[0].cornerCount == 4U);

    const auto* evaluatedMaterials =
        first.attributes().values<std::int32_t>("material_index", AttributeDomain::Face);
    assert(evaluatedMaterials != nullptr);
    assert(evaluatedMaterials->size() == 1U);
    assert((*evaluatedMaterials)[0] == 7);

    std::uint32_t cursor = first.faces()[0].firstCorner;
    for (std::uint32_t index = 0; index < first.faces()[0].cornerCount; ++index) {
        assert(cursor < first.cornerCount());
        const EvaluatedCorner& corner = first.corners()[cursor];
        assert(corner.vertex < first.vertexCount());
        assert(corner.edge < first.edgeCount());
        cursor = corner.next;
    }
    assert(cursor == first.faces()[0].firstCorner);

    const auto oldPosition = first.position(0);
    assert(oldPosition.has_value());
    assert(oldPosition->x == 0.0F);

    MeshHistory history;
    // Keep the polygon geometrically valid so the derived-normal stage has a real surface.
    MoveVerticesCommand move(std::vector<VertexPositionTarget>{{a, {-1.0F, 0.0F, 0.0F}}});
    assert(document.executeMeshCommand(meshId, history, move));

    source = document.mesh(meshId);
    assert(source != nullptr);
    assert(source->revision > firstRevision);

    MeshEvaluationResult secondResult = MeshEvaluator::evaluate(*source);
    assert(secondResult);
    const EvaluatedMesh& second = *secondResult.mesh;
    assert(second.sourceRevision() == source->revision);

    const auto newPosition = second.position(0);
    assert(newPosition.has_value());
    assert(newPosition->x == -1.0F);

    const auto preservedOldPosition = first.position(0);
    assert(preservedOldPosition.has_value());
    assert(preservedOldPosition->x == 0.0F);

    assert(history.undoCount() == 1U);
    assert(document.undoMeshCommand(meshId, history));
    source = document.mesh(meshId);
    assert(source != nullptr);

    MeshEvaluationResult thirdResult = MeshEvaluator::evaluate(*source);
    assert(thirdResult);
    const auto restoredPosition = thirdResult.mesh->position(0);
    assert(restoredPosition.has_value());
    assert(restoredPosition->x == 0.0F);

    return 0;
}
