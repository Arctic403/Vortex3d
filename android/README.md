# Vortex3D Android host

This directory is the Android host around the portable Vortex3D C++ engine. The host owns Android/JNI/lifecycle and Vulkan presentation concerns; `include/vortex` and the portable engine sources remain platform-independent.

The v0.3 viewport bootstrap replaces the old text-only host proof with a real `SurfaceView` backed by the first Vortex-owned Vulkan renderer. It creates the Vulkan instance/device/surface/swapchain, presents a continuous clear frame, survives surface recreation, and reports GPU/API/ABI diagnostics through the Android overlay. Mesh upload, camera, picking, and editor overlays are the next renderer increments.

Supported ABIs remain intentionally explicit:

- `armeabi-v7a` (32-bit),
- `arm64-v8a` (64-bit).

Open `android/` in Android Studio or invoke Gradle from an Android SDK/NDK environment. No Gradle wrapper JAR is committed because repository policy rejects generated/binary artifacts.
