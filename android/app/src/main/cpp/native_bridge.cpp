#include "vortex/engine.hpp"

#include <jni.h>

extern "C" JNIEXPORT jstring JNICALL
Java_com_vortex3d_app_MainActivity_engineVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(vortex::engineArchitectureVersion);
}
