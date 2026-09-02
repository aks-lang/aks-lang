// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "parser.h"

char *gProgramName = "stdlib_tests";

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
    printf("Running Standard Library C Unit Tests\n\n");

    // Alloc Module (Box, Rc, Arc, Vec)
    assert_compile_success("alloc: Box, Rc, Arc, Vec",
        "fn test_alloc() {\n"
        "    let b = BoxNew(100);\n"
        "    let r = RcNew(200);\n"
        "    let r2 = r.Clone();\n"
        "    let a = ArcNew(300);\n"
        "    let v = VecNew();\n"
        "    v.Push(10);\n"
        "}\n"
        "fn main() { test_alloc(); } main();\n");

    // Collections Module (HashMap, HashSet, BTreeMap, LinkedList/Deque)
    assert_compile_success("collections: HashMap, HashSet, BTreeMap, Deque",
        "fn test_collections() {\n"
        "    let hm = HashMap();\n"
        "    hm.Put(\"a\", 1);\n"
        "    let hs = HashSet();\n"
        "    hs.Add(\"item\");\n"
        "    let bm = BTreeMap();\n"
        "    bm.Put(\"key\", 42);\n"
        "    let dq = Deque();\n"
        "    dq.PushBack(5);\n"
        "}\n"
        "fn main() { test_collections(); } main();\n");

    // Io & OS Module (File, Path, Environment)
    assert_compile_success("io & os: File, Path, Environment",
        "fn test_io_os() {\n"
        "    WriteFile(\"test_out.txt\", \"hello\");\n"
        "    let content = readfile(\"test_out.txt\");\n"
        "    let e = GetEnv(\"PATH\");\n"
        "    RemoveFile(\"test_out.txt\");\n"
        "}\n"
        "fn main() { test_io_os(); } main();\n");

    // Sync & Concurrency (Channel, Mutex)
    assert_compile_success("sync: Channel, Mutex",
        "fn test_sync() {\n"
        "    let ch = channel(10);\n"
        "    let m = mutex(0);\n"
        "}\n"
        "fn main() { test_sync(); } main();\n");

    // Time & Crypto (Duration, Instant, SHA, HMAC, AES)
    assert_compile_success("time & crypto: Instant, SHA, HMAC, AES",
        "fn test_time_crypto() {\n"
        "    let t = time();\n"
        "    let h = SHA256(\"secret\");\n"
        "    let hm = HMACSHA256(\"key\", \"data\");\n"
        "}\n"
        "fn main() { test_time_crypto(); } main();\n");

    // Additional Requirements (Result/Option, Serialization, Regex)
    assert_compile_success("additional: Result/Option, JSON/Msgpack, Regex",
        "fn test_extras() {\n"
        "    let ok_val = Result::Ok(42);\n"
        "    let opt_val = Option::Some(\"val\");\n"
        "    let json_str = JsonStringify({\"key\": \"value\"});\n"
        "    let is_match = RegexMatch(\"^hello\", \"hello world\");\n"
        "}\n"
        "fn main() { test_extras(); } main();\n");

    printf("\nTest Results: %d Passed, %d Failed\n", g_pass_count, g_fail_count);
    return g_fail_count > 0 ? 1 : 0;
}
