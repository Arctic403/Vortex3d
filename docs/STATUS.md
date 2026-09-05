# Vortex3D Native Status

Last updated: 2026-09-04

## Current engineering focus

The hardened portable engine foundation is in place and the current product work is the native Android Vulkan viewport/editor path. The project has moved beyond the original JNI host proof: evaluated engine geometry is now rendered through a Vortex-owned Vulkan backend with depth, grid/axes, camera navigation, and dual-ABI Android packaging.

The next product phase is selection and editor interaction. Before that phase, the repository is being cleaned and hardened so the viewport can connect to persistent editor state without carrying temporary bootstrap shortcuts forward.

## Foundation state

The portable C++20 foundation remains the authored source of truth:

- `Document` owns Scene / Collection / Object / Mesh datablocks.
- Mesh topology uses stable typed 64-bit IDs independent from packed storage.
- `EditableMesh` owns authored polygon topology and attributes.
- Commands, transactions, `EditorHistory`, and mesh history provide bounded undo/redo.
- `MeshEvaluator` produces validated rebuildable evaluated geometry.
- `RenderExtractor` converts evaluated geometry into renderer-facing triangle snapshots while preserving source IDs for future picking.
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

The bootstrap cube proves evaluated engine geometry reaches the Vulkan renderer on Android.

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

## Pre-Phase-5 audit results

The audit found no foundation rewrite requirement. The important cleanup/hardening actions are:

- remove the accidentally committed nested `Vortex3d-main/` source-tree copy,
- add repository-policy protection so that nested export cannot return,
- refresh Android/renderer status documentation,
- preserve the rule that Phase 5 selection must use persistent engine/editor state and stable source IDs rather than renderer-owned selection state.

The current bootstrap cube is still created from a temporary native `Document` during GPU resource creation. That was correct for renderer proof stages, but Phase 5 must replace it with a persistent viewport/editor session before selection becomes authoritative.

Likewise, current one-finger input treats movement as orbit immediately. Phase 5 will add tap-candidate vs drag arbitration so a short tap can become selection while movement beyond touch slop remains orbit.

## CI gate

Core CI currently covers:

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

Both required Android ABIs are first-class build targets. A viewport/editor patch is not considered ready when it only works on ARM64.

## Known non-blocking renderer debt

These are tracked but do not block Phase 5 selection work:

- camera changes currently rebuild recorded swapchain command buffers rather than using a per-frame camera buffer/descriptors,
- static mesh/grid data currently uses host-visible coherent Vulkan memory rather than a staging upload into device-local buffers,
- Android renderer source is warnings-as-errors compiled but is not yet included in the root host clang-tidy source list,
- some shader/build helper names still carry early-stage naming even though the renderer has advanced beyond Stage 1.

These should be improved when the dynamic scene/resource path is introduced rather than by complicating the current tiny bootstrap renderer prematurely.

## Phase 5 entry contract

Phase 5 should begin with this ownership path:

```text
Persistent native viewport/editor session
    Document
    EditorHistory
    EditorContext
        |
        v
MeshEvaluator
        |
        v
RenderExtractor with stable source IDs
        |
        v
Vulkan GPU caches / overlays
```

Then add:

```text
touch tap candidate
-> CPU ray/picking query
-> stable engine Object/Face ID
-> EditorContext selection
-> renderer highlight/overlay
```

No Java-side or Vulkan-array-index selection authority should be introduced.

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
