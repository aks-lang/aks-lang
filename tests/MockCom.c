#include <windows.h>

typedef struct IMock IMock;
typedef struct IMockVtbl IMockVtbl;

struct IMockVtbl {
    int (__stdcall *AddRef)(IMock *this);
    int (__stdcall *Release)(IMock *this);
    int (__stdcall *Add)(IMock *this, int a, int b);
    double (__stdcall *GetVal)(IMock *this, double x);
};

struct IMock {
    IMockVtbl *lpVtbl;
    int ref_count;
};

static int __stdcall Mock_AddRef(IMock *self) {
    return ++(self->ref_count);
}

static int __stdcall Mock_Release(IMock *self) {
    int rc = --(self->ref_count);
    if (rc == 0) {
        GlobalFree(self);
    }
    return rc;
}

static int __stdcall Mock_Add(IMock *self, int a, int b) {
    return a + b;
}

static double __stdcall Mock_GetVal(IMock *self, double x) {
    return x * 2.0;
}

static IMockVtbl g_MockVtbl = {
    Mock_AddRef,
    Mock_Release,
    Mock_Add,
    Mock_GetVal
};

__declspec(dllexport) void* __stdcall CreateMockCOMObject() {
    IMock *obj = (IMock*)GlobalAlloc(GPTR, sizeof(IMock));
    obj->lpVtbl = &g_MockVtbl;
    obj->ref_count = 1;
    return obj;
}
