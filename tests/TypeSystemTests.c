// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "parser.h"

char *gProgramName = "type_system_tests";

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

extern int gStrictTypes;

static void assert_compile_failure(const char *test_name, const char *source) {
    int parseError = 0;
    Node *ast = Parse(source, &parseError);
    if (parseError || !ast) {
        if (ast) NodeFree(ast);
        printf("PASS: [%s] (Compilation failed as expected)\n", test_name);
        g_pass_count++;
        return;
    }
    Compiler *c = CompNew();
    int oldstrict = gStrictTypes;
    gStrictTypes = 1;
    int ok = CompCompile(c, ast);
    gStrictTypes = oldstrict;
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
    printf("Running Type System C Unit Tests\n\n");

    // Trait System & Bounds
    assert_compile_success("Trait declaration and bounds",
        "interface Display { fn show(msg: string) -> string; }\n"
        "fn print_val<T: Display>(x: T) { print(x.show(\"hi\")); }\n"
        "fn main() {} main();\n");

    assert_compile_success("Multiple trait bounds and where clause",
        "interface Clone { fn clone(val: int) -> int; }\n"
        "interface Display { fn show(msg: string) -> string; }\n"
        "fn process<T>(x: T) where T: Clone + Display { print(\"ok\"); }\n"
        "fn main() {} main();\n");

    assert_compile_success("Trait objects dyn Trait",
        "interface Printable { fn to_str(val: int) -> string; }\n"
        "fn render(obj: dyn Printable) -> string { return obj.to_str(1); }\n"
        "fn main() {} main();\n");

    // Associated Types
    assert_compile_success("Associated type in interface/trait",
        "interface Container {\n"
        "    type Item;\n"
        "    fn get(val: int) -> string;\n"
        "};\n"
        "fn main() {} main();\n");

    // Pattern Matching & Exhaustiveness
    assert_compile_success("Exhaustive ADT pattern matching",
        "enum Option { Some(any), None };\n"
        "fn main() {\n"
        "    let opt = Option::Some(42);\n"
        "    match (opt) {\n"
        "        case Option.Some(x) if x > 0 => { print(x); }\n"
        "        case Option.Some(x) => { print(0); }\n"
        "        case Option.None => { print(0); }\n"
        "    }\n"
        "}\n"
        "main();\n");

    assert_compile_failure("Non-exhaustive match rejection",
        "enum Color { Red, Green, Blue };\n"
        "fn main() {\n"
        "    let c: Color = Color::Red;\n"
        "    match (c) {\n"
        "        case Color.Red => { print(1); }\n"
        "    }\n"
        "}\n"
        "main();\n");

    // Dependent Types (Array length bounds)
    assert_compile_success("Fixed-size dependent array type",
        "fn process_buf(buf: [int; 5]) -> int { return buf[0]; }\n"
        "fn main() {} main();\n");

    printf("\nTest Results: %d Passed, %d Failed\n", g_pass_count, g_fail_count);
    return g_fail_count > 0 ? 1 : 0;
}
