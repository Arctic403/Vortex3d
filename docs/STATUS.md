# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

**Phase 4 is active: bounded evaluated-geometry reuse is live; next is Recalculate Normals, then renderer-facing snapshot/upload preparation.**

The portable foundation is strong enough to build upward from without rewriting the authoring kernel. Document ownership/history, mesh topology, Android dual-ABI compilation, static analysis, sanitizer coverage, performance instrumentation, deliberate corruption testing, authored-to-evaluated conversion, ordered modifier evaluation, Transform, Mirror, Mirror welding, non-destructive Triangulate, and a byte-budgeted evaluation cache are all live.

## Foundation state

### Language and portability

- [x] C++20 required; extensions disabled.
- [x] GCC + Clang warnings-as-errors.
- [x] ASan + UBSan.
- [x] clang-tidy against a real compile database.
- [x] portable-core dependency scanner.
- [x] Android NDK `armeabi-v7a` compile gate with explicit 32-bit pointer-width validation.
- [x] Android NDK `arm64-v8a` compile gate with explicit 64-bit pointer-width validation.
- [x] no Android/JNI/Vulkan/WebView/platform framework dependency in portable engine code.

### Document and ownership

- [x] Scene / Collection / Object / Mesh data blocks.
- [x] stable typed 64-bit IDs.
- [x] shared mesh instances through `MeshId` references.
- [x] explicit Make Unique behavior.
- [x] move-only `Document`.
- [x] `MeshBlock` uniquely owns authored `EditableMesh` data through a private `unique_ptr`.
- [x] const `MeshBlock` access propagates constness to authored geometry.
- [x] ordinary Document undo uses reversible deltas rather than retained whole-Document snapshots.
- [x] global Document history has an explicit retained-byte budget.

### Mesh kernel

- [x] Vertex / Edge / Face / Corner domains.
- [x] stable IDs independent from packed storage.
- [x] n-gons, face cycles, radial cycles, manifold and supported non-manifold topology.
- [x] generic domain-qualified attributes.
- [x] conservative add/remove topology operations.
- [x] stable-ID edge split.
- [x] face extrusion.
- [x] deterministic attribute copy/interpolation/compaction.
- [x] structured mesh validation.
- [x] deterministic randomized mutation coverage.

### Commands and history

- [x] command/transaction mutation boundary.
- [x] compact Document deltas.
- [x] bounded `MeshHistory`.
- [x] vertex movement before/after deltas.
- [x] exact-ID local topology history for face extrusion.
- [x] repeated and stacked extrusion undo/redo tests.
- [x] mesh history bound to owning Document instance + `MeshId` through the Document bridge.

Whole meshes are not retained as normal undo steps. Some complex topology operations still use temporary whole-`EditableMesh` copies as atomic rollback guards; those remain performance debt to profile rather than history architecture.

### Validation and evaluation coverage

CTest now registers **17 native suites**.

Coverage includes authored topology, deliberate corruption, command/history replay, evaluation, modifier-generated topology, and bounded evaluated-result reuse:

- triangle / quad / n-gon / cube,
- shared and non-manifold radial edges,
- edge split and face extrusion,
- attribute compaction,
- exact-ID history replay,
- deterministic randomized mutation,
- broken face/radial cycles,
- invalid endpoints / invalid face sizes / unreachable corners,
- attribute-size mismatch,
- duplicate/reversed edge prevention,
- illegal deletion order,
- authored-to-evaluated conversion and revision behavior,
- Transform source immutability and cache invalidation,
- Mirror no-weld and explicit weld behavior,
- seam projection / edge reuse / planar face suppression,
- manifold and non-manifold welded seams,
- concave n-gon ear clipping,
- exact `n - 2` generated triangle count,
- generated Face/Corner source mapping,
- material and UV remapping through triangulation,
- source-less generated diagonal edge identity,
- `Transform -> Mirror Weld -> Triangulate`,
- authored n-gon preservation,
- explicit degenerate-polygon triangulation failure,
- evaluation cache hit/miss behavior,
- deterministic LRU eviction,
- cache retained-byte budget enforcement,
- oversized result non-retention,
- zero-budget operation,
- authored-revision cache invalidation,
- old snapshot survival across edits and eviction,
- per-mesh explicit cache invalidation.

Private corruption access exists only in test builds through `VORTEX_ENABLE_TEST_HOOKS`.

### Performance and memory measurement

- [x] optional `vortex_mesh_bench` Release target.
- [x] vertex / edge / face creation.
- [x] topology traversal.
- [x] edge split / face extrusion.
- [x] attribute lookup / mutation.
- [x] vertex movement.
- [x] undo / redo.
- [x] full validation.
- [x] JSON output and Actions artifacts.
- [x] manual 10k / 100k / 1M requested profiles where practical.
- [x] normal CI benchmark smoke.

Initial 64-bit layout observations:

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

These measurements make corner/hash-heavy storage a future optimization candidate, but they do not justify a container rewrite by themselves.

## Evaluated geometry, modifiers, and cache

A separate portable `vortex_eval` target owns rebuildable generated geometry.

Persistent authored IDs remain 64-bit. Generated connectivity uses checked packed 32-bit indices and is never persistent identity.

Implemented modifiers:

1. `TransformModifier` — translation, XYZ rotation, non-uniform scale.
2. `MirrorModifier` v0.2 — X/Y/Z axis, plane offset, no-weld duplication, optional explicit seam welding.
3. `TriangulateModifier` v0.1 — deterministic evaluated-only triangulation with concave n-gon support.

Triangulate uses deterministic ear clipping after dominant-axis projection from a Newell face normal. Authored n-gons are not changed.

Generated triangle faces retain their source `FaceId`; generated corners retain their source `CornerId`. Existing boundary edges retain source `EdgeId` mappings. New diagonal edges intentionally have an invalid source `EdgeId` because there is no corresponding authored edge.

`AttributeSet::remapDomain()` rebuilds generated Face/Corner attribute storage directly from source-index mappings, avoiding per-corner `AttributeRow` materialization during topology-generating evaluation.

Every evaluated snapshot carries:

```text
MeshId + authored Mesh revision + ordered modifier-stack revision
```

### Evaluation Cache v0.1

`EvaluationCache` retains immutable `EvaluatedMesh` snapshots under an explicit byte budget.

- default budget: **16 MiB**,
- caller-configurable budget,
- deterministic LRU eviction,
- exact-key hit reuse,
- zero budget disables retention but not evaluation,
- oversized evaluated results are returned but not cached,
- `eraseMesh(MeshId)` invalidates all retained revisions/stacks for one authored mesh,
- externally held `shared_ptr<const EvaluatedMesh>` snapshots remain valid after cache eviction,
- budget accounting covers cache-retained ownership only,
- `EvaluatedMesh::estimatedRetainedBytes()` uses topology capacities + attribute dynamic storage with saturating `size_t` arithmetic,
- hit/miss/eviction counters are available for diagnostics.

The cache is intentionally single-threaded in v0.1.

The cache patch also hardened authored ownership: callers can no longer obtain mutable geometry through a `const MeshBlock` and bypass revision advancement.

See `docs/EVALUATION.md` and `docs/OWNERSHIP.md`.

## Current CI gate

Normal Core CI has **8 jobs**:

1. portable dependency boundary,
2. GCC host build/tests,
3. Clang host build/tests,
4. ASan + UBSan,
5. clang-tidy,
6. Android ARMv7 32-bit cross-compile,
7. Android ARM64 cross-compile,
8. Release benchmark smoke + artifact.

Evaluation Cache PR #9 passed all eight gates before merge, including suite #17 under sanitizers, clang-tidy, and both Android ABIs.

## Deferred intentionally

Still deliberately deferred:

- custom persistent allocators,
- broad `std::pmr` conversion,
- ECS replacement of authoring data,
- plugin ABI,
- storage rewrite without larger benchmark evidence,
- exception-heavy result architecture,
- renderer ownership of authored data,
- parallel evaluation/cache mutation before a deterministic concurrency model exists,
- allocator-exact cache telemetry,
- unbounded renderer/export snapshot pinning,
- broad self-intersecting polygon triangulation guarantees.

## Next engineering target

1. Implement **Recalculate Normals** as derived evaluated attributes.
2. Define explicit flat versus smooth normal policy rather than inheriting accidental authored data.
3. Add cube, mirrored seam, non-manifold, and triangulated normal fixtures.
4. Prepare a narrow renderer-facing immutable evaluated snapshot/upload contract.
5. Keep storage optimization separate and evidence-driven from benchmark results.

No renderer or Android UI code should bypass the evaluator boundary.

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

The headless engine can now produce deterministic welded mirrored triangles and reuse them under a bounded memory policy while keeping the editable source as n-gons. The next derived-geometry step is trustworthy generated normals for render/export consumers.
