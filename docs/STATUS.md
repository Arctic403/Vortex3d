# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

Vortex3D remains in the evaluated-geometry/product-foundation stage. The portable authoring kernel, command/history model, modifier pipeline, bounded evaluation cache, and derived shading normals are live. The current post-audit hardening pass is strengthening correctness boundaries before renderer and Android product layers build on them.

Post-audit hardening status:

- [x] Phase 1 — runtime Document identity and cross-Document cache/history safety.
- [x] Phase 2 — authored/evaluated validator hardening merged and regression-covered.
- [x] Phase 3 — bounded change journal, mesh evaluation-revision split, and no-op mutation cleanup implemented and regression-covered.
- [x] Phase 4 — deterministic randomized history/evaluation stress coverage and audit documentation added without changing authored/evaluated ownership rules.
- [x] Phase 5 — unified editor history provides one runtime-lineage-bound, byte-budgeted chronological undo/redo timeline across Document and Mesh commands.
- [x] Phase 6 — in-tree repository protection adds read-only CI permissions, CODEOWNERS, repository-policy checks, and an explicit GitHub branch/ruleset contract.

## Foundation state

### Language and portability

- [x] C++20 required; extensions disabled.
- [x] GCC and Clang host builds/tests.
- [x] warnings-as-errors CI policy.
- [x] ASan + UBSan.
- [x] clang-tidy against a real compile database.
- [x] portable-core dependency scanner.
- [x] Android NDK `armeabi-v7a` compile gate with explicit 32-bit pointer-width validation.
- [x] Android NDK `arm64-v8a` compile gate with explicit 64-bit pointer-width validation.
- [x] no Android/JNI/Vulkan/WebView/platform framework dependency in portable engine code.

### Document, ownership, and runtime identity

- [x] Scene / Collection / Object / Mesh datablocks.
- [x] stable typed 64-bit authored IDs.
- [x] shared mesh instances through `MeshId` references.
- [x] explicit Make Unique behavior.
- [x] move-only `Document`.
- [x] `MeshBlock` uniquely owns authored `EditableMesh` through a private `unique_ptr`.
- [x] const `MeshBlock` access propagates constness to authored geometry.
- [x] process-local non-serialized `RuntimeDocumentId` distinguishes live Document lineages.
- [x] Document moves transfer runtime lineage with authored state and reset the moved-from Document to a fresh lineage.
- [x] resident MeshBlocks carry and validate the owning runtime Document identity.
- [x] `DocumentHistory` and `MeshHistory` bind by runtime lineage rather than C++ object address.
- [x] ordinary Document undo uses reversible deltas rather than retained whole-Document snapshots.
- [x] runtime Document change notifications are bounded and gap-aware rather than an unbounded vector.
- [x] Document and mesh histories have explicit retained-byte budgets.

### Mesh kernel

- [x] Vertex / Edge / Face / Corner domains.
- [x] stable IDs independent from packed storage.
- [x] n-gons, face cycles, radial cycles, manifold and supported non-manifold topology.
- [x] generic domain-qualified attributes.
- [x] add/remove topology operations.
- [x] stable-ID edge split.
- [x] face extrusion.
- [x] deterministic attribute copy/interpolation/compaction.
- [x] structured authored topology validation.
- [x] strict authored storage validation across order vectors, index maps, registries, record identity, allocator state, and duplicate edges.
- [x] deterministic randomized mutation coverage.

### Commands and history

- [x] command/transaction mutation boundary.
- [x] compact Document deltas.
- [x] bounded `MeshHistory`.
- [x] vertex movement before/after deltas.
- [x] exact-ID local topology history for face extrusion.
- [x] reversible Edge `sharp` and Face `sharp_face` boolean deltas.
- [x] repeated and stacked extrusion undo/redo tests.
- [x] mesh history bound to `{RuntimeDocumentId, MeshId}` through the Document bridge.

Whole meshes are not retained as normal undo steps. Some complex topology operations still use temporary whole-`EditableMesh` copies as operation-local rollback guards; those remain measured performance debt, not history architecture.


### Change tracking and revision semantics

The runtime `Document` change journal has a configurable 256 KiB default logical payload budget and discards oldest events deterministically. `changesSince()` returns a gap-aware result: if the caller asks from a revision older than the retained floor, `complete()` is false and the consumer must resync from current state. Transaction/DocumentHistory batches defer pruning so rollback cannot lose the prior event suffix.

`MeshBlock::revision` is the general datablock revision. `MeshBlock::evaluationRevision()` advances only for authored geometry/shading changes. Mesh rename therefore advances metadata/Document revision without invalidating an otherwise identical evaluated snapshot. No-op mesh commands create no history record, emit no change event, and advance no revision. The first `sharp_face=false` edit materializes the optional Face layer; undo removes that layer again when it did not previously exist.

See `docs/CHANGE_TRACKING.md`.

## Validation hardening

### Authored

`EditableMesh::validate()` remains the established topology/attribute validator used by current mutation paths.

`EditableMesh::validateStrict()` is the full trust-boundary gate. It adds checks for:

- order/index/registry size agreement,
- duplicate or zero stable IDs in order storage,
- exact packed-index agreement,
- registry records matching their keys,
- registry/index completeness,
- monotonic allocator state,
- duplicate undirected authored edges,
- repeated short face cycles that could otherwise masquerade as a valid `cornerCount` cycle.

`MeshEvaluator` requires strict authored validation before packing geometry. Future import/deserialization must use the same strict boundary before reconstructed storage is trusted.

### Evaluated

`EvaluatedMeshValidator` is the centralized generated-geometry gate. It validates:

- runtime/source identity,
- attribute-domain sizes,
- uint32 packed-count bounds,
- edge endpoint ranges and duplicate packed edges,
- face cycles and corner ownership,
- next/prev consistency,
- corner-edge boundary consistency,
- radial-ring coverage and mutual links,
- unreachable generated corners.

The evaluator validates:

```text
strict authored source
-> packed evaluated conversion
-> after every modifier
-> after derived normal generation
-> immutable snapshot
```

A modifier that reports success while leaving malformed topology is converted into structured `ModifierFailed / GeneratedTopologyInvalid` output with the failing modifier index and first `EvaluatedMeshValidationCode`.

`EvaluationCache` preserves that validation diagnostic on failed cache-backed evaluations.

See `docs/VALIDATION.md` for the exact invariant and diagnostic contract.

## Validation and evaluation coverage

CTest registers **24 native suites** after the Phase 5-6 hardening patch.

Coverage includes:

- triangle / quad / n-gon / cube authored topology,
- shared and supported non-manifold radial edges,
- edge split and face extrusion,
- attribute compaction,
- exact-ID history replay,
- deterministic randomized mutation,
- deliberate broken face/radial cycles,
- invalid endpoints and invalid face sizes,
- unreachable corners and attribute-size mismatch,
- duplicate/reversed edge prevention,
- illegal deletion order,
- deliberate order/index/registry disagreement,
- duplicate authored stable IDs,
- registry-record identity mismatch,
- allocator rewind detection,
- duplicate authored undirected-edge corruption,
- strict short-cycle detection,
- authored-to-evaluated conversion and revision behavior,
- Transform source immutability and cache invalidation,
- Mirror no-weld and explicit weld behavior,
- manifold and non-manifold welded seams,
- deterministic concave n-gon triangulation,
- generated source mappings and attribute remapping,
- `Transform -> Mirror Weld -> Triangulate`,
- evaluation cache hit/miss, LRU, byte budgets, and snapshot lifetime,
- cross-Document cache identity and Document move lineage,
- derived flat/smooth Corner normals and shading boundaries,
- deliberate evaluated edge/face/radial/attribute/orphan-corner corruption,
- rejection of a modifier that returns success with invalid generated topology,
- identical structured validation diagnostics through direct evaluation and `EvaluationCache`,
- 200 deterministic randomized Transform/Mirror/Triangulate evaluation stacks proving repeated-evaluation equivalence and authored-source immutability.
- one chronological `EditorHistory` timeline spanning Document and Mesh commands, including mixed undo/redo ordering, redo branching, budget eviction, and cross-Document rejection.

Private corruption access exists only in test builds through `VORTEX_ENABLE_TEST_HOOKS`.

## Evaluated geometry, modifiers, cache, and shading

A separate portable `vortex_eval` target owns rebuildable generated geometry. Persistent authored IDs remain 64-bit; generated connectivity uses checked packed uint32 indices and is not persistent identity.

Implemented modifiers:

1. `TransformModifier` — translation, XYZ rotation, non-uniform scale.
2. `MirrorModifier` v0.2 — X/Y/Z axis, plane offset, no-weld duplication, optional explicit seam welding.
3. `TriangulateModifier` v0.1 — deterministic evaluated-only triangulation with concave n-gon support.

Triangulate uses deterministic ear clipping after dominant-axis projection from a Newell face normal. Authored n-gons remain unchanged.

Generated triangle faces retain source `FaceId`; generated corners retain source `CornerId`; existing boundary edges retain source `EdgeId`; generated diagonal edges intentionally use invalid source `EdgeId` because no authored edge exists.

### Evaluation cache v0.1

Every evaluated snapshot/cache entry is identified by:

```text
RuntimeDocumentId
+ MeshId
+ source Mesh evaluation revision
+ ordered modifier-stack revision
```

`EvaluationCache` retains immutable `EvaluatedMesh` snapshots under an explicit byte budget.

- default budget: 16 MiB,
- deterministic LRU eviction,
- exact-key hit reuse,
- zero budget disables retention without disabling evaluation,
- oversized evaluated results are returned but not cached,
- `eraseMesh(RuntimeDocumentId, MeshId)` invalidates one authored mesh in one live Document lineage,
- externally held `shared_ptr<const EvaluatedMesh>` snapshots remain valid after cache eviction,
- budget accounting covers cache-retained ownership only,
- hit/miss/eviction counters are exposed.

The cache is intentionally single-threaded in v0.1.

### Derived shading normals v0.1

Standard `normal` is a final derived Corner attribute, not authoritative authored truth.

- Newell geometric face normals,
- flat by default,
- smooth fans use corner-angle weighting,
- smoothing crosses only unsharp exactly-two-use manifold edges between smooth faces,
- boundary/non-manifold/sharp edges stop smoothing,
- shading commands use compact reversible deltas and revision-based cache invalidation,
- new authored meshes do not bootstrap standard Corner normals,
- final evaluation overwrites tolerated legacy/imported Corner `normal` data,
- structured failure prevents NaN output from degenerate geometry.

See `docs/EVALUATION.md`, `docs/SHADING_NORMALS.md`, `docs/OWNERSHIP.md`, and `docs/VALIDATION.md`.

## Performance and memory measurement

Optional Release benchmark targets remain available for authored and evaluated work.

Initial 64-bit layout observations remain:

```text
MeshVertex              8 bytes
MeshEdge               32 bytes
MeshFace               24 bytes
MeshCorner             64 bytes
AttributeLayer        112 bytes
vertex hash payload    16 bytes
edge hash payload      40 bytes
face hash payload      32 bytes
corner hash payload    72 bytes
bucket pointer           8 bytes
```

These measurements keep corner/hash-heavy authored storage on the optimization watchlist, but they do not justify a container rewrite without larger benchmark evidence.

Strict validation is intentionally whole-domain and correctness-first. If validation becomes a measured large-mesh bottleneck, optimize the implementation without weakening the invariant contract.

## Current CI gate

Normal Core CI has **9 jobs**:

1. repository policy,
2. portable dependency boundary,
3. GCC host build/tests,
4. Clang host build/tests,
5. ASan + UBSan,
6. clang-tidy,
7. Android ARMv7 32-bit cross-compile,
8. Android ARM64 cross-compile,
9. Release core + evaluation benchmark smoke/artifact.

A hardening patch is not merge-ready until all nine jobs pass on its exact head. See `docs/REPOSITORY_PROTECTION.md` for the in-tree policy and required GitHub-hosted branch/ruleset settings.

## Deferred intentionally

Still deliberately deferred:

- custom persistent allocators,
- broad `std::pmr` conversion,
- authored storage rewrite without benchmark evidence,
- exception-heavy result architecture,
- parallel evaluation/cache mutation before a deterministic concurrency model exists,
- renderer ownership of authored data,
- allocator-exact cache telemetry,
- broad self-intersecting polygon triangulation guarantees,
- custom/split-normal authoring,
- Weighted Normal modifier behavior,
- automatic angle-threshold smoothing,
- MikkTSpace tangents until material/export requirements require them.

## Product engineering target after hardening gates

The next product-facing architecture target remains a narrow renderer-facing immutable snapshot/upload contract that consumes evaluated triangle topology, positions, Corner normals, UVs, and material assignments without gaining ownership of authored truth.

No renderer or Android UI code should bypass the evaluator/validation boundary.

## First meaningful product milestone

```text
Launch native APK
-> Create cube
-> Enter Edit Mode
-> Select face
-> Extrude
-> Undo
-> Redo
-> Add Mirror modifier
-> Save
-> Kill app
-> Reopen
-> Export GLB
-> Re-import and validate
```
