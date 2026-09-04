# Vortex3D Android host

This directory is the Android host around the portable Vortex3D C++ engine. The host owns Android/JNI/lifecycle and Vulkan presentation concerns; `include/vortex` and the portable engine sources remain platform-independent.

The v0.3 viewport bootstrap replaces the old text-only host proof with a real `SurfaceView` backed by the first Vortex-owned Vulkan renderer. It creates the Vulkan instance/device/surface/swapchain, presents a continuous clear frame, survives surface recreation, and reports GPU/API/ABI diagnostics through the Android overlay. Mesh upload, camera, picking, and editor overlays are the next renderer increments.

The launcher is implemented in Kotlin 1.9+ and forwards `SurfaceHolder.Callback` plus Activity lifecycle events to the JNI bridge on the UI thread. `Choreographer` provides vsync-locked frame pacing; pause and surface teardown remove the queued callback before native surface destruction. The Activity also guards renderer handles and JNI failures so surface recreation cannot accidentally render through a destroyed `ANativeWindow`.

Supported Android ABIs are intentionally explicit and permanent unless a future compatibility review changes them:

- `armeabi-v7a` (32-bit ARM),
- `arm64-v8a` (64-bit ARM).

Gradle produces three debug APK variants: an ARMv7-only APK, an ARM64-only APK, and a universal APK containing both native libraries. GitHub CI separately cross-compiles the native engine/renderer for each ABI, then builds the Gradle APK set and verifies that `libvortex_android.so` is packaged only in the expected ABI directories before uploading the APKs as workflow artifacts.

Use the `armeabi-v7a` APK for the current 32-bit Samsung target. Use the `arm64-v8a` APK on modern 64-bit Android devices. The universal APK is useful for convenience/testing but is larger because it carries both native builds.

Open `android/` in Android Studio or invoke Gradle from an Android SDK/NDK environment. No Gradle wrapper JAR is committed because repository policy rejects generated/binary artifacts.
