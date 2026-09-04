# Vortex3D v0.2 Architecture Stack

## Purpose

v0.2 is the first full application architecture built above the hardened v0.1 core. The rule is simple: authored truth stays in the portable core; interaction, procedural composition, viewport extraction, persistence adapters, and platform hosting build around it without taking ownership away from `Document` / `EditableMesh`.

## Layering

```text
Android / future desktop host
        |
EditorContext + Tool Sessions
        |
Operators + Extension Registries
        |
Unified EditorHistory
        |
Document / EditableMesh authored truth
        |
GeometryOperations
        |
DependencyGraph + GeometryGraph
        |
MeshEvaluator + Modifier Stack
        |
Viewport RenderExtractor
        |
GPU backend (future Vulkan layer)
```

The native `.vortex` ProjectCodec is orthogonal to evaluation: it serializes authored state and stable IDs, never evaluated/cache/runtime-document identity.

## Geometry operations foundation

`GeometryOperations` is the reusable algorithm-facing API. It currently provides topology views, incident-edge/face/neighbor queries, centroids, vertex translation, edge split, face extrusion, and face inset. Operations validate their output and use operation-local rollback where needed.

The important architectural rule is that geometry algorithms do not know whether they were called by a UI, modifier, node graph, script, import adapter, or test.

## Editor model

`EditorContext` owns ephemeral interaction state only:

- Object/Edit mode,
- active object and resolved active mesh,
- vertex/edge/face selection domain,
- selected stable topology IDs,
- access to the unified `EditorHistory`.

Selection and tool state are not stored inside the `Document` authoring model.

## Operators and tools

Operators are reusable editor actions. v0.2 includes face extrusion and selected-vertex translation operators. They commit through `EditorHistory`, so UI and tool sessions do not bypass undo/revision/change-journal semantics.

`TranslateToolSession` demonstrates preview/update/commit/cancel separation. Preview state remains transient; commit creates the authored edit.

## Dependency graph

`DependencyGraph` is intentionally separate from the scene hierarchy and from user-facing geometry nodes. It provides:

- explicit dependency/dependent edges,
- cycle rejection,
- dirty propagation to dependents,
- deterministic topological evaluation order,
- dirty-only scheduling order.

v0.2 is single-threaded. The graph API is structured so a future scheduler can execute independent nodes concurrently without changing authoring ownership.

## Procedural geometry graph

`GeometryGraph` is the first user-facing procedural layer. Nodes currently wrap the same modifier implementations used by direct evaluation:

- Transform,
- Mirror,
- Triangulate,
- Simple Deform Twist.

The graph builds an ordered modifier stack and asks `MeshEvaluator` for the result. It does not create a second geometry engine.

## Modifier library

v0.2 adds `SimpleDeformTwistModifier` to the existing Transform / Mirror / Triangulate stack. It is evaluated-only, deterministic, revision-tokened, and source-immutable like the existing modifiers.

## Viewport extraction

`RenderExtractor` consumes only validated `EvaluatedMesh` data. It triangulates a private evaluated copy when needed and emits compact viewport vertices + triangle indices with source vertex/face IDs for picking.

The viewport representation is not editable truth and never writes back to the authoring mesh.

## Project format v0.1

`ProjectCodec` implements a versioned binary authored-project payload with:

- fixed 8-byte magic,
- explicit schema version,
- payload size,
- FNV-1a integrity checksum,
- stable Document / Scene / Collection / Object / Mesh IDs,
- exact stable Vertex / Edge / Face / Corner IDs and allocator state,
- scene hierarchy and collection membership,
- shared mesh references,
- mesh revision/evaluation revision,
- all generic attribute layers and values.

RuntimeDocumentId, undo history, evaluated meshes, and caches are intentionally not serialized.

Decoding is bounded and validates reconstructed `EditableMesh` storage through `validateStrict()` and the reconstructed `Document` before exposing it.

## Extensibility

v0.2 introduces:

- typed `PropertySchema` / `PropertyBag`,
- `OperatorRegistry`,
- `ModifierRegistry`,
- `GeometryNodeRegistry`,
- `ProjectAdapterRegistry`.

The built-in `.vortex` adapter is registered through the same adapter boundary intended for future import/export formats. A scripting runtime remains deferred; the registries/properties are the ABI-neutral surface it can bind to later.

## Android shell v0.1

`android/` now contains an actual APK host instead of a placeholder. It:

- supports `armeabi-v7a` and `arm64-v8a`,
- links the complete `Vortex3D::engine` target,
- keeps JNI/Android APIs outside portable engine code,
- exposes a minimal JNI engine-version handshake,
- provides the host location for lifecycle, input, storage, and future Vulkan surface ownership.

The GitHub Android matrix configures this shell CMake project for both required ABIs, so the full engine + JNI host is cross-compiled rather than only `vortex_core`.

## Deliberately deferred

v0.2 is architecture-complete, not feature-complete as a modeling product. Still deferred are production Vulkan rendering, touch UI/workspaces, bevel/boolean/subdivision-quality production algorithms, graph branching/field systems, materials/assets, GLB adapters, scripting, autosave/recovery plumbing, and high-performance parallel dependency scheduling.
