# Vortex3D Ownership and Lifetime Model

## Purpose

Ownership is explicit. Stable IDs express durable relationships; pointers express only implementation lifetime and never persistent identity.

## Authoring ownership

- `Document` uniquely owns its data-block registries.
- `MeshBlock` uniquely owns exactly one authored `EditableMesh` through `std::unique_ptr`.
- `ObjectBlock` does **not** own a mesh. It references a `MeshBlock` by stable `MeshId`.
- Multiple objects instance/share authored geometry by referencing the same `MeshId`.
- `Make Unique` deliberately deep-copies one authored mesh and rewires one object to the new `MeshId`.
- `Document` is movable but not copyable. Copying an entire scene graph is not an ordinary editing primitive.

`std::shared_ptr` is not the default ownership mechanism. It should be introduced only when multiple independent owners truly share lifetime, not to make copying convenient.

## Non-owning access

Query functions may return `const T*` or views into Document/Mesh-owned state. These are non-owning and must not be persisted across mutations that can erase or move the referenced data.

Stable IDs are the durable reference mechanism across editor systems, history, serialization, evaluation, renderer caches, scripting, and future AI tooling.

## History ownership

- `DocumentHistory` owns compact reversible Document delta records and has an explicit byte budget.
- `MeshHistory` owns compact mesh value/topology delta records and has an explicit byte budget.
- `DocumentHistory` binds to one live Document instance.
- When used through `Document`, `MeshHistory` binds to one live Document instance **and** one stable `MeshId`; cross-document and cross-mesh replay are rejected.
- History never owns renderer/GPU state.
- `Make Unique` undo moves the newly created `MeshBlock` out of the Document and into the redo record; it does not keep a second full copy.

## Transactions

`Transaction` is an atomic composition boundary, not an undo snapshot. It keeps only the compact deltas produced by commands executed inside it. Failure or destruction before commit reverses those deltas and restores the starting Document revision/change log.

The monotonic ID allocator is not rewound during rollback. IDs allocated by a failed/rolled-back operation remain retired rather than being silently reused.

## Mesh topology

`EditableMesh` owns topology and attributes by value. Stable `VertexId`, `EdgeId`, `FaceId`, and `CornerId` values are independent from packed indices and hash-table layout.

The current maps/vectors are correctness-first storage. Future packed/sparse/generational storage may replace internals only if the public stable-ID contract is preserved.

## Evaluated and renderer data

Future evaluated meshes and renderer resources are derived/disposable caches:

- the evaluator reads authored data but does not own or mutate it,
- renderer caches are keyed by stable engine IDs + revisions,
- Vulkan handles and platform objects never enter persistent authoring structures,
- cache lifetime may use unique ownership, pools, or explicit cache managers; `shared_ptr` is not assumed.

## Threading assumption

Authoring mutation is single-threaded today. No current container or history type promises concurrent mutation safety. Future background evaluation/render work must consume immutable snapshots/revisioned derived data or synchronize explicitly rather than racing the authoring model.

## Allocation direction

The foundation currently uses standard-library allocators. APIs should remain compatible with future operation-local scratch arenas or `std::pmr` where profiling justifies them. Custom allocators are not introduced without benchmark evidence.
