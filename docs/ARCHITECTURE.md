# Vortex3D Native Architecture

## 1. Design objective

Vortex3D is an authoring system first. The persistent model must represent editable documents, shared data-blocks, stable topology, history, dependencies, and project state. Rendering and platform integration are consumers of that model, not owners of it.

The architecture therefore separates four different things that must never collapse into one object:

1. **Persistent authoring data** — what the user actually created.
2. **Editor/session state** — selection, active tool, hover state, gizmos, temporary previews.
3. **Evaluated data** — results of modifiers/procedural evaluation.
4. **Render data** — GPU-friendly triangulated/cached buffers.

## 2. Layer model

```text
+--------------------------------------------------+
| Platform Host                                    |
| Android lifecycle, files, IME, clipboard, input |
+--------------------------------------------------+
                     |
+--------------------------------------------------+
| Editor Layer                                     |
| Tools, selection, snapping, gizmos, workspaces  |
+--------------------------------------------------+
                     |
+--------------------------------------------------+
| Command / Transaction Layer                      |
| Validation, mutation, undo records, notifications|
+--------------------------------------------------+
                     |
+--------------------------------------------------+
| Document Layer                                   |
| Objects, meshes, materials, images, collections |
+--------------------------------------------------+
                     |
+--------------------------------------------------+
| Geometry Kernel                                  |
| Vertices, edges, faces, corners, attributes     |
+--------------------------------------------------+
                     |
+--------------------------------------------------+
| Evaluation Layer                                 |
| Modifiers, dependency graph, caches             |
+--------------------------------------------------+
                     |
+--------------------------------------------------+
| Renderer Adapter                                 |
| EvaluatedMesh -> GPU resources                  |
+--------------------------------------------------+
                     |
+--------------------------------------------------+
| Vulkan Renderer                                  |
+--------------------------------------------------+
```

## 3. Document model

The document owns durable data-blocks. Initial types:

- `Document`
- `Scene`
- `Object`
- `Mesh`
- `Material`
- `Image`
- `Collection`

Objects reference mesh data by stable ID. Multiple objects may reference the same mesh. A Make Unique operation duplicates the data-block and rewires one object.

The authoring core is deliberately **not an ECS**. ECS-style packed runtime structures may be used in a renderer cache later, but they do not define the saved project.

## 4. Stable identity

Every persistent data-block and topology element receives a stable 64-bit identity.

Requirements:

- IDs are not array indices.
- Repacking containers does not change identity.
- Topology-preserving operations retain valid identities.
- New topology receives new identities.
- Deleted identities are not accidentally reused during the document lifetime.
- Serialization preserves persistent identity where required by history/references.

Prefer strongly typed wrappers such as `ObjectId`, `MeshId`, `VertexId`, `EdgeId`, `FaceId`, and `CornerId` over naked integers.

## 5. Mesh kernel

The editable mesh uses four logical domains:

- **Vertex** — position-bearing point.
- **Edge** — connection between two vertices.
- **Face** — polygon boundary.
- **Corner / Loop** — one face-local use of a vertex/edge.

Corner/loop records allow face-varying data such as UVs and split normals. Radial connectivity allows traversal around an edge and supports non-manifold topology.

The kernel should expose a small trusted set of low-level topology operations. Higher-level tools such as Extrude, Inset, Bevel, Dissolve, Bridge, and Loop Cut are composed from these primitives.

### Generic attributes

Do not permanently hard-code `uv0`, `uv1`, color sets, creases, weights, masks, or future data into the mesh structure.

Use generic typed attributes:

```text
AttributeDomain = Vertex | Edge | Face | Corner
AttributeType   = Bool | Int | Float | Vec2 | Vec3 | Vec4 | ...
```

Examples:

- `position` -> Vertex / Vec3
- `uv:Map` -> Corner / Vec2
- `normal` -> Corner / Vec3 when split normals are authored
- `crease` -> Edge / Float
- `material_index` -> Face / Int
- `selection_set:*` -> optional editor-owned data, not necessarily persistent

## 6. Commands and transactions

No UI code directly mutates document internals.

All edits flow through commands:

```text
UI/Input -> Command -> Transaction -> Document/Kernel
```

Examples:

- `MoveVerticesCommand`
- `ExtrudeFacesCommand`
- `SetObjectTransformCommand`
- `AddModifierCommand`
- `AssignMaterialCommand`

A command:

1. validates its inputs,
2. opens or joins a transaction,
3. calls domain services/kernel operations,
4. records undo information,
5. commits or rolls back atomically,
6. emits invalidation/change notifications.

This single API is intended to serve touch UI, keyboard shortcuts, automated tests, plugins, macros, scripting, and future AI tooling.

## 7. Undo / redo

Use a hybrid strategy rather than cloning the whole scene after every action.

- Small property edits: compact inverse/delta records.
- Topology edits: transactional topology deltas where safe.
- Expensive or structurally complex operations: copy-on-write or scoped snapshots.
- User-visible actions may contain many low-level mutations but commit as one undo step.

Undo must restore authoring state and invalidate evaluated/render caches rather than serializing GPU state.

## 8. Evaluation graph

The original mesh is immutable from the evaluator's point of view.

```text
Editable Mesh
   -> Modifier A
   -> Modifier B
   -> Modifier C
   -> Evaluated Mesh
```

Initial rules:

- Deterministic evaluation.
- Explicit dependencies.
- Revision numbers / dirty propagation.
- Cache evaluated results when inputs have not changed.
- Start single-threaded.
- Add parallel scheduling only after dependencies and invalidation are proven correct.

Initial modifiers should be deliberately small:

1. Transform
2. Mirror
3. Triangulate
4. Recalculate Normals
5. Bevel
6. Subdivision

## 9. Rendering boundary

The renderer never owns editable topology.

It consumes an `EvaluatedMesh` and produces GPU caches such as:

- vertex/index buffers,
- material bindings,
- acceleration/picking resources,
- overlay buffers,
- revision stamps.

Triangulation belongs in evaluation/render preparation, not as the canonical saved authoring topology.

## 10. Platform boundary

The engine library must compile without Android headers.

Android owns:

- activity/app lifecycle,
- surface creation,
- file/document picker,
- scoped storage integration,
- clipboard,
- keyboard/IME,
- share intents,
- haptics,
- device capability queries.

C++ owns:

- document,
- mesh kernel,
- commands,
- undo/redo,
- evaluation,
- project serialization,
- import/export logic where platform-independent.

## 11. Native project format

The Vortex project format is an authoring format, not glTF.

It must preserve:

- stable IDs,
- object hierarchy,
- linked data-blocks,
- editable n-gons,
- generic attributes,
- materials and assets,
- modifier stacks / future node graphs,
- application version and schema version,
- migration metadata.

GLB/glTF is an interchange/export target.

## 12. Performance principles

Especially because Vortex supports 32-bit ARM:

- Avoid unbounded scene snapshots.
- Do not duplicate source, evaluated, and GPU buffers unnecessarily.
- Keep evaluated/render data rebuildable and discardable.
- Stream textures/assets when practical.
- Use compact handles and contiguous storage where it improves locality without sacrificing stable identity.
- Track memory budgets and cache ownership explicitly.
- Build stress tests early.

## 13. Non-negotiable architecture rules

1. No renderer types in persistent document classes.
2. No Android types in the portable engine.
3. No UI mutation of engine internals.
4. IDs are never array positions.
5. Authoring topology is not forced to triangles.
6. Evaluated geometry is never silently written back into source geometry.
7. Undo/redo is part of the mutation design, not bolted on later.
8. Serialization has explicit versions and migrations from the first public schema.
9. Every topology-changing kernel primitive has invariant tests.
10. 32-bit ARM remains a supported build target from the beginning.
