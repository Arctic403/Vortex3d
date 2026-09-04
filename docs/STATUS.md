# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

**Phase 4 is active: topology-generating modifiers are live; next is Triangulate, then bounded evaluation caching.**

The portable foundation is now strong enough to build upward from without rewriting the authoring kernel. Document ownership/history, mesh topology, Android dual-ABI compilation, static analysis, sanitizer coverage, performance instrumentation, deliberate corruption testing, authored-to-evaluated conversion, ordered modifier evaluation, Transform, Mirror, and explicit Mirror seam welding are all live.

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
- [x] `MeshBlock` uniquely owns authored `EditableMesh` data.
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

CTest now registers **15 native suites**.

Coverage includes authored topology, deliberate corruption, command/history replay, evaluation, and modifier-generated topology:

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
- Mirror axis / plane offset / source mappings,
- reversed mirrored face cycles and face-edge continuity,
- rebuilt mirrored radial rings,
- reflected normal data,
- no-weld Mirror behavior,
- explicit Mirror weld tolerance and exact-only tolerance,
- seam projection without authored mutation,
- seam-edge reuse,
- fully planar face suppression,
- reversed Corner/UV attribute mapping,
- two-use manifold welded seams,
- four-use non-manifold welded seams,
- invalid weld diagnostics.

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

## Evaluated geometry and modifiers

A separate portable `vortex_eval` target owns rebuildable generated geometry.

Persistent authored IDs remain 64-bit. Generated connectivity uses checked packed 32-bit indices and is never persistent identity.

Implemented modifiers:

1. `TransformModifier` — translation, XYZ rotation, non-uniform scale.
2. `MirrorModifier` v0.2 — X/Y/Z axis, plane offset, no-weld duplication, plus optional explicit seam welding.

Mirror welding uses `MirrorWeldSettings { enabled, tolerance }`. There is **no hidden epsilon**. `distance <= tolerance` welds; `tolerance == 0` means exact-only welding.

The deterministic survivor is the source evaluated vertex. It is projected exactly onto the mirror plane in evaluated output only. Seam edges are reused when both endpoints weld; fully seam faces are not duplicated; mirrored Corner attributes follow reversed source-corner order; radial rings are rebuilt globally and may form supported non-manifold rings.

Authored `EditableMesh` remains unchanged throughout evaluation.

Every evaluated snapshot carries:

```text
MeshId + authored Mesh revision + ordered modifier-stack revision
```

Mirror axis, offset, weld enablement, and weld tolerance participate in evaluation identity. No retained evaluation cache exists yet.

See `docs/EVALUATION.md`.

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

Mirror weld PR #7 passed all eight gates before merge, including the dedicated seam/non-manifold suite under sanitizers and the ARMv7 32-bit build.

## Deferred intentionally

Still deliberately deferred:

- custom persistent allocators,
- broad `std::pmr` conversion,
- ECS replacement of authoring data,
- plugin ABI,
- storage rewrite without larger benchmark evidence,
- exception-heavy result architecture,
- renderer ownership of authored data,
- retained evaluation caches without explicit memory budgets,
- parallel evaluation before deterministic invalidation exists,
- broad spatial welding/deduplication unrelated to the configured Mirror seam.

## Next engineering target

1. Implement **Triangulate** as derived evaluated topology while preserving authored n-gons.
2. Define deterministic source mappings for generated triangles/corners.
3. Add concave n-gon and modifier-stack fixtures (`Transform -> Mirror Weld -> Triangulate`).
4. Add an explicitly byte-budgeted evaluation cache after topology-generating invalidation is proven.
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

The headless engine now has deterministic no-weld and welded Mirror behavior. The next derived-geometry step is Triangulate, which will prepare evaluated topology for render/export consumers without destroying authored n-gons.
