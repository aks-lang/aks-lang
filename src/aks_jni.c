#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#define JNIEXPORT __declspec(dllexport)
#define JNICALL __stdcall
#else
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL
#define __stdcall
#endif

struct JNINativeInterface;
typedef const struct JNINativeInterface *JNIEnv;
typedef void *jobject;
typedef void *jstring;
typedef int32_t jint;
typedef unsigned char jboolean;

struct JNINativeInterface {
    void* reserved[169];
    const char* (__stdcall *GetStringUTFChars)(JNIEnv *env, jstring str, jboolean *isCopy);
    void (__stdcall *ReleaseStringUTFChars)(JNIEnv *env, jstring str, const char *chars);
};

extern int AKSRunSource(const char *source);

static jint JNIRunAKS(JNIEnv *env, jobject obj, jstring code) {
    (void)obj;
    if (!env || !code) return -1;

    const char *str = (*env)->GetStringUTFChars(env, code, NULL);
    if (!str) return -2;

    int res = AKSRunSource(str);

    (*env)->ReleaseStringUTFChars(env, code, str);

    return res;
}

// The JVM resolves native methods declared without a matching
// RegisterNatives() call by their mangled JNI symbol name
// (Java_<Class>_<Method>) via dlsym/GetProcAddress -- it's the JNI
// implicit-linking ABI, not an AKS naming choice, so the exported name
// itself can't change. Keep it as a thin pass-through to the core
// implementation above.
JNIEXPORT jint JNICALL Java_AKS_run(JNIEnv *env, jobject obj, jstring code) {
    return JNIRunAKS(env, obj, code);
}

JNIEXPORT jint JNICALL Java_aks_run(JNIEnv *env, jobject obj, jstring code) {
    return JNIRunAKS(env, obj, code);
}
