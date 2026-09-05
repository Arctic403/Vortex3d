# Vortex3D Android host

This directory is the Android host around the portable Vortex3D C++ engine. The host owns Android/JNI/lifecycle and Vulkan presentation concerns; `include/vortex` and the portable engine sources remain platform-independent.

## Current viewport state

The v0.3 Android host now runs the real Vortex-owned Vulkan viewport inside a `SurfaceView`. The current renderer includes:

- Vulkan instance/device/surface/swapchain lifecycle,
- depth buffering,
- evaluated engine mesh upload through `RenderExtractor`,
- indexed mesh drawing,
- XZ grid plus XYZ axes,
- native camera matrix handling,
- one-finger orbit,
- coherent two-finger pan,
- symmetric filtered pinch zoom,
- safe surface recreation and resize handling,
- GPU/API/ABI diagnostics in the Android overlay.

The current cube is still a renderer-bootstrap fixture. The next editor-facing step is to replace that temporary fixture plumbing with a persistent native editor/session path so picking and selection resolve back to stable engine IDs rather than renderer-owned state.

## ABI support

Supported Android ABIs are intentionally explicit and permanent unless a future compatibility review changes them:

- `armeabi-v7a` (32-bit ARM),
- `arm64-v8a` (64-bit ARM).

Gradle produces three debug APK variants: an ARMv7-only APK, an ARM64-only APK, and a universal APK containing both native libraries. GitHub CI separately cross-compiles the native engine/renderer for each ABI, then builds the Gradle APK set and verifies that `libvortex_android.so` is packaged only in the expected ABI directories before uploading the APKs as workflow artifacts.

Use the `armeabi-v7a` APK for 32-bit Android targets. Use the `arm64-v8a` APK on 64-bit Android devices. The universal APK is useful for convenience/testing but is larger because it carries both native builds.

## Host rules

Android input and lifecycle calls stay in the Java host and cross into C++ through the narrow JNI bridge. Renderer and engine state remain native-owned. The current frame loop and Vulkan JNI calls execute from the Activity/UI-thread path; do not move them to background threads without first introducing an explicit synchronization/ownership design.

Open `android/` in Android Studio or invoke Gradle from an Android SDK/NDK environment. No Gradle wrapper JAR is committed because repository policy rejects generated/binary artifacts.
