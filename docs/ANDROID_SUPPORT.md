# Android and 32-bit Support Contract

Vortex3D Native supports Android as its first application host.

## Required ABIs

From the first Android phase, build both:

- `armeabi-v7a` — 32-bit ARM
- `arm64-v8a` — 64-bit ARM

The portable core must remain compilable for both targets. A feature is not considered Android-ready if it only works on ARM64 without an explicit documented reason.

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

- fixed-width integers in file formats and wire structures,
- no persisted pointer-sized values,
- avoid giant all-scene snapshots for routine undo,
- evaluated geometry is cacheable/discardable,
- GPU staging memory has explicit lifetimes,
- textures and heavy assets may be streamed/evicted,
- memory statistics are exposed in diagnostics,
- stress fixtures include large meshes and long undo histories.

## Vulkan capability policy

Do not equate ABI with GPU capability.

At runtime, query and record:

- Vulkan API version,
- physical device/vendor/driver,
- memory heaps/budgets where available,
- supported formats,
- queue families,
- required/optional extensions,
- limits such as max image dimensions and descriptor limits.

Define capability tiers if necessary. A 32-bit device may still have a perfectly usable Vulkan GPU; an ARM64 device may still have driver limitations.

The renderer must have explicit fallbacks for optional features rather than crashing or silently corrupting output.

## File access

Use Android's Storage Access Framework for user-selected project/import/export locations. Keep autosave/recovery in app-owned storage.

The engine sees abstract streams/filesystem services rather than Android `Uri` objects.

## Lifecycle requirements

The Android host must correctly survive:

- surface recreation,
- rotation/resizing,
- app background/foreground,
- activity recreation,
- process death followed by recovery,
- interrupted save/autosave,
- low-memory signals.

Project correctness must not depend on a Vulkan surface staying alive.

## Build requirement example

When the Android project is introduced, the ABI configuration should include both targets:

```kotlin
ndk {
    abiFilters += listOf("armeabi-v7a", "arm64-v8a")
}
```

The exact SDK/NDK versions may move over time; ABI support and architecture boundaries are the durable contract.
