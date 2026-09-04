#include "vortex/core/document.hpp"
#include "vortex/eval/evaluation_cache.hpp"
#include "vortex/eval/modifier.hpp"
#include "vortex/mesh/command.hpp"
#include "vortex/mesh/editable_mesh.hpp"

#include <cassert>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

vortex::MeshId createQuad(vortex::Document& document) {
    using namespace vortex;

    EditableMesh mesh;
    const VertexId a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const VertexId b = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const VertexId c = mesh.addVertex({1.0F, 1.0F, 0.0F});
    const VertexId d = mesh.addVertex({0.0F, 1.0F, 0.0F});
    assert(mesh.addFace({a, b, c, d}));
    assert(mesh.validate());
    return document.createMesh("Cache Quad", std::move(mesh));
}

} // namespace

int main() {
    using namespace vortex;

    Document document;
    const MeshId meshId = createQuad(document);
    assert(meshId);

    const MeshBlock* source = document.mesh(meshId);
    assert(source != nullptr);

    MeshEvaluationResult baseline = MeshEvaluator::evaluate(*source);
    assert(baseline);
    assert(baseline.mesh.has_value());
    const std::size_t oneSnapshotBytes = baseline.mesh->estimatedRetainedBytes();
    assert(oneSnapshotBytes > 0U);
    assert(oneSnapshotBytes <= std::numeric_limits<std::size_t>::max() / 2U);

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
    const auto originalVertexId = source->authoredMesh->vertexIds().front();
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

    revisionCache.eraseMesh(meshId);
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

    return 0;
}
