// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "parser.h"
#include "pkg.h"

char *gProgramName = "ecosystem_tests";

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
    printf("Running Ecosystem & Package Manager C Unit Tests\n\n");

    // & 5.2 Package Manager & Registry Initialization
    int res_init = PkgInit(".");
    if (res_init == 0) {
        printf("PASS: [aks-pkg init & manifest generation]\n");
        g_pass_count++;
    } else {
        printf("FAIL: [aks-pkg init]\n");
        g_fail_count++;
    }

    int res_add = PkgAdd("aks-http@1.0.0-alpha");
    if (res_add == 0) {
        printf("PASS: [aks-pkg add with semantic versioning]\n");
        g_pass_count++;
    } else {
        printf("FAIL: [aks-pkg add]\n");
        g_fail_count++;
    }

    // Security & Supply Chain (Keygen, Sign, Verify, SBOM)
    int res_key = PkgKeygen("test_sec_key");
    if (res_key == 0) {
        printf("PASS: [Package Security: Ed25519 Keygen]\n");
        g_pass_count++;
    } else {
        printf("FAIL: [Package Security: Keygen]\n");
        g_fail_count++;
    }

    int res_sbom = PkgExportSbom("test_sbom.json");
    if (res_sbom == 0) {
        printf("PASS: [Supply Chain: CycloneDX SBOM Export]\n");
        g_pass_count++;
    } else {
        printf("FAIL: [Supply Chain: SBOM Export]\n");
        g_fail_count++;
    }

    // Cleanup generated test files
    remove("aks.json");
    remove("test_sec_key.private.pem");
    remove("test_sec_key.public.pem");
    remove("test_sbom.json");

    // First-Party Libraries (aks-async, aks-http, aks-web, aks-db, aks-cli)
    assert_compile_success("aks-async & aks-http first-party library integration",
        "fn test_first_party() {\n"
        "    let res = http_get(\"http://localhost:8080/\");\n"
        "    let db = sqlite_open(\":memory:\");\n"
        "}\n"
        "fn main() { test_first_party(); } main();\n");

    printf("\nTest Results: %d Passed, %d Failed\n", g_pass_count, g_fail_count);
    return g_fail_count > 0 ? 1 : 0;
}
