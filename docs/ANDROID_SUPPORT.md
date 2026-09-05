# Android and 32-bit Support Contract

Vortex3D Native supports Android as its first application host.

## Required ABIs

The portable core must compile for both:

- `armeabi-v7a` — 32-bit ARM,
- `arm64-v8a` — 64-bit ARM.

A feature is not Android-ready if it only compiles on ARM64 without an explicit documented reason.

## CI compile gate

GitHub Actions cross-compiles the Android JNI shell and therefore the complete `Vortex3D::engine` dependency graph with the Android NDK for both required ABIs. The matrix also passes `VORTEX_EXPECT_POINTER_BITS=32` for `armeabi-v7a` and `64` for `arm64-v8a`; CMake fails configuration if the selected toolchain does not match the expected pointer width.

The CI build uses the latest NDK installed by the GitHub-hosted runner rather than embedding Android framework code in the core. Release/application builds may pin a tested NDK version separately.

The current compile baseline uses API level 26. Changing that baseline is a product/platform decision, not a reason to weaken the portable engine boundary.

CI also builds split ARMv7/ARM64 debug APKs plus a universal APK and verifies that `libvortex_android.so` is packaged under the expected ABI directories.

## Android architecture

```text
Java Android host
  - Activity/lifecycle
  - SurfaceView ownership
  - touch gesture/tool recognition
  - future Storage Access Framework / IME / clipboard / haptics
        |
        | narrow JNI boundary
        v
C++ Vortex engine/editor session
  - document + object transforms
  - editor context
  - mesh kernel
  - commands/undo
  - evaluator
  - serialization
  - transient transform tool session
        |
        v
RenderExtractor
        |
        v
Vulkan viewport backend
  - derived draw ranges/world matrices
  - picking
  - selection/gizmo overlays
```

The Android layer hosts the engine. It does not become the engine. Persistent transform state remains in `Document`, while Java and Vulkan hold only input/tool state or rebuildable renderer data.

## 32-bit constraints are design inputs

32-bit devices have a smaller process address space and are more sensitive to fragmentation and duplicate buffers. Vortex therefore follows these rules from the start:

- fixed-width integers in file formats and persistent IDs,
- no persisted pointer-sized values,
- `size_t` is never serialized as identity,
- bounded Document and Mesh undo histories,
- avoid giant all-scene snapshots,
- evaluated geometry is cacheable/discardable,
- GPU staging memory has explicit lifetimes,
- textures and heavy assets may be streamed/evicted,
- memory statistics are exposed in diagnostics,
- stress fixtures include large meshes and long undo histories.

Interactive transform previews are also deliberately lightweight: a drag does not create a stream of `Document` revisions or undo records. One completed transform gesture commits one command.

## Platform separation

The portable core does not include Android SDK/NDK platform APIs merely because it is compiled by the NDK. Android owns lifecycle, surfaces, storage/URI integration, clipboard, IME, haptics, and device capability queries behind a narrow platform layer.

The engine sees portable services/data rather than Android `Uri`, Activity, Surface, or Java objects.

## Lifecycle requirements

The Android host must survive surface recreation, rotation/resizing, background/foreground, activity recreation, process recovery, interrupted save/autosave, and low-memory signals. Project correctness must not depend on a Vulkan surface remaining alive.

The current renderer separates device/session lifetime from swapchain/surface lifetime and waits for Vulkan work before tearing down swapchain resources. An in-progress transform preview is cancelled before pause/destroy or surface teardown, leaving authored `Document` state unchanged.

## Current host implementation

`android/app` is a live Vulkan editor viewport. `MainActivity.java` owns the `SurfaceView`, `SurfaceHolder.Callback`, `Choreographer` frame loop, camera gesture recognition, and transform-tool mode toolbar. JNI calls remain on that UI-thread path unless a future threading design explicitly changes ownership.

The native viewport currently provides:

- Vulkan instance/device/surface/swapchain,
- ARMv7 + ARM64 builds,
- depth buffering,
- persistent `Document + EditorHistory + EditorContext`,
- evaluated local-space engine mesh extraction,
- multiple object draw ranges with engine-derived world matrices,
- grid and XYZ axes,
- camera matrix push constants,
- one-finger orbit with tap arbitration,
- coherent two-finger pan,
- symmetric filtered pinch zoom,
- stable-ID object/face picking,
- selection outline and XYZ transform gizmo,
- interactive Move / Rotate / Scale axis tools,
- transient transform preview with one-command commit,
- native transform Undo / Redo controls,
- surface resize/recreation handling.

Selection belongs to `EditorContext`; authored object transforms belong to `Document`; renderer arrays and matrices remain derived data only. See `docs/PHASE6_TRANSFORMS.md` for the transform interaction and device-verification gate.
