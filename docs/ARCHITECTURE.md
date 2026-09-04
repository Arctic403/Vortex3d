# Vortex3D Native Architecture

## 1. Design objective

Vortex3D is an authoring system first. The persistent model must represent editable documents, shared data-blocks, stable topology, history, dependencies, and project state. Rendering and platform integration are consumers of that model, not owners of it.

The architecture separates:

1. **Persistent authoring data** — what the user actually created.
2. **Editor/session state** — selection, active tool, hover state, gizmos, temporary previews.
3. **Evaluated data** — results of modifiers/procedural evaluation.
4. **Render data** — GPU-friendly triangulated/cached buffers.

## 2. Layer model

```text
Platform Host (Android/Desktop)
        |
Editor / Tool Controllers
        |
Commands + Delta Transactions + Undo
        |
Document / Scene Data-blocks
        |
Editable Mesh Kernel
        |
Evaluation Graph / Derived Meshes
        |
Renderer Adapter
        |
Vulkan Renderer
```

## 3. Document and ownership model

The `Document` uniquely owns durable data-block registries. A `MeshBlock` uniquely owns its authored `EditableMesh`. Objects reference meshes by stable `MeshId`; they never share geometry by sharing owning pointers.

Multiple objects instance the same geometry by holding the same `MeshId`. `Make Unique` explicitly creates a new Mesh datablock, deep-copies that authored mesh once, and rewires one object.

The authoring core is deliberately **not an ECS**. Packed runtime structures may be used in derived/render caches later, but they do not define the saved project.

See `docs/OWNERSHIP.md` for lifetime rules.

## 4. Stable identity

Every persistent data-block and topology element receives a stable 64-bit identity.

- IDs are not array indices.
- Repacking containers does not change identity.
- Topology-preserving operations retain valid identities.
- New topology receives new identities.
- Deleted/rolled-back identities are not silently reused during the document lifetime.
- Serialization preserves persistent identity where required by history/references.

Use strongly typed wrappers such as `ObjectId`, `MeshId`, `VertexId`, `EdgeId`, `FaceId`, and `CornerId` rather than naked integers.

## 5. Mesh kernel

The editable mesh uses Vertex, Edge, Face, and Corner/Loop domains. Corners carry face-varying data such as UVs and split normals. Radial connectivity supports traversal around an edge and legal non-manifold topology.

Generic typed attributes remain domain-qualified rather than hard-coded into topology records.

The current vector + hash-map storage is correctness-first. Stable IDs are the public contract; packed indices are rebuildable implementation detail. Storage may become more cache-friendly only after benchmarks and memory measurements justify a change.

## 6. Commands, transactions, and history

No UI code directly mutates document internals.

```text
UI/Input -> Command -> Delta/Transaction -> Document/Kernel
```

Ordinary Document edits use `DocumentHistory` compact deltas. Mesh edits use `MeshHistory` value/topology deltas. Both histories have explicit retained-memory budgets.

`Transaction` is an atomic composition helper. It retains only the deltas produced by commands executed inside it and reverses them if the transaction fails or is not committed. It does not clone the Document.

See `docs/COMMANDS_UNDO.md`.

## 7. Evaluation graph

The original mesh is immutable from the evaluator's point of view.

```text
Editable Mesh
   -> Modifier A
   -> Modifier B
   -> Modifier C
   -> Evaluated Mesh
```

Initial rules:

- deterministic evaluation,
- explicit dependencies,
- revision/dirty propagation,
- cache outputs only while inputs remain unchanged,
- start single-threaded,
- add parallel scheduling after dependencies/invalidation are proven.

## 8. Rendering boundary

The renderer never owns editable topology. It consumes evaluated data and owns only derived GPU caches. Picking returns engine IDs/handles, not pointers into renderer-owned arrays.

## 9. Platform boundary

The engine library compiles without Android headers. Android owns activity/lifecycle, surfaces, file/document picker, scoped storage, clipboard, IME, haptics, and capability queries. C++ owns the portable document, mesh kernel, commands/history, evaluation, serialization, and portable import/export.

## 10. Native project format

The Vortex project format is an authoring format, not glTF. It preserves stable IDs, hierarchy, linked data-blocks, editable n-gons, generic attributes, materials/assets, future modifier/node state, and explicit schema/migration versions.

## 11. Performance and 32-bit principles

Especially because Vortex supports `armeabi-v7a`:

- avoid unbounded scene snapshots,
- do not duplicate source/evaluated/GPU buffers unnecessarily,
- keep evaluated/render data rebuildable and discardable,
- track history/cache memory budgets explicitly,
- use fixed-width persistent identifiers,
- benchmark representative 10k/100k/1M-scale workloads before redesigning storage,
- prefer cache-friendly packed internals only when stable public identity remains independent.

## 12. Threading

Authoring mutation is single-threaded today. Background evaluation/rendering must not race mutable authoring containers. Threading enters only through explicit immutable/revisioned handoff and measured need.

## 13. Non-negotiable rules

1. No renderer types in persistent document classes.
2. No Android types in the portable engine.
3. No UI mutation of engine internals.
4. IDs are never array positions.
5. Authoring topology is not forced to triangles.
6. Evaluated geometry is never silently written back into source geometry.
7. Undo/redo is part of mutation design.
8. Serialization has explicit versions/migrations.
9. Every topology-changing primitive has invariant tests.
10. 32-bit ARM remains a supported build target.
