# Vortex3D Android host

This directory is the Android host around the portable Vortex3D C++ engine. The host owns Android/JNI/lifecycle and Vulkan presentation concerns; `include/vortex` and the portable engine sources remain platform-independent.

## Current viewport state

The Android host runs the Vortex-owned Vulkan viewport inside a `SurfaceView`. Through Phase 6 the current path includes:

- Vulkan instance/device/surface/swapchain lifecycle,
- depth buffering,
- a persistent native `Document + EditorHistory + EditorContext` session,
- evaluated engine geometry through `MeshEvaluator -> RenderExtractor`,
- indexed multi-object drawing,
- XZ grid plus XYZ axes,
- native camera matrix handling,
- one-finger orbit with tap-vs-drag arbitration,
- coherent two-finger pan,
- symmetric filtered pinch zoom,
- nearest-hit CPU object picking with stable `ObjectId + FaceId` resolution,
- active-object outline and Gizmo System v2 transform controls,
- engine-owned object translation/rotation/scale,
- engine-derived world transforms shared by rendering, picking, outline and gizmo placement,
- touch-interactive Move axes/planes/center, Rotate XYZ/view rings, and Scale axes/planes/uniform controls,
- Global / Local / View transform-orientation controls in the Android test toolbar,
- transient transform previews with one `SetObjectTransformCommand` committed per completed drag,
- native Undo / Redo controls for committed transform edits,
- safe cancellation when multitouch, lifecycle, tool changes, or `ACTION_CANCEL` interrupts a drag,
- safe surface recreation and resize handling,
- GPU/API/ABI diagnostics in the Android overlay.

Authored object transforms stay in the portable engine. Vulkan receives only derived world matrices and local-space render geometry; it never becomes the source of truth for object placement. During a gizmo drag the preview remains transient host/renderer state. `ACTION_UP` commits exactly one command into `EditorHistory`, while cancellation restores the authored transform with no history entry.

On the experimental `work/gizmo-visual-polish` branch, object rotation is quaternion-authoritative and gizmo constraints live in portable `include/vortex/editor/gizmo.hpp` / `src/editor/gizmo.cpp`. Vulkan is responsible for camera rays, picking and drawing only. See `docs/GIZMO_SYSTEM_V2.md`.

All viewport JNI, touch routing, lifecycle callbacks, transform-tool calls and frame-loop callbacks currently execute on the Activity/UI-thread path. Do not move `SurfaceView` or Vulkan JNI work to background threads without first introducing an explicit synchronization/ownership design.

## ABI support

Supported Android ABIs are intentionally explicit and permanent unless a future compatibility review changes them:

- `armeabi-v7a` (32-bit ARM),
- `arm64-v8a` (64-bit ARM).

Gradle produces three debug APK variants: an ARMv7-only APK, an ARM64-only APK, and a universal APK containing both native libraries. GitHub CI separately cross-compiles the native engine/renderer for each ABI, then builds the Gradle APK set and verifies that `libvortex_android.so` is packaged only in the expected ABI directories before uploading the APKs as workflow artifacts.

Use the `armeabi-v7a` APK for 32-bit Android targets. Use the `arm64-v8a` APK on 64-bit Android devices. The universal APK is useful for convenience/testing but is larger because it carries both native builds.

## Phase 6 device gate

Before Phase 6 is called device-verified on the 32-bit target, test selection plus X/Y/Z Move, Rotate and Scale, then confirm Undo/Redo, drag cancellation via a second finger, normal orbit/pan/pinch, background/resume and surface recreation. Rendering, picking, outline and gizmo feedback should stay aligned throughout a preview and no Vulkan errors should appear.

## Host rules

Android input and lifecycle calls stay in the Java host and cross into C++ through the narrow JNI bridge. Renderer and engine state remain native-owned. Persistent authored state must never be invented in Java or Vulkan merely for UI convenience.

Open `android/` in Android Studio or invoke Gradle from an Android SDK/NDK environment. No Gradle wrapper JAR is committed because repository policy rejects generated/binary artifacts.
