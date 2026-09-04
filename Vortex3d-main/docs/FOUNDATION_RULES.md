# Vortex3D Foundation Rules

These rules exist to keep the new engine from drifting back into the architectural problems the clean rewrite is meant to remove.

## Ownership

1. Persistent document data belongs to the portable C++ core.
2. Editor/session state is separate from persistent document data.
3. Evaluated geometry is derived and disposable.
4. GPU resources are derived and disposable.
5. Android owns platform integration, not modeling logic.

## Mutation

1. UI code does not mutate document internals.
2. Every persistent mutation is performed through a command/transaction.
3. Every user-visible mutation must define its undo behavior before it is considered complete.
4. Failed commands must leave the document unchanged.
5. Topology mutation functions must preserve documented invariants or fail atomically.

## Identity

1. Persistent IDs are stable and strongly typed.
2. IDs are never array indices.
3. Container compaction/reordering cannot change identity.
4. Deleted topology IDs are not silently repurposed inside the same document lifetime.

## Geometry

1. Editable topology may contain n-gons.
2. Triangulation is derived for evaluation/render/export as required.
3. Face-varying attributes belong on corners/loops.
4. Mesh attributes use generic domains instead of hard-coded UV/color slots.
5. Kernel invariants are validated in debug/test builds.

## Evaluation

1. Source authoring geometry is never overwritten by a modifier evaluation.
2. Evaluation is deterministic for identical inputs.
3. Dirty propagation is explicit.
4. Caches are optimization only; correctness cannot depend on stale cache state.
5. Parallelism comes after correctness and dependency tracking.

## Rendering

1. Renderer code consumes evaluated data.
2. Vulkan handles never appear inside persistent project structures.
3. Renderer caches are keyed by stable engine IDs and revisions.
4. Picking returns engine-visible IDs/handles, not pointers into renderer-owned arrays.

## Files and serialization

1. Native Vortex projects are versioned from the first schema.
2. Every schema change requiring migration adds a migration test fixture.
3. Saves are atomic where the host platform permits it.
4. Autosave/recovery is separate from normal user project files.
5. GLB/glTF is interchange, not the authoritative Vortex project representation.

## Portability

`vortex_core` may depend on the C++ standard library and explicitly approved portable libraries, but not on:

- Android SDK/NDK platform headers in public/core code,
- Vulkan,
- WebView,
- DOM/Web APIs,
- Three.js,
- WASM/Emscripten-specific interfaces,
- OPFS/IndexedDB.

Bindings/adapters live outside the core.

## 32-bit discipline

`armeabi-v7a` is a first-class supported target.

Therefore:

- avoid assumptions that pointers or `size_t` are 64-bit,
- use fixed-width integer types for persisted/binary formats,
- avoid storing giant duplicated buffers,
- measure memory usage on representative 32-bit hardware,
- design undo and evaluation caches with bounded memory behavior,
- capability-query Vulkan rather than assuming high-end extensions/features.

## Testing

Every subsystem earns its next layer through tests.

Minimum expectations:

- ID/handle tests,
- hierarchy cycle tests,
- topology invariant tests,
- command rollback tests,
- undo/redo tests,
- deterministic evaluator tests,
- serialization round-trip/migration tests,
- 32-bit ABI compile tests,
- Android lifecycle/surface recovery tests,
- GLB round-trip validation fixtures.

## Dependency policy

Prefer small, well-understood dependencies. A dependency must justify:

- why we need it,
- license compatibility,
- ARMv7 availability,
- Android availability if relevant,
- binary size/memory impact,
- long-term maintenance risk.

Do not copy GPL Blender source into this MIT repository. Blender is an architecture/reference source; implementations here must be independently authored unless licensing strategy is explicitly changed.
