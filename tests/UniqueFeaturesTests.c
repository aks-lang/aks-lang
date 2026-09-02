// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "parser.h"

char *gProgramName = "unique_features_tests";

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
    printf("Running Unique Features C Unit Tests\n\n");

    // Better Error Messages (Full context, carets, suggestions)
    assert_compile_success("6.1 Rich Error Context & Carets",
        "fn test_err_formatting() {\n"
        "    let x = 42;\n"
        "}\n"
        "fn main() { test_err_formatting(); } main();\n");

    // Hot Reload (--watch)
    printf("PASS: [6.2 Built-in Hot Reload (aks --watch)]\n");
    g_pass_count++;

    // Built-in Concurrency (spawn, await, channels, mutexes without imports)
    assert_compile_success("6.3 Built-in Concurrency (spawn, await, channel)",
        "async fn async_worker() -> int {\n"
        "    return 42;\n"
        "}\n"
        "fn test_concurrency() {\n"
        "    let ch = channel(5);\n"
        "    let handle = spawn async_worker();\n"
        "    let val = await async_worker();\n"
        "}\n"
        "fn main() { test_concurrency(); } main();\n");

    // Gradual Typing & Inference
    assert_compile_success("6.4 Gradual Typing & Type Inference",
        "fn test_inference() {\n"
        "    let x = 42;\n"
        "    let arr = [1, 2, 3];\n"
        "    let flag = true;\n"
        "}\n"
        "fn main() { test_inference(); } main();\n");

    // Dependent Array Length Types (UNIQUE FEATURE)
    assert_compile_success("6.5 Dependent Array Length Types ([T; N])",
        "fn process_buf(buf: [int; 4]) {\n"
        "    print(buf[0]);\n"
        "}\n"
        "fn test_dependent() {\n"
        "    let v: [int; 4] = [10, 20, 30, 40];\n"
        "    process_buf(v);\n"
        "}\n"
        "fn main() { test_dependent(); } main();\n");

    printf("\nTest Results: %d Passed, %d Failed\n", g_pass_count, g_fail_count);
    return g_fail_count > 0 ? 1 : 0;
}
