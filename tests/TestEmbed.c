#include "aks.h"
#include <stdio.h>

#include <stdlib.h>
#include <assert.h>
#include <string.h>

static int g_native_called = 0;

AKSValue c_add(AksVmT *vm, int argc, AKSValue *args) {
    if (argc != 2) {
        fprintf(stderr, "Error: argc is %d, expected 2\n", argc);
        exit(1);
    }
    int64_t a = AKSValToInt(args[0]);
    int64_t b = AKSValToInt(args[1]);
    g_native_called = 1;
    return AKSValInt(a + b);
}

int main(void) {
    printf("Testing Embeddable C API...\n"); fflush(stdout);
    AksVmT *vm = AKSNewVM();
    if (!vm) {
        fprintf(stderr, "Error: Failed to create VM!\n");
        return 1;
    }

    AKSRegisterNative(vm, "c_add", c_add);

    // Test source compilation and execution using registered native
    const char *source = "let x = c_add(37, 5); if (x != 42) { exit(1); }";
    int run_res = AKSRunSource(vm, source);
    assert(run_res == 0);
    assert(g_native_called == 1);


    // Test VM cleanup
    AKSFreeVM(vm);
    printf("Embeddable C API Test Passed Successfully!\n"); fflush(stdout);
    return 0;
}
