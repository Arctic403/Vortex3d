#include "vortex/core/document.hpp"
#include "vortex/eval/evaluation_cache.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cstddef>
#include <utility>

int main() {
    using namespace vortex;

    EditableMesh authored;
    const VertexId a = authored.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = authored.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = authored.addVertex({1.0F, 1.0F, 0.0F});
    const VertexId d = authored.addVertex({0.0F, 1.0F, 0.0F});
    const FaceId face = authored.addFace({a, b, c, d});
    assert(face);
    assert(!authored.attributes().contains("sharp_face", AttributeDomain::Face));

    Document document;
    const MeshId meshId = document.createMesh("Revision Quad", std::move(authored));
    assert(meshId);

    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);
    const std::uint64_t initialBlockRevision = source->revision;
    const std::uint64_t initialEvaluationRevision = source->evaluationRevision();

    EvaluationCache cache(std::size_t{8} * std::size_t{1024} * std::size_t{1024});
    const CachedEvaluationResult first = cache.evaluate(*source);
    assert(first);
    assert(!first.cacheHit);
    assert(first.mesh->sourceEvaluationRevision() == initialEvaluationRevision);

    // Metadata changes still advance the datablock/document revision but must not invalidate
    // evaluated geometry when the authored geometry/shading payload is unchanged.
    const std::uint64_t documentRevisionBeforeRename = document.revision();
    assert(document.renameMesh(meshId, "Revision Quad Renamed"));
    source = document.mesh(meshId);
    assert(source != nullptr);
    assert(source->revision == initialBlockRevision + 1U);
    assert(source->evaluationRevision() == initialEvaluationRevision);
    assert(document.revision() == documentRevisionBeforeRename + 1U);

    const CachedEvaluationResult afterRename = cache.evaluate(*source);
    assert(afterRename);
    assert(afterRename.cacheHit);
    assert(afterRename.mesh == first.mesh);

    MeshHistory history;
    MeshCommandResult result;

    // Missing sharp_face semantically means flat=true. Setting that same value is a true
    // no-op: no layer allocation, no history record, and no revision/cache invalidation.
    const std::uint64_t blockBeforeFlatNoop = source->revision;
    const std::uint64_t evaluationBeforeFlatNoop = source->evaluationRevision();
    const std::uint64_t documentBeforeFlatNoop = document.revision();
    SetFaceSharpCommand keepFlat(face, true);
    assert(document.executeMeshCommand(meshId, history, keepFlat, &result));
    assert(!result.changed);
    assert(history.undoCount() == 0U);
    assert(!document.authoredMesh(meshId)->attributes().contains("sharp_face", AttributeDomain::Face));
    source = document.mesh(meshId);
    assert(source->revision == blockBeforeFlatNoop);
    assert(source->evaluationRevision() == evaluationBeforeFlatNoop);
    assert(document.revision() == documentBeforeFlatNoop);
    const CachedEvaluationResult afterFlatNoop = cache.evaluate(*source);
    assert(afterFlatNoop.cacheHit);
    assert(afterFlatNoop.mesh == first.mesh);

    // The first actual smooth edit materializes the layer and advances both general and
    // evaluation revisions.
    SetFaceSharpCommand makeSmooth(face, false);
    assert(document.executeMeshCommand(meshId, history, makeSmooth, &result));
    assert(result.changed);
    assert(history.undoCount() == 1U);
    assert(document.authoredMesh(meshId)->attributes().contains("sharp_face", AttributeDomain::Face));
    source = document.mesh(meshId);
    assert(source->revision == blockBeforeFlatNoop + 1U);
    assert(source->evaluationRevision() == evaluationBeforeFlatNoop + 1U);
    assert(document.revision() == documentBeforeFlatNoop + 1U);

    const CachedEvaluationResult afterSmooth = cache.evaluate(*source);
    assert(afterSmooth);
    assert(!afterSmooth.cacheHit);
    assert(afterSmooth.mesh != first.mesh);

    // Repeating the same authored value is also a no-op and must not create another history
    // entry or invalidate the just-built evaluated snapshot.
    const std::uint64_t blockBeforeSmoothNoop = source->revision;
    const std::uint64_t evaluationBeforeSmoothNoop = source->evaluationRevision();
    const std::uint64_t documentBeforeSmoothNoop = document.revision();
    SetFaceSharpCommand keepSmooth(face, false);
    assert(document.executeMeshCommand(meshId, history, keepSmooth, &result));
    assert(!result.changed);
    assert(history.undoCount() == 1U);
    source = document.mesh(meshId);
    assert(source->revision == blockBeforeSmoothNoop);
    assert(source->evaluationRevision() == evaluationBeforeSmoothNoop);
    assert(document.revision() == documentBeforeSmoothNoop);
    const CachedEvaluationResult afterSmoothNoop = cache.evaluate(*source);
    assert(afterSmoothNoop.cacheHit);
    assert(afterSmoothNoop.mesh == afterSmooth.mesh);

    // Undo of the first smooth edit restores the exact sparse representation: the optional
    // sharp_face layer disappears again rather than retaining an all-default allocation.
    assert(document.undoMeshCommand(meshId, history));
    assert(!document.authoredMesh(meshId)->attributes().contains("sharp_face", AttributeDomain::Face));
    source = document.mesh(meshId);
    assert(source->evaluationRevision() == evaluationBeforeSmoothNoop + 1U);
    const CachedEvaluationResult afterUndo = cache.evaluate(*source);
    assert(afterUndo);
    assert(!afterUndo.cacheHit);

    assert(document.redoMeshCommand(meshId, history));
    assert(document.authoredMesh(meshId)->attributes().contains("sharp_face", AttributeDomain::Face));
    const auto* sharpFaces = document.authoredMesh(meshId)->attributes().values<bool>("sharp_face", AttributeDomain::Face);
    assert(sharpFaces != nullptr);
    assert(!static_cast<bool>((*sharpFaces)[0]));

    // A no-op position command follows the same mutation contract: success without a durable
    // change must not advance revisions.
    source = document.mesh(meshId);
    const auto currentPosition = document.authoredMesh(meshId)->position(a);
    assert(currentPosition.has_value());
    const std::uint64_t blockBeforeMoveNoop = source->revision;
    const std::uint64_t evaluationBeforeMoveNoop = source->evaluationRevision();
    const std::uint64_t documentBeforeMoveNoop = document.revision();
    MoveVerticesCommand noMove({{a, *currentPosition}});
    assert(document.executeMeshCommand(meshId, history, noMove, &result));
    assert(!result.changed);
    source = document.mesh(meshId);
    assert(source->revision == blockBeforeMoveNoop);
    assert(source->evaluationRevision() == evaluationBeforeMoveNoop);
    assert(document.revision() == documentBeforeMoveNoop);

    return 0;
}
