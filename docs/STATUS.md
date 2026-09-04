# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

**Production-hardening the portable foundation, then connecting authored geometry to an evaluated-mesh layer.**

The project is no longer in the original bootstrap-only state. The Document, mesh kernel, command/history path, Android compile gate, static analysis, performance instrumentation, and deliberate validation-corruption coverage are all live.

## Foundation state

### Language and portability

- [x] C++20 required.
- [x] C++ extensions disabled.
- [x] GCC + Clang warnings-as-errors builds.
- [x] AddressSanitizer + UndefinedBehaviorSanitizer CI.
- [x] clang-tidy executes in CI against a real compile database.
- [x] Portable-core dependency scanner.
- [x] Android NDK cross-compile gate for `armeabi-v7a`.
- [x] Android NDK cross-compile gate for `arm64-v8a`.
- [x] Explicit CMake pointer-width validation proves ARMv7 is compiled as 32-bit and ARM64 as 64-bit.
- [x] Portable core remains free of Android/JNI/Vulkan/WebView/Emscripten/Three.js/platform-header dependencies.

### Document and ownership

- [x] Scene / Collection / Object / Mesh data blocks.
- [x] Stable typed 64-bit IDs.
- [x] Shared mesh instances through `MeshId` references.
- [x] explicit Make Unique behavior.
- [x] Parent-cycle rejection and reference validation.
- [x] Document revision/change events.
- [x] `Document` is move-only rather than casually copied.
- [x] `MeshBlock` uniquely owns authored `EditableMesh` data.
- [x] ordinary Document undo uses reversible deltas rather than retained whole-Document snapshots.
- [x] global Document history has an explicit retained-byte budget.

Objects do not own mesh geometry. Sharing means multiple Objects reference the same Mesh data block. Make Unique is the deliberate geometry clone boundary.

### Mesh kernel

- [x] Vertex / Edge / Face / Corner domains with stable IDs independent from packed indices.
- [x] Face `next/prev` cycles and radial edge cycles.
- [x] N-gons.
- [x] Manifold and selected non-manifold topology.
- [x] Generic domain-qualified attributes.
- [x] conservative add/remove topology primitives.
- [x] stable-ID edge split.
- [x] face extrusion.
- [x] deterministic attribute copying/interpolation/compaction.
- [x] structured validation diagnostics.
- [x] deterministic randomized mutation testing.
- [x] donor behavior contracts rewritten as native tests rather than architecture copies.

### Commands and history

- [x] Document command/transaction boundary.
- [x] compact rename / parent / mesh-reference style Document deltas.
- [x] bounded `MeshHistory`.
- [x] vertex move before/after history.
- [x] exact-ID local topology history for face extrusion.
- [x] undo/redo branch handling and budget enforcement.
- [x] Mesh history bound to its owning Document instance + MeshId when used through the Document bridge.
- [x] repeated and stacked extrusion undo/redo coverage.

Whole meshes are **not retained as normal undo steps**. Some complex topology operations still use full `EditableMesh` copies as temporary operation-local rollback guards. Those are correctness debt to profile, not retained history architecture.

### Validation hardening

CTest currently registers **11 native suites**.

Coverage includes:

- valid triangle/quad/n-gon/cube topology,
- shared and 3-face non-manifold radial edges,
- removal and attribute compaction,
- edge splitting,
- face extrusion,
- command/history replay,
- stacked topology undo/redo,
- deterministic randomized mutation,
- deliberate broken face cycles,
- deliberate broken radial cycles,
- invalid edge endpoints,
- invalid face sizes,
- orphan/unreachable corners,
- attribute-size mismatch,
- duplicate/reversed edge prevention,
- illegal deletion order.

Deliberate private corruption access exists only in test builds through `VORTEX_ENABLE_TEST_HOOKS`.

### Performance and memory measurement

- [x] optional `vortex_mesh_bench` Release benchmark target.
- [x] benchmark vertex creation.
- [x] benchmark edge creation.
- [x] benchmark face creation.
- [x] benchmark topology traversal.
- [x] benchmark edge split.
- [x] benchmark face extrusion.
- [x] benchmark attribute lookup/mutation.
- [x] benchmark vertex movement.
- [x] benchmark undo/redo.
- [x] benchmark full validation.
- [x] JSON output and GitHub Actions artifacts.
- [x] requested 10k / 100k / 1M manual profiles where practical.
- [x] normal CI benchmark smoke.

Initial 64-bit CI layout observations from the smoke profile:

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

These values exclude allocator/node bookkeeping. They confirm that corner/hash-heavy storage deserves investigation, but **do not justify a container rewrite by themselves**.

The current storage remains intentionally redundant:

```text
stable ID
  -> ID order
  -> ID-to-packed-index lookup
  -> ID-to-topology-record lookup
```

Future packed/sparse/generational storage must preserve the stable public ID contract.

## Current CI gate

Normal Core CI contains **8 jobs**:

1. portable dependency boundary,
2. GCC host build/tests,
3. Clang host build/tests,
4. ASan + UBSan tests,
5. clang-tidy static analysis,
6. Android ARMv7 32-bit cross-compile,
7. Android ARM64 cross-compile,
8. Release benchmark smoke + artifact.

All eight jobs are green on the validation-hardening foundation head.

## Deferred intentionally

The following are not being introduced prematurely:

- custom persistent allocators,
- broad `std::pmr` conversion,
- ECS replacement of the authoring model,
- plugin ABI,
- storage rewrite without benchmark evidence,
- exception-heavy result architecture,
- Android framework types inside the modeling core,
- renderer ownership of authored mesh data.

`std::pmr`, arenas, scratch buffers, and operation-local reusable storage remain candidates specifically for temporary topology work after profiling.

## Next engineering target

1. Capture larger benchmark baselines at requested 10k / 100k / 1M profiles and compare scaling.
2. Use those results to choose the first narrow storage/performance experiment; `findEdge()` linear lookup and corner/hash memory are current suspects, not assumptions.
3. Keep temporary whole-mesh rollback until measurements justify replacing it with scratch/local patches.
4. Begin the authored `EditableMesh` -> read-only `EvaluatedMesh` boundary with source-ID mappings and revision tracking.
5. Add modifiers only after that source/evaluated separation is deterministic.

No renderer or Android UI code should bypass this sequence.

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

The headless kernel already performs and exactly replays the Extrude/Undo/Redo portion. The next architectural gap is evaluated geometry, followed by project serialization and the Android host/render path.
