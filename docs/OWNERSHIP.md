# Vortex3D Ownership and Lifetime Model

## Purpose

Ownership is explicit. Stable IDs express durable relationships; pointers express only implementation lifetime and never persistent identity.

## Authoring ownership

- `Document` uniquely owns its data-block registries.
- `MeshBlock` uniquely owns exactly one authored `EditableMesh` through a private `std::unique_ptr`.
- `ObjectBlock` does **not** own a mesh. It references a `MeshBlock` by stable `MeshId`.
- Multiple objects instance/share authored geometry by referencing the same `MeshId`.
- `Make Unique` deliberately deep-copies one authored mesh and rewires one object to the new `MeshId`.
- `Document` is movable but not copyable. Copying an entire scene graph is not an ordinary editing primitive.

`std::shared_ptr` is not the default ownership mechanism. It should be introduced only when multiple independent owners truly share lifetime, not to make copying convenient.

## Runtime Document lineage

Stable authored IDs are scoped to one Document and are not globally unique across multiple open Documents. Each live Document therefore owns a process-local `RuntimeDocumentId` in addition to its persistent authored IDs.

`RuntimeDocumentId` is deliberately **not serialized**. It exists only to distinguish live Document lineages for caches/history/lifetime checks.

- construction allocates a fresh non-zero runtime identity,
- move construction and move assignment transfer that runtime identity with the authored state,
- the moved-from Document is reset to a fresh valid empty lineage,
- every resident `MeshBlock` carries the runtime identity of its owning Document,
- `Document::validate()` rejects a resident MeshBlock whose runtime owner identity does not match,
- a detached/manual MeshBlock without an owning runtime identity cannot be evaluated/cached.

A Document must not be moved while a `Transaction` is active on that object. `Transaction` is a short-lived synchronous mutation boundary and holds a direct `Document&`; moving the Document during that scope is outside the current contract. Document moves are supported across normal history/cache lifetimes, not through an active transaction scope.

## Non-owning authored access

Query functions may return `const T*` or views into Document/Mesh-owned state. These are non-owning and must not be persisted across mutations that can erase or move the referenced data.

`MeshBlock` does not expose its owning `unique_ptr`. `MeshBlock::authoredMesh()` returns `const EditableMesh*`, so const access propagates to the authored payload. This prevents callers from mutating geometry through a read-only datablock handle without advancing the `MeshBlock` revision.

Authored mesh mutation remains owned by `Document` command/history paths. That revision discipline is required for evaluator/cache correctness.

Stable IDs are the durable reference mechanism across editor systems, history, serialization, evaluation, renderer caches, scripting, and future AI tooling. Runtime Document identity is not a replacement for stable IDs and must never be persisted as project identity.

## History ownership

- `DocumentHistory` owns compact reversible Document delta records and has an explicit byte budget.
- `MeshHistory` owns compact mesh value/topology delta records and has an explicit byte budget.
- `DocumentHistory` binds to one live Document **lineage** by `RuntimeDocumentId`, not by the address of a particular C++ `Document` object.
- When used through `Document`, `MeshHistory` binds to one live Document lineage **and** one stable `MeshId`; cross-document and cross-mesh replay are rejected.
- Because runtime lineage moves with authored state, existing Document/Mesh history continues to target a moved-to Document and rejects the freshly reset moved-from Document.
- History never owns renderer/GPU state.
- `Make Unique` undo moves the newly created `MeshBlock` out of the Document and into the redo record; it does not keep a second full copy.

## Transactions

`Transaction` is an atomic composition boundary, not an undo snapshot. It keeps only the compact deltas produced by commands executed inside it. Failure or destruction before commit reverses those deltas and restores the starting Document revision/change log.

A Transaction directly references its Document for the duration of the scope. Do not move that Document until the Transaction is committed, rolled back, or destroyed. Future concurrency/state-indirection work may replace this precondition if asynchronous transaction ownership ever becomes a real requirement.

The monotonic ID allocator is not rewound during rollback. IDs allocated by a failed/rolled-back operation remain retired rather than being silently reused.

## Mesh topology

`EditableMesh` owns topology and attributes by value. Stable `VertexId`, `EdgeId`, `FaceId`, and `CornerId` values are independent from packed indices and hash-table layout.

The current maps/vectors are correctness-first storage. Future packed/sparse/generational storage may replace internals only if the public stable-ID contract is preserved.

## Evaluated data and cache ownership

`EvaluatedMesh` is derived, rebuildable state. It never owns or mutates authored geometry.

`EvaluationCache` is the one deliberate current use of `std::shared_ptr` for engine geometry. The reason is lifetime, not convenience: renderer/export/read-only consumers may still be using an immutable evaluated snapshot when the cache evicts its own entry.

The ownership rule is:

```text
EvaluationCache
    owns shared_ptr<const EvaluatedMesh>
              |
              +--> read-only consumer may temporarily share lifetime
```

- cache entries are immutable to normal consumers,
- eviction releases only the cache's ownership,
- a snapshot already held by a consumer remains valid,
- the cache's byte budget counts only snapshots retained by the cache,
- external consumers must not pin an unbounded number of snapshots,
- zero cache budget means evaluation still works but no generated result is retained,
- cache identity includes `RuntimeDocumentId + MeshId + source revision + modifier stack revision`,
- explicit `eraseMesh(RuntimeDocumentId, MeshId)` releases entries for exactly one authored mesh in one live Document lineage.

The cache is intentionally single-threaded in v0.1. No locking or concurrent mutation contract is implied yet.

## Renderer resources

Renderer resources remain future derived/disposable state:

- renderer caches consume evaluated geometry rather than authored topology,
- Vulkan handles and platform objects never enter persistent authoring structures,
- renderer/GPU lifetime must remain separate from `Document`, `EditableMesh`, and history,
- a future renderer may hold a read-only evaluated snapshot while creating/updating GPU buffers, but it does not gain ownership of authored data.

## Threading assumption

Authoring mutation is single-threaded today. Runtime Document identity allocation follows that same contract. No current container, history type, evaluator cache, or modifier promises concurrent mutation safety. Future background evaluation/render work must consume immutable snapshots/revisioned derived data or synchronize explicitly rather than racing the authoring model.

## Allocation direction

The foundation currently uses standard-library allocators. APIs should remain compatible with future operation-local scratch arenas or `std::pmr` where profiling justifies them. Custom allocators are not introduced without benchmark evidence.
