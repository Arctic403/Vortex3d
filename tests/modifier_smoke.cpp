#include "vortex/core/document.hpp"
#include "vortex/eval/evaluator.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <array>
#include <cassert>
#include <span>
#include <vector>

int main() {
    using namespace vortex;

    EditableMesh authored;
    const VertexId a = authored.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId b = authored.addVertex({0.0F, 1.0F, 0.0F});
    const VertexId c = authored.addVertex({0.0F, 0.0F, 0.0F});
    const FaceId faceId = authored.addFace({a, b, c});
    assert(faceId);
    assert(!authored.attributes().contains("normal", AttributeDomain::Corner));
    assert(authored.validate());

    Document document;
    const MeshId meshId = document.createMesh("Modifier Triangle", std::move(authored));
    assert(meshId);

    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);
    const std::uint64_t sourceRevision = source->revision;

    const TransformModifier transform(
        {3.0F, 4.0F, 5.0F},
        {},
        {2.0F, 3.0F, 4.0F});
    const std::array<const MeshModifier*, 1> transformStack{&transform};

    const MeshEvaluationResult transformedResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{transformStack});
    assert(transformedResult);
    assert(transformedResult.mesh.has_value());
    const EvaluatedMesh& transformed = *transformedResult.mesh;

    assert(transformed.sourceMeshId() == meshId);
    assert(transformed.sourceRevision() == sourceRevision);
    assert(transformed.modifierStackRevision() != 0U);
    assert(transformed.vertices()[0].sourceId == a);

    const auto transformedA = transformed.position(0);
    const auto transformedB = transformed.position(1);
    assert(transformedA.has_value());
    assert(transformedB.has_value());
    assert(transformedA->x == 5.0F && transformedA->y == 4.0F && transformedA->z == 5.0F);
    assert(transformedB->x == 3.0F && transformedB->y == 7.0F && transformedB->z == 5.0F);

    const auto* evaluatedNormals = transformed.attributes().values<Vec3>("normal", AttributeDomain::Corner);
    assert(evaluatedNormals != nullptr);
    for (const Vec3 normal : *evaluatedNormals) {
        assert(normal.x == 0.0F && normal.y == 0.0F && normal.z == 1.0F);
    }

    const EditableMesh* sourceMesh = document.authoredMesh(meshId);
    assert(sourceMesh != nullptr);
    assert(!sourceMesh->attributes().contains("normal", AttributeDomain::Corner));
    const auto authoredA = sourceMesh->position(a);
    assert(authoredA.has_value());
    assert(authoredA->x == 1.0F && authoredA->y == 0.0F && authoredA->z == 0.0F);

    const MeshEvaluationResult repeatedResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{transformStack});
    assert(repeatedResult);
    assert(repeatedResult.mesh->cacheKey() == transformed.cacheKey());
    assert(EvaluationCacheKeyHash{}(repeatedResult.mesh->cacheKey()) ==
           EvaluationCacheKeyHash{}(transformed.cacheKey()));

    const TransformModifier differentTransform({4.0F, 4.0F, 5.0F}, {}, {2.0F, 3.0F, 4.0F});
    const std::array<const MeshModifier*, 1> differentStack{&differentTransform};
    const MeshEvaluationResult differentResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{differentStack});
    assert(differentResult);
    assert(differentResult.mesh->modifierStackRevision() != transformed.modifierStackRevision());
    assert(differentResult.mesh->cacheKey() != transformed.cacheKey());

    const TransformModifier scaleFirst({}, {}, {2.0F, 1.0F, 1.0F});
    const TransformModifier translateSecond({1.0F, 0.0F, 0.0F});
    const std::array<const MeshModifier*, 2> scaleThenTranslate{&scaleFirst, &translateSecond};
    const std::array<const MeshModifier*, 2> translateThenScale{&translateSecond, &scaleFirst};

    const MeshEvaluationResult scaleThenTranslateResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{scaleThenTranslate});
    const MeshEvaluationResult translateThenScaleResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{translateThenScale});
    assert(scaleThenTranslateResult && translateThenScaleResult);
    assert(scaleThenTranslateResult.mesh->modifierStackRevision() !=
           translateThenScaleResult.mesh->modifierStackRevision());
    assert(scaleThenTranslateResult.mesh->position(0)->x == 3.0F);
    assert(translateThenScaleResult.mesh->position(0)->x == 4.0F);

    const TransformModifier invalidTransform({}, {}, {0.0F, 1.0F, 1.0F});
    const std::array<const MeshModifier*, 1> invalidStack{&invalidTransform};
    const MeshEvaluationResult invalidResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{invalidStack});
    assert(!invalidResult);
    assert(!invalidResult.mesh.has_value());
    assert(invalidResult.error == MeshEvaluationError::ModifierFailed);
    assert(invalidResult.modifierError == ModifierApplyError::InvalidTransform);
    assert(invalidResult.modifierIndex.has_value() && *invalidResult.modifierIndex == 0U);

    const std::array<const MeshModifier*, 1> nullStack{nullptr};
    const MeshEvaluationResult nullResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{nullStack});
    assert(!nullResult);
    assert(nullResult.error == MeshEvaluationError::NullModifier);
    assert(nullResult.modifierIndex.has_value() && *nullResult.modifierIndex == 0U);

    MeshHistory history;
    MoveVerticesCommand move(std::vector<VertexPositionTarget>{{a, {2.0F, 0.0F, 0.0F}}});
    assert(document.executeMeshCommand(meshId, history, move));
    source = document.mesh(meshId);
    assert(source != nullptr);
    assert(source->revision > sourceRevision);

    const MeshEvaluationResult movedResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{transformStack});
    assert(movedResult);
    assert(movedResult.mesh->sourceRevision() == source->revision);
    assert(movedResult.mesh->modifierStackRevision() == transformed.modifierStackRevision());
    assert(movedResult.mesh->cacheKey() != transformed.cacheKey());
    assert(movedResult.mesh->position(0)->x == 7.0F);

    assert(document.undoMeshCommand(meshId, history));
    source = document.mesh(meshId);
    assert(source != nullptr);
    const MeshEvaluationResult undoneResult =
        MeshEvaluator::evaluate(*source, std::span<const MeshModifier* const>{transformStack});
    assert(undoneResult);
    assert(undoneResult.mesh->position(0)->x == 5.0F);

    return 0;
}
