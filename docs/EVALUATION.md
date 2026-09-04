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
initial EvaluatedMesh
        |
        v
ordered modifier stack
        |
        +--> Transform
        +--> Mirror / Weld
        +--> Triangulate
        |
        v
Derived Shading Normals
        |
        v
immutable EvaluatedMesh snapshot
        |
        +--> EvaluationCache (optional bounded reuse)
        +--> future tangent generation
        +--> future renderer/export upload
```

Nothing in `vortex_core` depends on the evaluator, cache, or renderer.

## Current evaluated representation

Evaluation starts with a deterministic one-to-one conversion from a validated `EditableMesh` and preserves source runtime Document lineage, source `MeshId`, source mesh revision, source Vertex/Edge/Face/Corner IDs, generic attribute layers, authored n-gon boundaries, face cycles, and radial edge topology.

Generated topology is independent from authored container addresses and hash nodes.

## Runtime Document identity

Stable IDs are scoped to one authored `Document`; separate Documents may legitimately contain the same numeric `MeshId`, revision, or element IDs. Evaluation therefore also carries a process-local `RuntimeDocumentId` identifying the live Document lineage that owns a `MeshBlock`.

`RuntimeDocumentId` is runtime-only state:

- it is never serialized into a project,
- every newly constructed live Document receives a distinct non-zero value,
- move construction and move assignment transfer the lineage identity with the authored state,
- the moved-from Document is reset to a fresh valid empty lineage,
- a resident `MeshBlock` must carry the same runtime lineage as its owning Document,
- manually detached `MeshBlock` values without an owning runtime identity are rejected by evaluation rather than entering cache identity accidentally.

This prevents a cache shared by multiple open Documents from treating otherwise identical `{MeshId, revision, modifiers}` tuples as the same authored source.

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

Modifiers and final derived stages receive controlled internal mutation access through the evaluation layer only. Editor operations still modify authored data exclusively through commands/history.

An older evaluated snapshot remains valid as its own immutable value. Later authored edits, modifier evaluations, cache eviction, cache clearing, or moving the owning Document do not silently mutate a snapshot still held by a consumer.

## Authored revision discipline

Evaluation identity depends on `MeshBlock::revision`, so authored geometry and shading state must not be mutated without advancing that revision.

`MeshBlock` keeps its owning `std::unique_ptr<EditableMesh>` private and exposes only `const EditableMesh* authoredMesh() const`. Const datablock access propagates constness to the authored payload. Normal mutation remains behind `Document` mesh command/history operations, which advance mesh/document revisions.

This rule prevents stale cache hits caused by untracked authored mutation.

## Modifier stack

`MeshModifier` is the non-destructive modifier contract. Each modifier provides a stable type, human-readable name, deterministic configuration token, `apply(EvaluatedMesh&)`, and focused structured failure information.

Modifier configurations are immutable values after construction in the current design. Modifiers execute strictly in stack order, and order is included in evaluation identity.

### Transform modifier

`TransformModifier` supports translation, XYZ Euler rotation in radians, and non-uniform scale. Authored positions remain unchanged.

Existing normal attributes, when present on Vertex or Corner domains during intermediate evaluation, use inverse scale followed by the same rotation and normalization. Standard shading normals are then regenerated from the final evaluated geometry after the modifier stack, so stale authored/bootstrap normal values never define completed evaluated shading.

Zero/near-zero scale components and non-finite transform values are rejected.

```text
position -> scale -> rotate X -> rotate Y -> rotate Z -> translate
```

### Mirror modifier v0.2

`MirrorModifier` supports X/Y/Z axes, an explicit plane offset, source-ID preserving generated topology, reflected positions/intermediate normal vectors, reversed mirrored winding, and optional deterministic seam welding.

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
8. Intermediate Vertex/Corner normal data is reflected when present.
9. Radial rings are rebuilt globally after seam reuse and may form supported non-manifold rings.
10. Authored stable IDs are never changed.

Standard Corner normals are regenerated after Mirror/Weld finishes, so final shading describes the generated seam topology rather than relying on incremental normal maintenance.

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

Generated diagonals default to `sharp == false`, so triangulation does not introduce an artificial shading split into a smooth source surface.

Triangulate currently assumes a simple polygon boundary. Broad support for self-intersecting polygons is not implied.

#### Attribute remapping

Topology-generating evaluation uses `AttributeSet::remapDomain()` for Face and Corner domains. The operation copies attribute storage directly from source-index mappings layer-by-layer instead of materializing one heavyweight `AttributeRow` object per generated corner.

This keeps the generic attribute system intact while reducing temporary memory pressure for large generated meshes, particularly on 32-bit Android.

## Derived Shading Normals v0.1

Standard shading normals are a final derived evaluation stage, **not a `MeshModifier`**.

`DerivedNormalsGenerator` runs after the complete modifier stack and writes the completed Corner-domain `normal : Vec3` layer. Temporary face normals, corner angles, edge-use counts, and smoothing-fan data are released after generation; evaluated snapshots retain only the final Corner normals.

Authoring controls are:

- Edge `sharp : Bool`, default `false`.
- Face `sharp_face : Bool`, semantic default `true` when the layer is absent.

Flat faces use their geometric Newell normal at every corner. Smooth faces form angle-weighted normal fans around each evaluated vertex. Smoothing crosses an edge only when that edge has exactly two face uses, is not sharp, and both incident faces are smooth. Boundary, sharp, flat-face, and non-manifold edges therefore stop smoothing deterministically.

`SetEdgeSharpCommand` and `SetFaceSharpCommand` store tiny stable-ID + before/after boolean history records. Through the Document bridge they advance the Mesh revision, automatically invalidating revision-keyed evaluated cache results.

Degenerate/non-finite geometry and invalid shading attribute types produce focused `NormalGenerationError` values rather than NaN/Inf output.

The early authored Corner `normal` bootstrap layer is not authoritative standard shading truth; completed evaluated normals overwrite it. Future custom/split-normal authoring must use an explicit separate semantic such as `custom_normal`.

See `docs/SHADING_NORMALS.md` for the complete normal/shading contract and memory rules.

## Error model

Evaluation reports focused structured errors such as missing/invalid authored geometry, invalid source runtime identity, generated index overflow, missing topology references, null modifiers, modifier failure, and derived-normal generation failure.

Modifier failures additionally report `ModifierApplyError` and the failing stack index. Current structured modifier failures include invalid transforms, invalid Mirror/weld settings, invalid generated topology, generated-topology overflow, missing position data, attribute-copy failure, and `TriangulationFailed`.

`MeshEvaluationError::NormalGenerationFailed` additionally carries a focused `NormalGenerationError` through both direct and cached evaluation results.

The evaluator intentionally avoids an exception-heavy result architecture.

## Revision and cache identity

Every evaluated snapshot exposes:

```text
source RuntimeDocumentId
+ source MeshId
+ source MeshBlock revision
+ ordered modifier-stack revision
```

The runtime Document identity disambiguates different open Documents that can legally reuse the same numeric stable IDs. The modifier-stack revision hashes stable modifier type and configuration in stack order.

Consequences:

- same live Document lineage + same authored revision + same ordered modifiers => same key,
- same numeric MeshId/revision in a different Document => different key,
- authored geometry or shading changes => different key,
- modifier setting changes => different key,
- Mirror weld setting/tolerance changes => different key,
- adding/removing/reordering Triangulate => different key,
- moving a Document preserves the key because runtime lineage moves with the authored state,
- reusing a moved-from Document produces a fresh lineage and therefore cannot impersonate the moved state.

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
- `eraseMesh(RuntimeDocumentId, MeshId)` releases every retained revision/modifier result for exactly one authored mesh in one live Document lineage,
- `clear()` releases all cache-owned evaluated snapshots,
- hit, miss, and budget-eviction counters are exposed for diagnostics/benchmarking.

### Deterministic eviction

v0.1 uses deterministic least-recently-used eviction. Entries carry a monotonic use serial; the smallest serial is evicted first.

The cache intentionally uses a small `std::vector<Entry>` with linear key lookup instead of adding another hash table plus linked-list allocation graph. Under a strict byte budget, evaluated snapshots are expected to dominate memory and entry counts are expected to remain small. This can be revisited only with benchmark evidence.

### Byte accounting

`EvaluatedMesh::estimatedRetainedBytes()` includes:

- the `EvaluatedMesh` object,
- Vertex/Edge/Face/Corner vector capacities,
- dynamic generic attribute storage, including derived Corner normals.

Arithmetic saturates at `std::numeric_limits<std::size_t>::max()`. The estimate is intentionally allocator-agnostic and conservative; it is a stable budgeting metric, not a claim of allocator-exact heap telemetry.

### Cache ownership

The cache stores `std::shared_ptr<const EvaluatedMesh>` deliberately. This is one of the few places where shared ownership is appropriate: a renderer/export reader may still be consuming an immutable snapshot when the cache decides to evict it.

Eviction releases **only cache ownership**. A reader that already holds the snapshot remains valid.

The cache byte budget therefore bounds memory retained by the cache itself. It cannot bound memory that other subsystems intentionally pin through their own shared references. Renderer/export layers must keep their own in-flight snapshot counts bounded.

### Threading

Evaluation and `EvaluationCache` are single-threaded in v0.1. Runtime Document identity allocation follows the same current single-threaded authoring contract. No method promises concurrent mutation safety. Parallel/background evaluation remains deferred until deterministic invalidation and ownership behavior have been proven further.

## Modifier roadmap

1. Transform **implemented**
2. Mirror no-weld **implemented**
3. Mirror weld/merge v0.2 **implemented**
4. Triangulate v0.1 **implemented**
5. Bevel
6. Subdivision

Derived Shading Normals v0.1 is **implemented as a final evaluation stage**, not a modifier.

Topology-generating modifiers must preserve meaningful source mappings and must never write generated topology back into authored meshes.

## Testing and performance gate

Evaluation coverage proves authored/evaluated separation, source mappings, packed generated topology, Transform behavior, modifier ordering/cache identity, Mirror no-weld/weld behavior, Triangulate behavior, bounded cache behavior, runtime Document identity separation, Document move lineage behavior, and derived shading normals including:

- exact-key cache hits,
- same-numbered MeshIds/revisions from different Documents remaining distinct,
- detached MeshBlocks without an owning runtime identity being rejected,
- runtime identity following Document move construction and move assignment,
- DocumentHistory and MeshHistory following moved authored state while rejecting unrelated/moved-from lineages,
- modifier-configuration separation,
- deterministic LRU eviction,
- explicit retained-byte budget enforcement,
- oversized-result non-retention,
- zero-budget operation,
- authored-revision invalidation after geometry and shading commands,
- immutable old snapshots after authored edits,
- cache clear/per-mesh invalidation while externally held snapshots remain valid,
- invalid modifier diagnostics,
- concave n-gon ear clipping,
- exact `n - 2` triangle count,
- Face/Corner source mappings,
- material and UV remapping,
- intentionally source-less generated diagonals,
- flat and fully smooth face normals,
- reversed winding,
- sharp-edge fan splitting,
- Mirror Weld + Triangulate normals,
- non-manifold smoothing boundaries,
- non-uniform Transform geometry,
- explicit degenerate-face normal failure.

`vortex_eval_bench` separately times derived-normal generation on a smooth manifold strip and emits JSON alongside the existing core benchmark. Normal CI runs the 1,000-corner smoke case; manual larger requested profiles record explicit caps while correctness-first authoring setup remains nonlinear.

The evaluator/cache target is compiled by GCC, Clang, Android ARMv7, and Android ARM64 and is exercised under ASan/UBSan and clang-tidy through normal CI.
