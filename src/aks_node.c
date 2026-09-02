#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#define NAPI_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#define NAPI_EXPORT __attribute__((visibility("default")))
#endif

typedef void* napi_env;
typedef void* napi_value;
typedef void* napi_callback_info;
typedef napi_value (*napi_callback)(napi_env env, napi_callback_info info);

typedef struct {
    const char* utf8name;
    void* name;
    napi_callback method;
    void* getter;
    void* setter;
    napi_value value;
    int attributes;
    void* data;
} napi_property_descriptor;

typedef int (*napi_get_cb_info_t)(napi_env env, napi_callback_info cbinfo, size_t* argc, napi_value* argv, napi_value* this_arg, void** data);
typedef int (*napi_get_value_string_utf8_t)(napi_env env, napi_value value, char* buf, size_t bufsize, size_t* result);
typedef int (*napi_create_int32_t)(napi_env env, int32_t value, napi_value* result);
typedef int (*napi_define_properties_t)(napi_env env, napi_value object, size_t property_count, const napi_property_descriptor* properties);
typedef int (*napi_create_string_utf8_t)(napi_env env, const char* str, size_t length, napi_value* result);

static napi_get_cb_info_t p_napi_get_cb_info = NULL;
static napi_get_value_string_utf8_t p_napi_get_value_string_utf8 = NULL;
static napi_create_int32_t p_napi_create_int32 = NULL;
static napi_define_properties_t p_napi_define_properties = NULL;
static napi_create_string_utf8_t p_napi_create_string_utf8 = NULL;

static int ResolveNAPISymbols() {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA(NULL); // node.exe or bun.exe
    if (!h) return 0;
    p_napi_get_cb_info = (napi_get_cb_info_t)GetProcAddress(h, "napi_get_cb_info");
    p_napi_get_value_string_utf8 = (napi_get_value_string_utf8_t)GetProcAddress(h, "napi_get_value_string_utf8");
    p_napi_create_int32 = (napi_create_int32_t)GetProcAddress(h, "napi_create_int32");
    p_napi_define_properties = (napi_define_properties_t)GetProcAddress(h, "napi_define_properties");
    p_napi_create_string_utf8 = (napi_create_string_utf8_t)GetProcAddress(h, "napi_create_string_utf8");
#else
    void *h = dlopen(NULL, RTLD_LAZY);
    if (!h) return 0;
    p_napi_get_cb_info = (napi_get_cb_info_t)dlsym(h, "napi_get_cb_info");
    p_napi_get_value_string_utf8 = (napi_get_value_string_utf8_t)dlsym(h, "napi_get_value_string_utf8");
    p_napi_create_int32 = (napi_create_int32_t)dlsym(h, "napi_create_int32");
    p_napi_define_properties = (napi_define_properties_t)dlsym(h, "napi_define_properties");
    p_napi_create_string_utf8 = (napi_create_string_utf8_t)dlsym(h, "napi_create_string_utf8");
#endif
    return (p_napi_get_cb_info && p_napi_get_value_string_utf8 && p_napi_create_int32 && p_napi_define_properties && p_napi_create_string_utf8);
}

static napi_value JS_RunAKS(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    p_napi_get_cb_info(env, info, &argc, argv, NULL, NULL);

    char source[4096];
    size_t read_bytes = 0;
    p_napi_get_value_string_utf8(env, argv[0], source, sizeof(source), &read_bytes);

    extern int AKSRunSource(const char *source);
    int res = AKSRunSource(source);

    napi_value js_res;
    p_napi_create_int32(env, res, &js_res);
    return js_res;
}

static napi_value NapiRegisterModule(napi_env env, napi_value exports) {
    if (!ResolveNAPISymbols()) {
        return exports;
    }
    napi_property_descriptor desc = {
        "runAKS",
        NULL,
        JS_RunAKS,
        NULL,
        NULL,
        NULL,
        0,
        NULL
    };
    p_napi_define_properties(env, exports, 1, &desc);
    return exports;
}

// Node's native-addon loader (process.dlopen) resolves this exact symbol
// by literal name via dlsym/GetProcAddress -- it's the N-API v1 ABI entry
// point, not an AKS naming choice, so the exported name itself can't
// change. Keep it as a thin pass-through to the core implementation above.
NAPI_EXPORT napi_value napi_register_module_v1(napi_env env, napi_value exports) {
    return NapiRegisterModule(env, exports);
}
