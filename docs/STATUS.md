# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

The portable C++ engine foundation and the first editor-aware Android viewport are in place. Phase 5 is complete: the Android Vulkan viewport now runs from a persistent native `Document + EditorHistory + EditorContext` session, renders multiple evaluated objects, ray-picks the nearest object, maps hits back to stable engine identity, and shows active-object selection/gizmo feedback.

The next major phase is engine-owned object transforms and interactive gizmo manipulation.

## Foundation state

The portable C++20 foundation remains the authored source of truth:

- `Document` owns Scene / Collection / Object / Mesh datablocks.
- Mesh topology uses stable typed 64-bit IDs independent from packed storage.
- `EditableMesh` owns authored polygon topology and attributes.
- Commands, transactions, `EditorHistory`, and mesh history provide bounded undo/redo.
- `MeshEvaluator` produces validated rebuildable evaluated geometry.
- `RenderExtractor` converts evaluated geometry into renderer-facing triangle snapshots while preserving source IDs.
- `EditorContext` owns active object, mode, selection domain, and stable topology selection outside persistent Document state.
- Project serialization, validation, dependency/procedural graph infrastructure, registries, and benchmark coverage remain available above the hardened core.

The renderer does not own editable topology and Android framework types do not enter the portable engine.

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

Complete and merged.

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

Phase 5A and 5B were both verified on the 32-bit Android target. ARMv7 and ARM64 CI remained green.

## Pre-Phase-6 audit

The post-Phase-5 audit found no foundation rewrite requirement. The selection/editor ownership boundary is correct and ready to support object transforms.

Small Stage 5B hardening gaps were found and patched before Phase 6:

- viewport snapshots now verify the persistent object really references the mesh being extracted;
- extracted source document/mesh identity is checked before entering renderer state;
- editor selection is rolled back if the renderer rejects a corresponding active-object overlay change;
- multi-object render batches reject mixed-document snapshots, missing mesh identity, non-finite origins, and 32-bit index-capacity overflow.

See `docs/PRE_PHASE6_AUDIT.md` for the detailed transform, undo, project-format, and renderer entry contracts.

## Phase 6 entry contract

Phase 6 begins in the portable engine, not in Vulkan:

```text
ObjectBlock authored local transform
    translation / rotation / scale
        |
        v
Document mutation API + validation
        |
        v
Document command delta
        |
        v
Unified EditorHistory undo / redo
        |
        v
Project codec persistence
        |
        v
Derived world/model transform
        |
        v
Renderer draw item / picking / gizmo
```

Because objects already support parenting, object transform state must have explicit local-to-parent semantics and world transforms must compose through the parent chain.

The current project schema does not store transforms. Phase 6 must treat adding them as an explicit schema change and add compatibility coverage so existing v1 files load with identity transforms rather than being silently misread.

The current Stage 5B renderer batches static geometry into one draw. Phase 6 should preserve mesh-local evaluated geometry and introduce per-object draw ranges/model transforms rather than rebaking unrelated vertices whenever one object moves.

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

Both Android ABIs remain first-class build targets.

## Known non-blocking renderer debt

These remain tracked but do not block the first Phase 6 engine slice:

- camera/selection changes currently rebuild recorded swapchain command buffers rather than using per-frame camera/object buffers,
- static mesh/grid data currently uses host-visible coherent Vulkan memory rather than staging uploads into device-local memory,
- Android renderer source is warnings-as-errors compiled but is not yet included in the root host clang-tidy source list,
- some shader/build helper names still carry early-stage naming even though the renderer has advanced well beyond Stage 1.

The transform renderer refactor is the right point to improve the dynamic scene path without moving authored ownership into Vulkan.

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
