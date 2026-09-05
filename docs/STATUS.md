# Vortex3D Native Status

Last updated: 2026-09-05

## Current engineering focus

> **Experimental branch:** `work/gizmo-visual-polish` is currently carrying Gizmo System v2. It migrates authored object rotation to quaternions/project schema v3 and moves manipulation math into portable geometric constraints. It is intentionally ahead of the merged/device-verified Phase-6 baseline described below and still requires Android ABI CI plus a new ARMv7 device pass before merge. See `docs/GIZMO_SYSTEM_V2.md`.

The portable C++ engine foundation and editor-aware Android Vulkan viewport are in place. Phase 6 is complete, merged, CI-green, and verified on the 32-bit Samsung target: persistent object translation/rotation/scale is authored in `Document`, persisted by project schema v2, replayed through `EditorHistory`, consumed as engine-derived world matrices by rendering/picking/selection, and manipulated through touch Move / Rotate / Scale gizmo tools.

The Phase 6 device pass confirmed Move / Rotate / Scale, Undo / Redo, existing camera gestures, selection, and transform synchronization are working on the target phone. Phase 6 is therefore both implementation-complete and device-verified.

## Foundation state

The portable C++20 foundation remains the authored source of truth:

- `Document` owns Scene / Collection / Object / Mesh datablocks.
- `ObjectBlock` owns finite local translation/rotation/scale with explicit local-to-parent semantics.
- Object world transforms compose through the parent chain.
- Mesh topology uses stable typed 64-bit IDs independent from packed storage.
- `EditableMesh` owns authored polygon topology and attributes.
- Commands, transactions, `EditorHistory`, and mesh history provide bounded undo/redo.
- `SetObjectTransformCommand` is the durable commit boundary for object manipulation.
- `MeshEvaluator` produces validated rebuildable evaluated geometry.
- `RenderExtractor` converts evaluated geometry into renderer-facing local-space triangle snapshots while preserving source IDs.
- `EditorContext` owns active object, mode, selection domain, and stable topology selection outside persistent Document state.
- Project schema v2 stores object transforms and explicitly migrates schema-v1 objects to identity transforms.
- Project serialization, validation, dependency/procedural graph infrastructure, registries, and benchmark coverage remain available above the hardened core.

The renderer does not own editable topology or persistent object transforms, and Android framework types do not enter the portable engine.

## Android / Vulkan viewport progress

### Stage 1 — geometry + depth

Complete.

- Vulkan graphics pipeline.
- Vertex/index buffers.
- Depth image/view/memory.
- GLSL -> SPIR-V build/embed path.
- Indexed geometry rendering.

### Stage 2 — real engine mesh path

Complete.

```text
EditableMesh
-> Document / MeshBlock
-> MeshEvaluator
-> RenderExtractor
-> Vulkan vertex/index buffers
```

### Stage 3 — camera + grid

Complete.

- Native camera matrix math.
- Perspective projection.
- XZ grid.
- XYZ axes.
- Depth-tested mesh plus overlay grid pipeline.

### Stage 4 — touch camera

Complete and merged.

- one-finger orbit,
- coherent two-finger pan,
- symmetric filtered pinch zoom,
- Android touch-slop based arbitration,
- native-owned camera state,
- safe command-buffer refresh only after the previous frame fence has signaled.

Stage 4 is verified on the 32-bit ARM Samsung target and remains compatible with ARM64 builds.

### Phase 5 — selection foundation

Complete, merged, and verified on the 32-bit Android target.

- persistent native viewport/editor session,
- tap-candidate vs orbit arbitration,
- multiple persistent visible engine objects,
- evaluated/extracted render snapshots per object,
- nearest-hit CPU ray/triangle picking,
- stable `ObjectId + source FaceId` resolution,
- engine-side active object through `EditorContext`,
- tap-empty deselection,
- active-object outline and XYZ gizmo/origin foundation,
- renderer-local synthetic IDs kept out of editor state,
- active selection-buffer writes deferred until the previous frame fence signals.

### Phase 6A — engine-owned transforms

Complete and merged.

- persistent `ObjectTransform { translation, rotationRadians, scale }`,
- finite-value validation and no-op revision behavior,
- local matrix order `T * Rz * Ry * Rx * S`,
- parent-composed world matrices,
- `SetObjectTransformCommand`,
- chronological unified transform undo/redo,
- project schema v2 transform persistence,
- explicit schema-v1 identity migration and corruption coverage.

### Phase 6B — transform-aware renderer

Complete and merged.

- meshes remain object-local instead of baking placement into topology,
- renderer receives stable `ObjectId + local ViewportMesh + engine-derived world matrix`,
- per-object Vulkan draw ranges over shared geometry buffers,
- object world matrices applied during command recording,
- CPU picking transformed from retained local triangles,
- selected outline and XYZ gizmo use the same world matrix as the object,
- stable object/face identity preserved across renderer-local batching.

### Phase 6C — interactive transform gizmos

Complete, merged, CI-green, and verified on the 32-bit Android target.

- projected X/Y/Z touch hit-testing,
- Move, Rotate, and Scale modes,
- axis lock before one-finger orbit begins,
- transient renderer-only preview during `ACTION_MOVE`,
- no authored Document mutation per move event,
- exactly one `SetObjectTransformCommand` on successful drag completion,
- cancellation with no history entry on multitouch, lifecycle interruption, tool change, or `ACTION_CANCEL`,
- native Undo / Redo controls,
- rendering, CPU picking, outline, and gizmo remain synchronized during preview,
- existing orbit/pan/pinch/tap selection routing retained for non-gizmo gestures,
- UI-thread JNI ownership retained.

See `docs/PHASE6_TRANSFORMS.md` for the complete contract and device-test checklist.

## CI gate

Core CI covers:

- repository policy,
- portable-core boundary checks,
- GCC host build/tests,
- Clang host build/tests,
- ASan + UBSan,
- clang-tidy,
- Android ARMv7 32-bit native build,
- Android ARM64 native build,
- split/universal debug APK build and ABI packaging verification,
- Release benchmark smoke.

Both Android ABIs remain first-class build targets. Phase 6 was merged only after its exact-head CI run was fully green.

## Known non-blocking renderer debt

These remain tracked beyond Phase 6 and do not change authored ownership:

- camera/selection/object-preview changes currently rebuild recorded swapchain command buffers rather than using per-frame camera/object buffers,
- static mesh/grid data currently uses host-visible coherent Vulkan memory rather than staging uploads into device-local memory,
- Android renderer source is warnings-as-errors compiled but is not yet included in the root host clang-tidy source list,
- some shader/build helper names still carry early-stage naming even though the renderer has advanced well beyond Stage 1.

These are renderer-performance/maintainability improvements, not reasons to move persistent transform state into Vulkan.

## First meaningful product milestone

```text
Launch APK
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
