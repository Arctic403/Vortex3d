# Evaluated Geometry Architecture

## Purpose

`EditableMesh` is durable authored state. `EvaluatedMesh` is rebuildable generated state for modifiers, derived geometry, caching, and rendering.

```text
Document / MeshBlock
        |
        v
EditableMesh          durable authored topology
        |
        v
vortex_eval
        |
        v
EvaluationCache       optional bounded reuse
        |
        v
EvaluatedMesh         generated topology/attributes
        |
        v
ordered modifier stack
        |
        +--> Transform
        +--> Mirror / Weld
        +--> Triangulate
        +--> future Recalculate Normals
        +--> future renderer upload
```

Nothing in `vortex_core` depends on the evaluator, cache, or renderer.

## Current evaluated representation

Evaluation starts with a deterministic one-to-one conversion from a validated `EditableMesh` and preserves source `MeshId`, source mesh revision, source Vertex/Edge/Face/Corner IDs, generic attribute layers, authored n-gon boundaries, face cycles, and radial edge topology.

Generated topology is independent from authored container addresses and hash nodes.

## Stable identity versus packed generated indices

Persistent public identity remains 64-bit and belongs to authored data. Evaluated connectivity uses checked `std::uint32_t` packed indices.

```text
stable source ID
      |
      v
Evaluated record
      |
      +--> 32-bit packed generated connectivity
```

- Packed indices are never serialized as persistent identity.
- Re-evaluation may repack generated geometry freely.
- Picking/diagnostics can map generated elements back to stable source IDs.
- One source ID may map to multiple generated elements after topology-generating modifiers.
- A generated element with no authored equivalent may carry an invalid source ID rather than inventing persistent identity.
- 32-bit connectivity reduces generated topology memory and is friendly to the required ARMv7 target.
- Evaluation/modifiers fail explicitly rather than silently truncating generated indices.

## Read-only external contract

`EvaluatedMesh` exposes const spans and const attributes to normal consumers. There is no editor mutation API.

Modifiers receive controlled internal mutation access through the evaluation layer only. Editor operations still modify authored data exclusively through commands/history.

An older evaluated snapshot remains valid as its own immutable value. Later authored edits, modifier evaluations, cache eviction, or cache clearing do not silently mutate a snapshot still held by a consumer.

## Authored revision discipline

Evaluation identity depends on `MeshBlock::revision`, so authored geometry must not be mutated without advancing that revision.

`MeshBlock` therefore keeps its owning `std::unique_ptr<EditableMesh>` private and exposes only `const EditableMesh* authoredMesh() const`. Const datablock access now propagates constness to the authored payload. Normal mutation remains behind `Document` mesh command/history operations, which advance mesh/document revisions.

This rule prevents stale cache hits caused by untracked authored mutation.

## Modifier stack

`MeshModifier` is the non-destructive modifier contract. Each modifier provides a stable type, human-readable name, deterministic configuration token, `apply(EvaluatedMesh&)`, and focused structured failure information.

Modifier configurations are immutable values after construction in the current design. Modifiers execute strictly in stack order, and order is included in evaluation identity.

### Transform modifier

`TransformModifier` supports translation, XYZ Euler rotation in radians, and non-uniform scale. Authored positions remain unchanged.

Normals, when present on Vertex or Corner domains, use inverse scale followed by the same rotation and normalization. Zero/near-zero scale components and non-finite transform values are rejected.

```text
position -> scale -> rotate X -> rotate Y -> rotate Z -> translate
```

### Mirror modifier v0.2

`MirrorModifier` supports X/Y/Z axes, an explicit plane offset, source-ID preserving generated topology, reflected positions/normals, reversed mirrored winding, and optional deterministic seam welding.

Mirror operates on the **current evaluated input**, so it can stack after Transform or another Mirror without touching authored topology.

Welding is controlled by `MirrorWeldSettings { enabled, tolerance }`. There is no hidden epsilon. A vertex welds when `abs(axis_coordinate - plane_offset) <= tolerance`; `tolerance == 0` means exact-only welding.

When welding is enabled:

1. The source evaluated vertex is the deterministic survivor.
2. A surviving seam vertex is projected exactly onto the configured plane in evaluated output only.
3. Its mirrored duplicate is omitted.
4. A seam edge whose endpoints both weld reuses the source evaluated edge.
5. A face fully contained in the seam is not duplicated back onto itself.
6. Other mirrored faces retain reversed winding.
7. Corner attributes are copied according to reversed source-corner order.
8. Vertex/Corner normal data is reflected when present.
9. Radial rings are rebuilt globally after seam reuse and may form supported non-manifold rings.
10. Authored stable IDs are never changed.

Welding is not broad spatial deduplication; it only merges each source vertex with its own mirrored counterpart at the configured plane.

### Triangulate modifier v0.1

`TriangulateModifier` converts the **current evaluated faces** into triangles while leaving the authored `EditableMesh` untouched. Authored n-gons therefore remain editable n-gons even when renderer/export-facing evaluated geometry is triangular.

The current triangulation contract is:

- triangles remain triangles,
- an n-gon with `n` corners produces `n - 2` generated triangles,
- simple concave n-gons use deterministic ear clipping rather than a naive fan,
- a Newell face normal chooses the dominant 2D projection plane for ear clipping,
- generated triangles retain the source `FaceId` of the evaluated face they derive from,
- each generated triangle corner retains the source `CornerId` of the corner supplying its face-varying attributes,
- boundary edges retain their existing source `EdgeId`,
- newly generated diagonal edges intentionally use an invalid source `EdgeId` because no authored edge exists,
- generated diagonal edge attributes start from the Edge-domain layer defaults,
- diagonal edges are reused by packed endpoint pair when shared by adjacent generated triangles,
- radial rings are rebuilt after the generated face/corner topology is replaced,
- degenerate polygons that cannot be triangulated fail with `TriangulationFailed` instead of emitting invalid triangles.

Triangulate currently assumes a simple polygon boundary. Broad support for self-intersecting polygons is not implied.

#### Attribute remapping

Topology-generating evaluation uses `AttributeSet::remapDomain()` for Face and Corner domains. The operation copies attribute storage directly from source-index mappings layer-by-layer instead of materializing one heavyweight `AttributeRow` object per generated corner.

This keeps the generic attribute system intact while reducing temporary memory pressure for large generated meshes, particularly on 32-bit Android.

## Error model

Evaluation reports focused structured errors such as missing/invalid authored geometry, generated index overflow, missing topology references, null modifiers, and modifier failure.

Modifier failures additionally report `ModifierApplyError` and the failing stack index. Current structured failures include invalid transforms, invalid Mirror/weld settings, invalid generated topology, generated-topology overflow, missing position data, attribute-copy failure, and `TriangulationFailed`.

The evaluator intentionally avoids an exception-heavy result architecture.

## Revision and cache identity

Every evaluated snapshot exposes:

```text
source MeshId
+ source MeshBlock revision
+ ordered modifier-stack revision
```

The modifier-stack revision hashes stable modifier type and configuration in stack order.

Consequences:

- same authored revision + same ordered modifiers => same key,
- authored changes => different key,
- modifier setting changes => different key,
- Mirror weld setting/tolerance changes => different key,
- adding/removing/reordering Triangulate => different key.

`MeshEvaluator::cacheKeyFor()` computes this identity without first constructing an `EvaluatedMesh`.

## Evaluation Cache v0.1

`EvaluationCache` retains immutable evaluated snapshots under an explicit byte budget. The default budget is **16 MiB** and callers may set a different value appropriate to device class/session policy.

### Retention rules

- cache entries are keyed by `EvaluationCacheKey`,
- exact-key hits return the same retained `shared_ptr<const EvaluatedMesh>`,
- misses evaluate normally and may retain the result,
- results larger than the entire cache budget are still returned to the caller but are not retained,
- a zero-byte budget disables retention while leaving evaluation functional,
- lowering the budget immediately evicts entries until retained bytes fit,
- `eraseMesh(MeshId)` releases every retained revision/modifier result for that authored mesh,
- `clear()` releases all cache-owned evaluated snapshots,
- hit, miss, and budget-eviction counters are exposed for diagnostics/benchmarking.

### Deterministic eviction

v0.1 uses deterministic least-recently-used eviction. Entries carry a monotonic use serial; the smallest serial is evicted first.

The cache intentionally uses a small `std::vector<Entry>` with linear key lookup instead of adding another hash table plus linked-list allocation graph. Under a strict byte budget, evaluated snapshots are expected to dominate memory and entry counts are expected to remain small. This can be revisited only with benchmark evidence.

### Byte accounting

`EvaluatedMesh::estimatedRetainedBytes()` includes:

- the `EvaluatedMesh` object,
- Vertex/Edge/Face/Corner vector capacities,
- dynamic generic attribute storage.

Arithmetic saturates at `std::numeric_limits<std::size_t>::max()`. The estimate is intentionally allocator-agnostic and conservative; it is a stable budgeting metric, not a claim of allocator-exact heap telemetry.

### Cache ownership

The cache stores `std::shared_ptr<const EvaluatedMesh>` deliberately. This is one of the few places where shared ownership is appropriate: a renderer/export reader may still be consuming an immutable snapshot when the cache decides to evict it.

Eviction releases **only cache ownership**. A reader that already holds the snapshot remains valid.

The cache byte budget therefore bounds memory retained by the cache itself. It cannot bound memory that other subsystems intentionally pin through their own shared references. Renderer/export layers must keep their own in-flight snapshot counts bounded.

### Threading

Evaluation and `EvaluationCache` are single-threaded in v0.1. No method promises concurrent mutation safety. Parallel/background evaluation remains deferred until deterministic invalidation and ownership behavior have been proven further.

## Modifier roadmap

1. Transform **implemented**
2. Mirror no-weld **implemented**
3. Mirror weld/merge v0.2 **implemented**
4. Triangulate v0.1 **implemented**
5. Recalculate Normals
6. Bevel
7. Subdivision

Topology-generating modifiers must preserve meaningful source mappings and must never write generated topology back into authored meshes.

## Testing gate

Evaluation coverage proves authored/evaluated separation, source mappings, packed generated topology, Transform behavior, modifier ordering/cache identity, Mirror no-weld/weld behavior, Triangulate behavior, and cache behavior including:

- exact-key cache hits,
- modifier-configuration separation,
- deterministic LRU eviction,
- explicit retained-byte budget enforcement,
- oversized-result non-retention,
- zero-budget operation,
- authored-revision invalidation after a mesh command,
- immutable old snapshots after authored edits,
- cache clear/per-mesh invalidation while externally held snapshots remain valid,
- invalid modifier diagnostics,
- concave n-gon ear clipping,
- exact `n - 2` triangle count,
- Face/Corner source mappings,
- material and UV remapping,
- intentionally source-less generated diagonals,
- `Transform -> Mirror Weld -> Triangulate`.

The evaluator/cache target is compiled by GCC, Clang, Android ARMv7, and Android ARM64 and is exercised under ASan/UBSan and clang-tidy through normal CI.
