# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

**Phase 4 is active: topology-generating modifiers, explicit Mirror welding rules, then Triangulate and bounded evaluation caching.**

The portable foundation is now strong enough to build upward from without rewriting the authoring kernel. Document ownership/history, mesh topology, Android dual-ABI compilation, static analysis, sanitizer coverage, performance instrumentation, deliberate corruption testing, authored-to-evaluated conversion, ordered modifier evaluation, Transform, and Mirror v0.1 are all live.

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
- [x] n-gons, face cycles, radial cycles, manifold and selected non-manifold topology.
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

### Validation hardening

CTest now registers **14 native suites**, including evaluator, modifier-stack, and Mirror generated-topology coverage.

Coverage includes valid and deliberately malformed topology plus evaluated/modifier behavior:

- triangle / quad / n-gon / cube,
- shared and 3-face non-manifold radial edges,
- edge split and face extrusion,
- attribute compaction,
- exact-ID history replay,
- deterministic randomized mutation,
- broken face cycles,
- broken radial cycles,
- invalid endpoints,
- invalid face sizes,
- unreachable corners,
- attribute-size mismatch,
- duplicate/reversed edge prevention,
- illegal deletion order,
- authored-to-evaluated conversion and revision behavior,
- Transform source immutability,
- modifier ordering,
- modifier cache-key invalidation,
- invalid/null modifier diagnostics,
- Mirror axis and plane offset,
- mirrored source-ID mapping,
- reversed mirrored face cycles,
- mirrored face-edge continuity,
- rebuilt mirrored radial rings,
- reflected normal data,
- no-weld mirror-plane behavior,
- invalid Mirror diagnostics.

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

## Evaluated geometry and modifier boundary

A separate portable `vortex_eval` target is live.

`MeshEvaluator` converts a validated `MeshBlock` / `EditableMesh` into an `EvaluatedMesh` containing:

- source `MeshId`,
- source mesh revision,
- source Vertex/Edge/Face/Corner IDs,
- copied generic attribute layers,
- preserved n-gon boundaries,
- preserved face and radial connectivity,
- checked 32-bit packed generated topology references.

Persistent authored IDs remain 64-bit. The 32-bit evaluated indices are rebuildable implementation detail and are never persistent identity.

`MeshModifier` defines the ordered non-destructive modifier contract.

Implemented modifiers:

1. `TransformModifier` — translation, XYZ rotation, and non-uniform scale.
2. `MirrorModifier` v0.1 — X/Y/Z mirror axis plus plane offset, generated topology duplication, reversed winding, rebuilt mirrored radial rings, and reflected normal data.

Mirror operates on the current evaluated input and never writes generated topology into the authored `EditableMesh`.

Mirrored generated elements preserve the stable source IDs they derive from. This deliberately allows multiple generated elements to map back to one authored identity.

**Mirror v0.1 does not weld vertices on the mirror plane.** Even an exactly-on-plane vertex is duplicated. Welding is deferred until merge tolerance, collapsed topology, corner attributes, source mappings, and non-manifold outcomes have explicit tested rules.

Every evaluated snapshot carries an `EvaluationCacheKey`:

```text
MeshId + authored Mesh revision + ordered modifier-stack revision
```

Same inputs produce the same key; authored edits, modifier configuration changes, modifier axis/offset changes, or modifier reordering change it. Hash support exists for future caches, but **no retained evaluation cache is introduced yet**.

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

Mirror PR #6 passed all eight gates before merge, including the 32-bit ARMv7 compile and the dedicated generated-topology test.

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
- hidden Mirror weld epsilon.

## Next engineering target

1. Define and implement **Mirror weld/merge v0.2** with explicit tolerance and deterministic collapse rules.
2. Preserve source mappings and corner-domain attributes through welded mirror seams.
3. Add seam/non-manifold fixtures before enabling welding by default anywhere.
4. Implement **Triangulate** downstream while preserving authored n-gons.
5. Add an explicitly budgeted evaluation cache only after topology-generating modifier invalidation behavior is proven.
6. Keep storage optimization separate and evidence-driven from benchmark results.

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

The headless engine can now execute the **Add Mirror modifier** step non-destructively. The next architectural decision is deterministic mirror welding, followed by triangulation/render-oriented derived geometry.
