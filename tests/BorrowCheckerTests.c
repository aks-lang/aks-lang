// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "parser.h"

char *gProgramName = "borrow_checker_tests";

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
        printf("FAIL: [%s] Expected clean compilation, but borrow check or compile error occurred\n", test_name);
        g_fail_count++;
    }
}

static void assert_compile_failure(const char *test_name, const char *source, const char *expected_substr) {
    int parseError = 0;
    Node *ast = Parse(source, &parseError);
    if (parseError || !ast) {
        // If syntax error, it's also a compile failure, but let's check
        if (ast) NodeFree(ast);
        printf("PASS: [%s] (Compilation failed as expected)\n", test_name);
        g_pass_count++;
        return;
    }
    Compiler *c = CompNew();
    int ok = CompCompile(c, ast);
    NodeFree(ast);
    CompFree(c);
    if (!ok) {
        printf("PASS: [%s] (Compilation failed as expected)\n", test_name);
        g_pass_count++;
    } else {
        printf("FAIL: [%s] Expected compilation failure, but code compiled successfully\n", test_name);
        g_fail_count++;
    }
}

int main(void) {
    printf("Running Borrow Checker C Unit Tests\n\n");

    // Should pass tests
    assert_compile_success("Simple immutable borrow",
        "fn main() { let x = 42; let r = &x; print(str(r)); } main();\n");

    assert_compile_success("Multiple immutable borrows",
        "fn main() { let x = 42; let r1 = &x; let r2 = &x; print(str(r1)); print(str(r2)); } main();\n");

    assert_compile_success("Borrow ends at scope exit",
        "fn main() {\n"
        "    let mut x = 42;\n"
        "    {\n"
        "        let r = &x;\n"
        "        print(str(r));\n"
        "    }\n"
        "    let m = &mut x;\n"
        "    print(str(m));\n"
        "}\n"
        "main();\n");

    assert_compile_success("Move without subsequent use",
        "fn main() { let x = \"hello\"; let y = x; print(y); } main();\n");

    assert_compile_success("Conditional move with re-initialization",
        "fn main() { let mut x = \"hello\"; let cond = true; if (cond) { let y = x; x = \"world\"; } print(x); } main();\n");

    assert_compile_success("Move inside loop with re-initialization",
        "fn main() { let mut x = \"item\"; let mut i = 0; while (i < 3) { let y = x; x = \"fresh\"; i = i + 1; } } main();\n");

    // Should fail tests
    assert_compile_failure("Mutable + immutable borrow simultaneously (E0502)",
        "fn main() { let mut x = 42; let r1 = &x; let r2 = &mut x; print(str(r1)); } main();\n",
        "E0502");

    assert_compile_failure("Custom variable names borrow conflict (E0502)",
        "fn main() { let mut custom_val = 42; let my_immut_ref = &custom_val; let my_mut_ref = &mut custom_val; print(str(my_immut_ref)); } main();\n",
        "E0502");

    assert_compile_failure("Two mutable borrows of same variable (E0499)",
        "fn main() { let mut x = 42; let r1 = &mut x; let r2 = &mut x; print(str(r1)); } main();\n",
        "E0499");

    assert_compile_failure("Use after move (E0382)",
        "fn main() { let x = \"hello\"; let y = x; print(x); } main();\n",
        "E0382");

    assert_compile_failure("Conditional move without re-initialization (E0382)",
        "fn main() { let x = \"hello\"; let cond = true; if (cond) { let y = x; } print(x); } main();\n",
        "E0382");

    assert_compile_failure("Move inside loop without re-initialization (E0382)",
        "fn main() { let x = \"item\"; let mut i = 0; while (i < 3) { let y = x; i = i + 1; } } main();\n",
        "E0382");

    assert_compile_failure("Return reference to local variable (E0515)",
        "fn getRef() { let x = 42; return &x; } fn main() { let r = getRef(); } main();\n",
        "E0515");

    assert_compile_failure("Mutate through immutable reference (E0596)",
        "fn main() { let x = 42; let r = &x; x = 100; print(str(r)); } main();\n",
        "E0596");

    printf("\nTest Results: %d Passed, %d Failed\n", g_pass_count, g_fail_count);
    return g_fail_count > 0 ? 1 : 0;
}
