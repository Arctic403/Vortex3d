#include "vulkan_viewport.hpp"

#include "vortex/engine.hpp"

#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdint>
#include <new>
#include <string>

namespace {

[[nodiscard]] vortex::android::VulkanViewport* viewportFromHandle(const jlong handle) noexcept {
    return reinterpret_cast<vortex::android::VulkanViewport*>(static_cast<std::uintptr_t>(handle));
}

[[nodiscard]] jlong handleFromViewport(vortex::android::VulkanViewport* viewport) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(viewport));
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortex3d_app_MainActivity_engineVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(vortex::engineArchitectureVersion);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_vortex3d_app_MainActivity_nativeCreateRenderer(JNIEnv*, jclass) {
    return handleFromViewport(new (std::nothrow) vortex::android::VulkanViewport{});
}

extern "C" JNIEXPORT void JNICALL
Java_com_vortex3d_app_MainActivity_nativeDestroyRenderer(JNIEnv*, jclass, const jlong handle) {
    delete viewportFromHandle(handle);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeSurfaceCreated(
    JNIEnv* env,
    jclass,
    const jlong handle,
    jobject surface) {
    auto* viewport = viewportFromHandle(handle);
    if (viewport == nullptr || surface == nullptr) {
        return JNI_FALSE;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return JNI_FALSE;
    }
    return viewport->attach(window) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeSurfaceChanged(JNIEnv*, jclass, const jlong handle) {
    auto* viewport = viewportFromHandle(handle);
    return viewport != nullptr && viewport->resize() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_vortex3d_app_MainActivity_nativeSurfaceDestroyed(JNIEnv*, jclass, const jlong handle) {
    auto* viewport = viewportFromHandle(handle);
    if (viewport != nullptr) {
        viewport->detach();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeRenderFrame(JNIEnv*, jclass, const jlong handle) {
    auto* viewport = viewportFromHandle(handle);
    return viewport != nullptr && viewport->render() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeOrbitCamera(
    JNIEnv*, jclass, const jlong handle, const jfloat deltaX, const jfloat deltaY) {
    auto* viewport = viewportFromHandle(handle);
    return viewport != nullptr && viewport->orbitCamera(deltaX, deltaY) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativePanCamera(
    JNIEnv*, jclass, const jlong handle, const jfloat deltaX, const jfloat deltaY) {
    auto* viewport = viewportFromHandle(handle);
    return viewport != nullptr && viewport->panCamera(deltaX, deltaY) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vortex3d_app_MainActivity_nativeZoomCamera(
    JNIEnv*, jclass, const jlong handle, const jfloat scaleFactor) {
    auto* viewport = viewportFromHandle(handle);
    return viewport != nullptr && viewport->zoomCamera(scaleFactor) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortex3d_app_MainActivity_nativeRendererInfo(JNIEnv* env, jclass, const jlong handle) {
    auto* viewport = viewportFromHandle(handle);
    const std::string info = viewport == nullptr ? "Renderer handle unavailable" : viewport->info();
    return env->NewStringUTF(info.c_str());
}
