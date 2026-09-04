# Vortex3D Android shell v0.1

This directory is the Android host around the portable Vortex3D C++ engine. The host owns Android/JNI/lifecycle concerns; `include/vortex` and `src` remain platform-independent.

The application is intentionally minimal in v0.1: it proves that the complete `Vortex3D::engine` target can be linked into an APK and reached through JNI. The actual viewport/input UI will grow here without leaking Android types into the engine.

Supported ABIs are intentionally explicit:

- `armeabi-v7a` (32-bit),
- `arm64-v8a` (64-bit).

Open `android/` in Android Studio or invoke Gradle from an Android SDK/NDK environment. No Gradle wrapper JAR is committed because repository policy rejects generated/binary artifacts.
