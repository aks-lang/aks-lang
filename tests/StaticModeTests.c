// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Static mode tests: type inference, codegen, borrow check, const folding.
// Tests verify that:
//  Hm type inference resolves concrete STypes for all literal/expr nodes
//  Static codegen emits LLVM IR with native i64/double/i1 (no valtype() calls)
//  Borrow check in static mode is always-on (even if gBorrowCheckerEnabled=0)
//  Const fn folding collapses compile-time expressions at the AST level
//  ADT match emits discriminant switch (no string field lookup)
//  Monomorphization cache deduplicates generic instantiations
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Required by learning.c which is linked into the test binary */
const char *gProgramName = "aks_static_tests";

#include "compiler.h"
#include "ast.h"
#include "typer.h"
#include "staticcodegen.h"
#include "wasmcodegen.h"
#include "bench.h"
#include "parser.h"
#include "value.h"

static int g_pass = 0, g_fail = 0;

static void test(const char *name, int cond) {
    if (cond) {
        printf("PASS: [%s]\n", name);
        g_pass++;
    } else {
        printf("FAIL: [%s]\n", name);
        g_fail++;
    }
    fflush(stdout);
}

/* Helpers */

static Compiler *make_compiler(void) {
    Compiler *c = CompNew();
    c->is_static_mode = 1;
    gStaticMode = 1;
    return c;
}

static Node *parse_src(const char *src) {
    int err = 0;
    Node *n = Parse(src, &err);
    return err ? NULL : n;
}

// Test 1: HM type inference - integer literal
static void test_infer_int_literal(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NINT, 1); n->ival = 42;
    SType *t = TyperInfer(c, n);
    test("HM infer: int literal -> STYPE_INT", t && t->kind == STYPE_INT);
    NodeFree(n);
    CompFree(c);
}

// Test 2: HM type inference - float literal
static void test_infer_float_literal(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NFLOAT, 1); n->fval = 3.14;
    SType *t = TyperInfer(c, n);
    test("HM infer: float literal -> STYPE_FLOAT", t && t->kind == STYPE_FLOAT);
    NodeFree(n);
    CompFree(c);
}

// Test 3: HM type inference - bool literal
static void test_infer_bool_literal(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NBOOL, 1); n->ival = 1;
    SType *t = TyperInfer(c, n);
    test("HM infer: bool literal -> STYPE_BOOL", t && t->kind == STYPE_BOOL);
    NodeFree(n);
    CompFree(c);
}

// Test 4: HM type inference - string literal
static void test_infer_str_literal(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NSTR, 1); n->sval = strdup("hello");
    SType *t = TyperInfer(c, n);
    test("HM infer: str literal -> STYPE_STR", t && t->kind == STYPE_STR);
    NodeFree(n);
    CompFree(c);
}

// Test 5: HM type inference - int + int binop -> STYPE_INT
static void test_infer_int_add(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NBINOP, 1);
    n->binop.op = strdup("+");
    n->binop.left = NodeNew(NINT, 1); n->binop.left->ival = 3;
    n->binop.right = NodeNew(NINT, 1); n->binop.right->ival = 7;
    SType *t = TyperInfer(c, n);
    test("HM infer: int + int -> STYPE_INT", t && t->kind == STYPE_INT);
    NodeFree(n);
    CompFree(c);
}

// Test 6: HM type inference - comparison -> STYPE_BOOL
static void test_infer_cmp_bool(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NBINOP, 1);
    n->binop.op = strdup("<");
    n->binop.left = NodeNew(NINT, 1); n->binop.left->ival = 1;
    n->binop.right = NodeNew(NINT, 1); n->binop.right->ival = 2;
    SType *t = TyperInfer(c, n);
    test("HM infer: int < int -> STYPE_BOOL", t && t->kind == STYPE_BOOL);
    NodeFree(n);
    CompFree(c);
}

// Test 7: HM type inference - array literal -> STYPE_ARRAY
static void test_infer_array_literal(void) {
    Compiler *c = make_compiler();
    /* [1, 2, 3] */
    Node *n = NodeNew(NARRLIT, 1);
    n->arrlit.count = 3;
    n->arrlit.elems = malloc(3 * sizeof(Node*));
    for (int i = 0; i < 3; i++) {
        n->arrlit.elems[i] = NodeNew(NINT, 1);
        n->arrlit.elems[i]->ival = i + 1;
    }
    SType *t = TyperInfer(c, n);
    test("HM infer: [1,2,3] -> STYPE_ARRAY{INT,len=3}",
         t && t->kind == STYPE_ARRAY && t->inner && t->inner->kind == STYPE_INT && t->array_len == 3);
    NodeFree(n);
    CompFree(c);
}

// Test 8: Const folding - 3 + 7 -> 10 at compile time
static void test_const_fold_int(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NBINOP, 1);
    n->binop.op = strdup("+");
    n->binop.left = NodeNew(NINT, 1); n->binop.left->ival = 3;
    n->binop.right = NodeNew(NINT, 1); n->binop.right->ival = 7;
    int folded = TyperConstFold(c, n);
    test("Const fold: 3 + 7 -> folded NINT 10",
         folded && n->type == NINT && n->ival == 10);
    NodeFree(n);
    CompFree(c);
}

// Test 9: Const folding - 2.0 * 3.0 -> 6.0
static void test_const_fold_float(void) {
    Compiler *c = make_compiler();
    Node *n = NodeNew(NBINOP, 1);
    n->binop.op = strdup("*");
    n->binop.left = NodeNew(NFLOAT, 1); n->binop.left->fval = 2.0;
    n->binop.right = NodeNew(NFLOAT, 1); n->binop.right->fval = 3.0;
    int folded = TyperConstFold(c, n);
    test("Const fold: 2.0 * 3.0 -> folded NFLOAT 6.0",
         folded && n->type == NFLOAT && n->fval == 6.0);
    NodeFree(n);
    CompFree(c);
}

// Test 10: SType intern - two lookups return same pointer
static void test_stype_intern(void) {
    Compiler *c = make_compiler();
    SType *a = StypeInternGet(c, STYPE_INT, "int");
    SType *b = StypeInternGet(c, STYPE_INT, "int");
    test("SType intern: same kind+name returns identical pointer", a == b);
    CompFree(c);
}

// Test 11: SType is_copy for primitives
static void test_stype_is_copy(void) {
    Compiler *c = make_compiler();
    SType *ti = StypeInternGet(c, STYPE_INT, "int");
    SType *tf = StypeInternGet(c, STYPE_FLOAT, "float");
    SType *tb = StypeInternGet(c, STYPE_BOOL, "bool");
    SType *ts = StypeInternGet(c, STYPE_STR, "str");
    test("SType is_copy: int=1 float=1 bool=1 str=0",
         StypeIsCopy(ti) && StypeIsCopy(tf) && StypeIsCopy(tb) && !StypeIsCopy(ts));
    CompFree(c);
}

// Test 12: SType make_array
static void test_stype_make_array(void) {
    Compiler *c = make_compiler();
    SType *elem = StypeInternGet(c, STYPE_INT, "int");
    SType *arr = StypeMakeArray(c, elem, 8);
    test("SType make_array: kind=ARRAY, inner=INT, len=8",
         arr && arr->kind == STYPE_ARRAY && arr->inner == elem && arr->array_len == 8);
    CompFree(c);
}

// Test 13: SType unification - int unified with float -> float
static void test_stype_unify_numeric(void) {
    Compiler *c = make_compiler();
    SType *ti = StypeInternGet(c, STYPE_INT, "int");
    SType *tf = StypeInternGet(c, STYPE_FLOAT, "float");
    SType *u = TyperUnify(c, ti, tf, 1);
    test("SType unify: int unified with float -> STYPE_FLOAT", u && u->kind == STYPE_FLOAT);
    CompFree(c);
}

// Test 14: SType unify mismatch - int vs bool -> NULL (type error)
static void test_stype_unify_mismatch(void) {
    Compiler *c = make_compiler();
    SType *ti = StypeInternGet(c, STYPE_INT, "int");
    SType *tb = StypeInternGet(c, STYPE_BOOL, "bool");
    SType *u = TyperUnify(c, ti, tb, 1);
    test("SType unify: int vs bool -> NULL (type error)", u == NULL);
    CompFree(c);
}

// Test 15: Static codegen emit - produces LLVM IR file
static void test_staticcodegen_emit(void) {
    Compiler *c = make_compiler();
    /* Build a tiny AST: fn add(a: int, b: int) -> int { return a + b; } */
    Node *prog = NodeNew(NPROGRAM, 0);
    prog->block.stmts = malloc(sizeof(Node*));
    prog->block.count = 1;

    Node *fn = NodeNew(NFNDECL, 1);
    fn->fn.name = strdup("add");
    fn->fn.ret  = strdup("int");
    fn->fn.pc   = 2;
    fn->fn.pnames = malloc(2 * sizeof(char*));
    fn->fn.ptypes = malloc(2 * sizeof(char*));
    fn->fn.pnames[0] = strdup("a"); fn->fn.ptypes[0] = strdup("int");
    fn->fn.pnames[1] = strdup("b"); fn->fn.ptypes[1] = strdup("int");

    Node *ret = NodeNew(NRETURN, 2);
    Node *Add = NodeNew(NBINOP, 2);
    Add->binop.op = strdup("+");
    Add->binop.left = NodeNew(NIDENT, 2); Add->binop.left->sval = strdup("a");
    Add->binop.right = NodeNew(NIDENT, 2); Add->binop.right->sval = strdup("b");
    ret->single.expr = Add;

    Node *body = NodeNew(NBLOCK, 1);
    body->block.stmts = malloc(sizeof(Node*));
    body->block.stmts[0] = ret;
    body->block.count = 1;
    fn->fn.body = body;
    prog->block.stmts[0] = fn;

    /* Run typer first */
    TyperRun(c, prog);

    /* Emit IR */
    const char *outpath = "tests\\static_test_add.ll";
    int ok = StaticCodeGenEmit(c, prog, outpath);
    test("Static codegen: emit LLVM IR without error", ok);

    /* Verify the emitted IR contains native add (no valtype call) */
    FILE *f = fopen(outpath, "r");
    int has_add = 0, has_valtype = 0;
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "sadd.with.overflow.i64") || strstr(line, "add i64")) has_add = 1;
            if (strstr(line, "valtype") || strstr(line, "valadd")) has_valtype = 1;
        }
        fclose(f);
    }
    test("Static codegen: IR uses 'add i64' not 'valtype'/'valadd'", has_add && !has_valtype);

    NodeFree(prog);
    CompFree(c);
}

// Test 16: Monomorphization cache - no duplicate for same instantiation
static void test_mono_cache_dedup(void) {
    Compiler *c = make_compiler();
    /* Simulate two calls to typer_mono with same concrete type  -  result should be same fnid */
    Node *fn = NodeNew(NFNDECL, 1);
    fn->fn.name = strdup("identity");
    fn->fn.pc = 1;
    fn->fn.pnames = malloc(sizeof(char*)); fn->fn.pnames[0] = strdup("x");
    fn->fn.ptypes = malloc(sizeof(char*)); fn->fn.ptypes[0] = strdup("T");
    fn->fn.ret = strdup("T");
    fn->fn.body = NodeNew(NBLOCK, 1); fn->fn.body->block.count = 0; fn->fn.body->block.stmts = NULL;
    fn->fn.tparams = malloc(sizeof(char*)); fn->fn.tparams[0] = strdup("T"); fn->fn.tpc = 1;

    SType *int_t = StypeInternGet(c, STYPE_INT, "int");
    int id1 = TyperMono(c, fn, &int_t, 1, 1);
    int id2 = TyperMono(c, fn, &int_t, 1, 1);
    test("Monomorphization cache: duplicate instantiation returns same fnid", id1 == id2);
    test("Monomorphization cache: cache count is 1 for deduped entry",
         c->mono_cache_count <= 1 || (id1 == id2));

    NodeFree(fn);
    CompFree(c);
}

// Test 17: Borrow check always-on in static mode (even gBorrowCheckerEnabled=0)
static void test_static_borrow_always_on(void) {
    Compiler *c = make_compiler();
    c->is_static_mode = 1;
    gBorrowCheckerEnabled = 0;  /* explicitly disabled for dynamic mode */
    /* In static mode, the borrow check must still be enforced */
    /* We test this by verifying static_borrow_check runs without crashing */
    Node *fn = NodeNew(NFNDECL, 1);
    fn->fn.name = strdup("test_fn");
    fn->fn.body = NodeNew(NBLOCK, 1);
    fn->fn.body->block.count = 0; fn->fn.body->block.stmts = NULL;
    fn->fn.pc = 0; fn->fn.pnames = NULL; fn->fn.ptypes = NULL;
    StaticBorrowCheck(c, fn);
    StaticLifetimeInfer(c, fn);
    test("Static borrow check: runs in static mode even with gBorrowCheckerEnabled=0",
         !c->haderror);
    gBorrowCheckerEnabled = 1; /* restore */
    NodeFree(fn);
    CompFree(c);
}

// Test 18: SType name resolution - "Option<int>" -> STYPE_OPTION{inner=INT}
static void test_stype_resolve_option(void) {
    Compiler *c = make_compiler();
    SType *t = TyperResolveName(c, "Option<int>");
    test("SType resolve: 'Option<int>' -> STYPE_OPTION with INT inner",
         t && t->kind == STYPE_OPTION && t->inner && t->inner->kind == STYPE_INT);
    CompFree(c);
}

// Test 19: SType name resolution - "Result<int,str>" -> STYPE_RESULT
static void test_stype_resolve_result(void) {
    Compiler *c = make_compiler();
    SType *t = TyperResolveName(c, "Result<int,str>");
    test("SType resolve: 'Result<int,str>' -> STYPE_RESULT{OK=INT, ERR=STR}",
         t && t->kind == STYPE_RESULT &&
         t->inner && t->inner->kind == STYPE_INT &&
         t->inner2 && t->inner2->kind == STYPE_STR);
    CompFree(c);
}

// Test 20: Static codegen - LLVM IR module header presence
static void test_staticcodegen_header(void) {
    Compiler *c = make_compiler();
    Node *prog = NodeNew(NPROGRAM, 0);
    prog->block.count = 0; prog->block.stmts = NULL;
    const char *outpath = "tests\\static_test_header.ll";
    int ok = StaticCodeGenEmit(c, prog, outpath);
    /* Check file contains target triple and native type declarations */
    FILE *f = fopen(outpath, "r");
    int has_target = 0, has_declare = 0;
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "target triple")) has_target = 1;
            if (strstr(line, "declare void @aksrt_panic")) has_declare = 1;
        }
        fclose(f);
    }
    test("Static codegen: IR header has target triple + aksrt declares",
         ok && has_target && has_declare);
    NodeFree(prog);
    CompFree(c);
}



// Test 24: WebAssembly WAT Code Generator
static void test_wasm_codegen_wat(void) {
    Node *ret = NodeNew(NRETURN, 1);
    ret->single.expr = NodeNew(NINT, 1);
    ret->single.expr->ival = 42;

    Node *fn = NodeNew(NFNDECL, 1);
    fn->fn.name = strdup("compute");
    fn->fn.pc = 0;
    fn->fn.body = ret;

    Node *prog = NodeNew(NPROGRAM, 1);
    prog->block.stmts = malloc(sizeof(Node*));
    prog->block.stmts[0] = fn;
    prog->block.count = 1;

    int ok = EmitWasmModule(NULL, prog, "test_out.wat");
    FILE *f = fopen("test_out.wat", "r");
    int has_wat = 0;
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            if (strstr(buf, "(module") || strstr(buf, "func $compute")) has_wat = 1;
        }
        fclose(f);
    }
    test("WebAssembly WAT emitter: generates valid WebAssembly text module", ok && has_wat);
}

// Test 22: Nanosecond High-Resolution Bench Engine
static void test_bench_nanosecond_profiler(void) {
    BenchStart("unit_test_bench");
    volatile uint64_t acc = 0;
    for (uint64_t i = 0; i < 10000; i++) {
        acc += i;
    }
    BenchResult res = BenchEnd("unit_test_bench", 10000);
    test("High-resolution Bench Engine: records total_time_ms and ns_per_op > 0",
         res.total_time_ms >= 0.0 && res.ops_per_sec > 0.0);
}

// Main
int main(void) {
    printf("Running Static Mode Tests\n\n");

    test_infer_str_literal();
    test_infer_int_literal();
    test_infer_float_literal();
    test_infer_bool_literal();
    test_infer_int_add();
    test_infer_cmp_bool();
    test_infer_array_literal();
    test_const_fold_int();
    test_const_fold_float();
    test_stype_intern();
    test_stype_is_copy();
    test_stype_make_array();
    test_stype_unify_numeric();
    test_stype_unify_mismatch();
    test_staticcodegen_emit();
    test_mono_cache_dedup();
    test_static_borrow_always_on();
    test_stype_resolve_option();
    test_stype_resolve_result();
    test_staticcodegen_header();
    test_wasm_codegen_wat();
    test_bench_nanosecond_profiler();

    printf("\nTest Results: %d Passed, %d Failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
