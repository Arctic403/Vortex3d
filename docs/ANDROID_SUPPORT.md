# Android and 32-bit Support Contract

Vortex3D Native supports Android as its first application host.

## Required ABIs

The portable core must compile for both:

- `armeabi-v7a` — 32-bit ARM,
- `arm64-v8a` — 64-bit ARM.

A feature is not Android-ready if it only compiles on ARM64 without an explicit documented reason.

## CI compile gate

GitHub Actions cross-compiles `vortex_core` with the Android NDK for both required ABIs. The matrix also passes `VORTEX_EXPECT_POINTER_BITS=32` for `armeabi-v7a` and `64` for `arm64-v8a`; CMake fails configuration if the selected toolchain does not match the expected pointer width.

The CI build uses the latest NDK installed by the GitHub-hosted runner rather than embedding Android framework code in the core. Release/application builds may pin a tested NDK version separately.

The current compile baseline uses API level 26. Changing that baseline is a product/platform decision, not a reason to weaken the portable engine boundary.

## Android architecture

```text
Kotlin / Android host
  - Activity/lifecycle
  - Storage Access Framework
  - IME/keyboard
  - clipboard/share intents
  - haptics
  - permissions/system UI
  - device capability reporting
        |
        | JNI / narrow native boundary
        v
C++ Vortex engine
  - document
  - mesh kernel
  - commands/undo
  - evaluator
  - serialization
  - import/export
        |
        v
Vulkan renderer backend
```

The Android layer hosts the engine. It does not become the engine.

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

## Platform separation

The portable core does not include Android SDK/NDK platform APIs merely because it is compiled by the NDK. Android owns lifecycle, surfaces, storage/URI integration, clipboard, IME, haptics, and device capability queries behind a narrow platform layer.

The engine sees portable services/data rather than Android `Uri`, Activity, Surface, or Java/Kotlin objects.

## Lifecycle requirements

The eventual Android host must survive surface recreation, rotation/resizing, background/foreground, activity recreation, process recovery, interrupted save/autosave, and low-memory signals. Project correctness must not depend on a Vulkan surface remaining alive.
