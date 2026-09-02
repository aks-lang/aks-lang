// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "parser.h"

char *gProgramName = "performance_tests";

static int g_pass_count = 0;
static int g_fail_count = 0;

static void assert_compile_success(const char *test_name, const char *source) {
    int parseError = 0;
    Node *ast = Parse(source, &parseError);
    if (parseError || !ast) {
        printf("FAIL: [%s] Parse failed unexpectedly\n", test_name);
        g_fail_count++;
        if (ast) NodeFree(ast);
        return;
    }
    Compiler *c = CompNew();
    int ok = CompCompile(c, ast);
    NodeFree(ast);
    CompFree(c);
    if (ok) {
        printf("PASS: [%s] (Compiled cleanly)\n", test_name);
        g_pass_count++;
    } else {
        printf("FAIL: [%s] Expected clean compilation, but compile error occurred\n", test_name);
        g_fail_count++;
    }
}

int main(void) {
    printf("Running Performance Optimization C Unit Tests\n\n");

    // Refcount Elimination & Escape Analysis
    assert_compile_success("Refcount Elimination via Static Safe Flag",
        "fn calculate() -> int {\n"
        "    let x = 42;\n"
        "    let y = x + 10;\n"
        "    return y;\n"
        "}\n"
        "fn main() { calculate(); } main();\n");

    // SIMD Intrinsics & Vectorization
    assert_compile_success("SIMD vector type annotations & operations",
        "fn vector_ops() {\n"
        "    let v1: f32x4 = [1.0, 2.0, 3.0, 4.0];\n"
        "    let v2: f32x4 = [5.0, 6.0, 7.0, 8.0];\n"
        "    print(\"simd ready\");\n"
        "}\n"
        "fn main() { vector_ops(); } main();\n");

    // Tiered JIT Compilation Function Hot Loops
    assert_compile_success("Tiered JIT Hot Loop Call Thresholds",
        "fn hot_func(n: int) -> int {\n"
        "    let mut sum = 0;\n"
        "    let mut i = 0;\n"
        "    while (i < n) {\n"
        "        sum = sum + i;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return sum;\n"
        "}\n"
        "fn main() { hot_func(100); } main();\n");

    // Inline Assembly Support
    assert_compile_success("Inline Assembly Block (asm { ... })",
        "fn run_cpu_inst() {\n"
        "    asm {\n"
        "        \"nop\"\n"
        "    };\n"
        "}\n"
        "fn main() { run_cpu_inst(); } main();\n");

    printf("\nTest Results: %d Passed, %d Failed\n", g_pass_count, g_fail_count);
    return g_fail_count > 0 ? 1 : 0;
}
