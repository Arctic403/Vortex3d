#include "viewport_host.hpp"

#include "vortex/engine.hpp"

#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdint>
#include <new>
#include <optional>
#include <string>

namespace {

[[nodiscard]] vortex::android::ViewportHost* hostFromHandle(const jlong handle) noexcept {
    return reinterpret_cast<vortex::android::ViewportHost*>(static_cast<std::uintptr_t>(handle));
}

[[nodiscard]] jlong handleFromHost(vortex::android::ViewportHost* host) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(host));
}

[[nodiscard]] std::optional<vortex::TransformToolMode> transformToolMode(const jint mode) noexcept {
    using vortex::TransformToolMode;
    switch (mode) {
        case 0:
            return TransformToolMode::Move;
        case 1:
            return TransformToolMode::Rotate;
        case 2:
            return TransformToolMode::Scale;
        default:
            return std::nullopt;
    }
}

[[nodiscard]] std::optional<vortex::TransformOrientation> transformOrientation(
    const jint orientation) noexcept {
    using vortex::TransformOrientation;
    switch (orientation) {
        case 0:
            return TransformOrientation::Global;
        case 1:
            return TransformOrientation::Local;
        case 2:
            return TransformOrientation::View;
        default:
            return std::nullopt;
    }
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortex3d_app_MainActivity_engineVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(vortex::engineArchitectureVersion);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_vortex3d_app_MainActivity_nativeCreateRenderer(JNIEnv*, jclass) {
    return handleFromHost(new (std::nothrow) vortex::android::ViewportHost{});
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeSetDisplayDensity(
    JNIEnv*, jclass, const jlong handle, const jfloat density) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->setDisplayDensity(density) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_vortex3d_app_MainActivity_nativeDestroyRenderer(JNIEnv*, jclass, const jlong handle) {
    delete hostFromHandle(handle);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeSurfaceCreated(
    JNIEnv* env,
    jclass,
    const jlong handle,
    jobject surface) {
    auto* host = hostFromHandle(handle);
    if (host == nullptr || surface == nullptr) {
        return JNI_FALSE;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return JNI_FALSE;
    }
    return host->attach(window) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeSurfaceChanged(JNIEnv*, jclass, const jlong handle) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->resize() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_vortex3d_app_MainActivity_nativeSurfaceDestroyed(JNIEnv*, jclass, const jlong handle) {
    auto* host = hostFromHandle(handle);
    if (host != nullptr) {
        host->detach();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeRenderFrame(JNIEnv*, jclass, const jlong handle) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->render() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeOrbitCamera(
    JNIEnv*, jclass, const jlong handle, const jfloat deltaX, const jfloat deltaY) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->orbitCamera(deltaX, deltaY) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativePanCamera(
    JNIEnv*, jclass, const jlong handle, const jfloat deltaX, const jfloat deltaY) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->panCamera(deltaX, deltaY) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeZoomCamera(
    JNIEnv*, jclass, const jlong handle, const jfloat scaleFactor) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->zoomCamera(scaleFactor) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeTapViewport(
    JNIEnv*, jclass, const jlong handle, const jfloat x, const jfloat y) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->tap(x, y) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeSetTransformTool(
    JNIEnv*, jclass, const jlong handle, const jint mode) {
    auto* host = hostFromHandle(handle);
    const auto resolvedMode = transformToolMode(mode);
    return host != nullptr && resolvedMode && host->setTransformTool(*resolvedMode)
        ? JNI_TRUE
        : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeSetTransformOrientation(
    JNIEnv*, jclass, const jlong handle, const jint orientation) {
    auto* host = hostFromHandle(handle);
    const auto resolvedOrientation = transformOrientation(orientation);
    return host != nullptr && resolvedOrientation &&
                   host->setTransformOrientation(*resolvedOrientation)
        ? JNI_TRUE
        : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeBeginTransformGesture(
    JNIEnv*,
    jclass,
    const jlong handle,
    const jint mode,
    const jfloat x,
    const jfloat y) {
    auto* host = hostFromHandle(handle);
    const auto resolvedMode = transformToolMode(mode);
    return host != nullptr && resolvedMode && host->beginTransformGesture(*resolvedMode, x, y)
        ? JNI_TRUE
        : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeUpdateTransformGesture(
    JNIEnv*, jclass, const jlong handle, const jfloat x, const jfloat y) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->updateTransformGesture(x, y) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeEndTransformGesture(
    JNIEnv*, jclass, const jlong handle, const jboolean commit) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->endTransformGesture(commit == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeUndo(JNIEnv*, jclass, const jlong handle) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->undo() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeRedo(JNIEnv*, jclass, const jlong handle) {
    auto* host = hostFromHandle(handle);
    return host != nullptr && host->redo() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortex3d_app_MainActivity_nativeRendererInfo(JNIEnv* env, jclass, const jlong handle) {
    auto* host = hostFromHandle(handle);
    const std::string info = host == nullptr ? "Viewport host unavailable" : host->info();
    return env->NewStringUTF(info.c_str());
}