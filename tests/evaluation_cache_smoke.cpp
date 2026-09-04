#include "vortex/core/command.hpp"
#include "vortex/core/document.hpp"
#include "vortex/core/document_commands.hpp"
#include "vortex/eval/evaluation_cache.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

vortex::MeshId createQuad(vortex::Document& document, const float xOffset = 0.0F) {
    using namespace vortex;

    EditableMesh mesh;
    const VertexId a = mesh.addVertex({xOffset + 0.0F, 0.0F, 0.0F});
    const VertexId b = mesh.addVertex({xOffset + 1.0F, 0.0F, 0.0F});
    const VertexId c = mesh.addVertex({xOffset + 1.0F, 1.0F, 0.0F});
    const VertexId d = mesh.addVertex({xOffset + 0.0F, 1.0F, 0.0F});
    const FaceId face = mesh.addFace({a, b, c, d});
    assert(face);
    assert(mesh.validate());
    return document.createMesh("Cache Quad", std::move(mesh));
}

} // namespace

int main() {
    using namespace vortex;

    Document document;
    const MeshId meshId = createQuad(document);
    assert(meshId);
    assert(document.runtimeId());

    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);
    assert(source->ownerDocumentRuntimeId() == document.runtimeId());

    MeshEvaluationResult baseline = MeshEvaluator::evaluate(*source);
    assert(baseline);
    assert(baseline.mesh.has_value());
    assert(baseline.mesh->sourceDocumentRuntimeId() == document.runtimeId());
    const std::size_t oneSnapshotBytes = baseline.mesh->estimatedRetainedBytes();
    assert(oneSnapshotBytes > 0U);
    assert(oneSnapshotBytes <= std::numeric_limits<std::size_t>::max() / 4U);

    // Two Documents intentionally overlap in numeric MeshId and revision. Runtime Document
    // identity must keep their cache entries distinct even when one cache serves both.
    Document otherDocument;
    const MeshId otherMeshId = createQuad(otherDocument, 10.0F);
    assert(otherMeshId == meshId);
    assert(otherDocument.runtimeId() != document.runtimeId());
    const MeshBlock* otherSource = otherDocument.mesh(otherMeshId);
    assert(otherSource != nullptr);
    assert(otherSource->revision == source->revision);
    assert(otherSource->ownerDocumentRuntimeId() == otherDocument.runtimeId());

    EvaluationCache crossDocumentCache(oneSnapshotBytes * 4U);
    CachedEvaluationResult documentAFirst = crossDocumentCache.evaluate(*source);
    assert(documentAFirst);
    assert(!documentAFirst.cacheHit);
    CachedEvaluationResult documentBFirst = crossDocumentCache.evaluate(*otherSource);
    assert(documentBFirst);
    assert(!documentBFirst.cacheHit);
    assert(documentAFirst.mesh->cacheKey() != documentBFirst.mesh->cacheKey());
    assert(documentAFirst.mesh->sourceMeshId() == documentBFirst.mesh->sourceMeshId());
    assert(documentAFirst.mesh->sourceRevision() == documentBFirst.mesh->sourceRevision());
    assert(documentAFirst.mesh->sourceDocumentRuntimeId() != documentBFirst.mesh->sourceDocumentRuntimeId());
    const auto documentBPosition = documentBFirst.mesh->position(0);
    assert(documentBPosition.has_value());
    assert(documentBPosition->x == 10.0F);
    CachedEvaluationResult documentASecond = crossDocumentCache.evaluate(*source);
    assert(documentASecond.cacheHit);
    assert(documentASecond.mesh == documentAFirst.mesh);
    assert(crossDocumentCache.entryCount() == 2U);

    // A manually detached MeshBlock has no owning Document runtime identity and cannot enter
    // the evaluator/cache identity space accidentally.
    MeshBlock detachedBlock(MeshId{999}, "Detached", std::make_unique<EditableMesh>());
    const MeshEvaluationKeyResult detachedKey = MeshEvaluator::cacheKeyFor(detachedBlock);
    assert(!detachedKey);
    assert(detachedKey.error == MeshEvaluationError::InvalidSourceIdentity);

    EvaluationCache cache(oneSnapshotBytes * 2U);

    TransformModifier transformA({0.0F, 0.0F, 0.0F});
    TransformModifier transformB({1.0F, 0.0F, 0.0F});
    TransformModifier transformC({2.0F, 0.0F, 0.0F});

    const std::vector<const MeshModifier*> stackA{&transformA};
    const std::vector<const MeshModifier*> stackB{&transformB};
    const std::vector<const MeshModifier*> stackC{&transformC};

    CachedEvaluationResult firstA = cache.evaluate(*source, stackA);
    assert(firstA);
    assert(!firstA.cacheHit);
    assert(firstA.retainedByCache);
    assert(cache.entryCount() == 1U);
    assert(cache.hitCount() == 0U);
    assert(cache.missCount() == 1U);

    CachedEvaluationResult secondA = cache.evaluate(*source, stackA);
    assert(secondA);
    assert(secondA.cacheHit);
    assert(secondA.retainedByCache);
    assert(secondA.mesh == firstA.mesh);
    assert(cache.entryCount() == 1U);
    assert(cache.hitCount() == 1U);
    assert(cache.missCount() == 1U);

    CachedEvaluationResult firstB = cache.evaluate(*source, stackB);
    assert(firstB);
    assert(!firstB.cacheHit);
    assert(cache.entryCount() == 2U);

    CachedEvaluationResult thirdA = cache.evaluate(*source, stackA);
    assert(thirdA.cacheHit);

    CachedEvaluationResult firstC = cache.evaluate(*source, stackC);
    assert(firstC);
    assert(!firstC.cacheHit);
    assert(cache.entryCount() == 2U);
    assert(cache.retainedBytes() <= cache.budgetBytes());
    assert(cache.evictionCount() == 1U);

    // A was touched immediately before C, so deterministic LRU eviction removes B.
    CachedEvaluationResult secondB = cache.evaluate(*source, stackB);
    assert(secondB);
    assert(!secondB.cacheHit);
    assert(cache.evictionCount() == 2U);

    // Cache ownership can disappear while an existing read-only snapshot remains valid.
    const auto pinnedSnapshot = firstA.mesh;
    assert(pinnedSnapshot != nullptr);
    const auto pinnedPosition = pinnedSnapshot->position(0);
    assert(pinnedPosition.has_value());
    cache.clear();
    assert(cache.entryCount() == 0U);
    assert(cache.retainedBytes() == 0U);
    const auto stillPinnedPosition = pinnedSnapshot->position(0);
    assert(stillPinnedPosition.has_value());
    assert(stillPinnedPosition->x == pinnedPosition->x);

    // Oversized snapshots are returned to the caller but never retained by the cache.
    EvaluationCache tinyCache(oneSnapshotBytes - 1U);
    CachedEvaluationResult oversizedFirst = tinyCache.evaluate(*source, stackA);
    assert(oversizedFirst);
    assert(!oversizedFirst.cacheHit);
    assert(!oversizedFirst.retainedByCache);
    assert(tinyCache.entryCount() == 0U);
    CachedEvaluationResult oversizedSecond = tinyCache.evaluate(*source, stackA);
    assert(oversizedSecond);
    assert(!oversizedSecond.cacheHit);
    assert(tinyCache.missCount() == 2U);

    // Authored edits change the key. The previous evaluated snapshot remains immutable.
    source = document.mesh(meshId);
    assert(source != nullptr);
    const EditableMesh* authored = document.authoredMesh(meshId);
    assert(authored != nullptr);
    const VertexId originalVertexId = authored->vertexIds().front();
    const std::uint64_t originalRevision = source->revision;

    EvaluationCache revisionCache(oneSnapshotBytes * 2U);
    CachedEvaluationResult beforeEdit = revisionCache.evaluate(*source);
    assert(beforeEdit);
    assert(!beforeEdit.cacheHit);
    const auto beforeEditPosition = beforeEdit.mesh->position(0);
    assert(beforeEditPosition.has_value());

    MeshHistory history;
    MoveVerticesCommand move({VertexPositionTarget{originalVertexId, {4.0F, 0.0F, 0.0F}}});
    assert(document.executeMeshCommand(meshId, history, move));
    assert(history.ownerDocumentRuntimeId() == document.runtimeId());

    source = document.mesh(meshId);
    assert(source != nullptr);
    assert(source->revision > originalRevision);

    CachedEvaluationResult afterEdit = revisionCache.evaluate(*source);
    assert(afterEdit);
    assert(!afterEdit.cacheHit);
    assert(afterEdit.mesh->cacheKey() != beforeEdit.mesh->cacheKey());
    const auto afterEditPosition = afterEdit.mesh->position(0);
    assert(afterEditPosition.has_value());
    assert(afterEditPosition->x == 4.0F);
    const auto preservedBeforeEdit = beforeEdit.mesh->position(0);
    assert(preservedBeforeEdit.has_value());
    assert(preservedBeforeEdit->x == beforeEditPosition->x);

    revisionCache.eraseMesh(document.runtimeId(), meshId);
    assert(revisionCache.entryCount() == 0U);
    assert(revisionCache.retainedBytes() == 0U);
    assert(afterEdit.mesh->position(0).has_value());

    // Shrinking to zero deterministically drops all cache ownership.
    revisionCache.setBudgetBytes(0U);
    CachedEvaluationResult zeroBudget = revisionCache.evaluate(*source);
    assert(zeroBudget);
    assert(!zeroBudget.retainedByCache);
    assert(revisionCache.entryCount() == 0U);

    const std::vector<const MeshModifier*> invalidStack{nullptr};
    CachedEvaluationResult invalid = revisionCache.evaluate(*source, invalidStack);
    assert(!invalid);
    assert(invalid.error == MeshEvaluationError::NullModifier);
    assert(invalid.modifierIndex.has_value());
    assert(*invalid.modifierIndex == 0U);

    // Runtime identity follows move construction, so existing cache and MeshHistory state
    // continues to address the moved-to Document rather than its old object address.
    EvaluationCache moveCache(oneSnapshotBytes * 2U);
    source = document.mesh(meshId);
    assert(source != nullptr);
    CachedEvaluationResult beforeDocumentMove = moveCache.evaluate(*source);
    assert(beforeDocumentMove);
    assert(!beforeDocumentMove.cacheHit);
    const RuntimeDocumentId originalRuntimeId = document.runtimeId();

    Document movedDocument = std::move(document);
    assert(movedDocument.runtimeId() == originalRuntimeId);
    assert(document.runtimeId() != originalRuntimeId);
    assert(document.validate());
    assert(movedDocument.validate());

    source = movedDocument.mesh(meshId);
    assert(source != nullptr);
    assert(source->ownerDocumentRuntimeId() == originalRuntimeId);
    CachedEvaluationResult afterDocumentMove = moveCache.evaluate(*source);
    assert(afterDocumentMove.cacheHit);
    assert(afterDocumentMove.mesh == beforeDocumentMove.mesh);
    assert(movedDocument.undoMeshCommand(meshId, history));

    // The moved-from Document is reset as a fresh valid lineage. It may reuse the same numeric
    // MeshId, but neither cache identity nor MeshHistory binding can mistake it for the old one.
    const MeshId movedFromMeshId = createQuad(document, 20.0F);
    assert(movedFromMeshId == meshId);
    const MeshBlock* movedFromSource = document.mesh(movedFromMeshId);
    assert(movedFromSource != nullptr);
    CachedEvaluationResult movedFromEvaluation = moveCache.evaluate(*movedFromSource);
    assert(movedFromEvaluation);
    assert(!movedFromEvaluation.cacheHit);
    assert(movedFromEvaluation.mesh->sourceDocumentRuntimeId() == document.runtimeId());
    const EditableMesh* movedFromAuthored = document.authoredMesh(movedFromMeshId);
    assert(movedFromAuthored != nullptr);
    const VertexId movedFromVertexId = movedFromAuthored->vertexIds().front();
    MoveVerticesCommand wrongDocumentMove({VertexPositionTarget{movedFromVertexId, {25.0F, 0.0F, 0.0F}}});
    assert(!document.executeMeshCommand(movedFromMeshId, history, wrongDocumentMove));

    // Move assignment transfers the same lineage too; the already-bound MeshHistory can redo
    // against the assigned-to object and rejects the freshly reset moved-from object.
    const RuntimeDocumentId moveAssignedRuntimeId = movedDocument.runtimeId();
    Document assignedDocument;
    assignedDocument = std::move(movedDocument);
    assert(assignedDocument.runtimeId() == moveAssignedRuntimeId);
    assert(movedDocument.runtimeId() != moveAssignedRuntimeId);
    assert(assignedDocument.redoMeshCommand(meshId, history));
    assert(!movedDocument.redoMeshCommand(meshId, history));

    // DocumentHistory uses the same runtime lineage identity rather than a Document address.
    Document metadataDocument;
    const MeshId metadataMeshId = createQuad(metadataDocument, 30.0F);
    const ObjectId metadataObjectId = metadataDocument.createObject("Before", metadataMeshId);
    assert(metadataObjectId);
    DocumentHistory documentHistory;
    RenameObjectCommand rename(metadataObjectId, "After");
    assert(documentHistory.execute(metadataDocument, rename));
    const RuntimeDocumentId metadataRuntimeId = metadataDocument.runtimeId();
    assert(documentHistory.ownerDocumentRuntimeId() == metadataRuntimeId);

    Document movedMetadataDocument = std::move(metadataDocument);
    assert(movedMetadataDocument.runtimeId() == metadataRuntimeId);
    assert(metadataDocument.runtimeId() != metadataRuntimeId);
    assert(documentHistory.undo(movedMetadataDocument));
    const ObjectBlock* beforeRename = movedMetadataDocument.object(metadataObjectId);
    assert(beforeRename != nullptr);
    assert(beforeRename->name == "Before");

    Document assignedMetadataDocument;
    assignedMetadataDocument = std::move(movedMetadataDocument);
    assert(assignedMetadataDocument.runtimeId() == metadataRuntimeId);
    assert(movedMetadataDocument.runtimeId() != metadataRuntimeId);
    assert(documentHistory.redo(assignedMetadataDocument));
    const ObjectBlock* afterRename = assignedMetadataDocument.object(metadataObjectId);
    assert(afterRename != nullptr);
    assert(afterRename->name == "After");
    assert(!documentHistory.undo(movedMetadataDocument));

    return 0;
}
