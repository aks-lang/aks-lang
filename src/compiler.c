// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "compiler.h"
#include "ast.h"
#include "opcode.h"
#include "parser.h"
#include "vm.h"
#include "pkg.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

int gNoBoundsCheck = 0;
int gStrictOvf = 0;
int gTypeCheckingEnabled = 1;
int gBorrowCheckerEnabled = 1;
int gImmutableByDefault = 0;
int gStrictTypes = 0;
/* static mode globals */
/* NOTE: gStaticMode and gDebugBuild are defined in src/staticcodegen.c
   (where they are first used) and declared extern here via compiler.h */


static void PreregisterFns(Compiler *c, Node *n);
static void PreregisterTypes(Compiler *c, Node *n);
static void MangleTypeName(const char *type, char *dest, size_t destsize);
static int TypesCompatible(Compiler *c, const char *expected, const char *actual);
static char *ReplAceTypeParam(const char *src, const char *param, const char *arg);
static void SubstituteTypes(char **typeptr, char **tparams, int tpc, char **targs, int tc);
static void EmitExpr(Compiler *c, Node *n);
static void EmitNode(Compiler *c, Node *n);
static void EmitRefCheck(Compiler *c, const char *typename, int line, Node *valnode);
static int ParseRefParts(const char *typename, char *basetype, char *varname, char *condstr);
static int AddLocal(Compiler *c, const char *name, int isconst);
static int ResolveLocal(Compiler *c, const char *name);
static int ResolveGlobal(Compiler *c, const char *name, int line);
static FnProto *NewFn(Compiler *c, const char *name);
static void Error(Compiler *c, int line, const char *msg);
static void CheckBorrows(Compiler *c, Node *n, Node *root);

int gContractCheck = 0;

static void CompileContractCheck(Compiler *c, Node *cond, const char *err_msg, int line) {
    Node *err_expr = NodeNew(NSTR, line);
    err_expr->sval = strdup(err_msg);
    Node *throw_node = NodeNew(NTHROW, line);
    throw_node->throwstmt.expr = err_expr;
    
    Node *not_cond = NodeNew(NUNOP, line);
    not_cond->binop.op = strdup("!");
    not_cond->binop.left = NodeCopy(cond);
    not_cond->binop.right = NULL;
    
    Node *then_block = NodeNew(NBLOCK, line);
    then_block->block.count = 1;
    then_block->block.stmts = malloc(sizeof(Node*));
    then_block->block.stmts[0] = throw_node;
    
    Node *if_node = NodeNew(NIF, line);
    if_node->ifn.cond = not_cond;
    if_node->ifn.then = then_block;
    if_node->ifn.els = NULL;
    
    EmitNode(c, if_node);
    
    NodeFree(if_node);
}

static char *ASTToSMT(Node *n) {
    if (!n) return strdup("");
    char buf[1024];
    switch (n->type) {
        case NINT:
            snprintf(buf, sizeof(buf), "%lld", (long long)n->ival);
            return strdup(buf);
        case NIDENT:
            return strdup(n->sval);
        case NUNOP: {
            char *sub = ASTToSMT(n->binop.left);
            if (strcmp(n->binop.op, "-") == 0) {
                snprintf(buf, sizeof(buf), "(- %s)", sub);
            } else if (strcmp(n->binop.op, "!") == 0) {
                snprintf(buf, sizeof(buf), "(not %s)", sub);
            } else {
                snprintf(buf, sizeof(buf), "%s", sub);
            }
            free(sub);
            return strdup(buf);
        }
        case NBINOP: {
            char *l = ASTToSMT(n->binop.left);
            char *r = ASTToSMT(n->binop.right);
            const char *op = n->binop.op;
            const char *smt_op = op;
            if (strcmp(op, "==") == 0) smt_op = "=";
            else if (strcmp(op, "!=") == 0) {
                snprintf(buf, sizeof(buf), "(not (= %s %s))", l, r);
                free(l); free(r);
                return strdup(buf);
            } else if (strcmp(op, "&&") == 0) smt_op = "and";
            else if (strcmp(op, "||") == 0) smt_op = "or";
            
            snprintf(buf, sizeof(buf), "(%s %s %s)", smt_op, l, r);
            free(l); free(r);
            return strdup(buf);
        }
        case NIF: {
            char *cond = ASTToSMT(n->ifn.cond);
            char *then = ASTToSMT(n->ifn.then);
            char *els = ASTToSMT(n->ifn.els);
            snprintf(buf, sizeof(buf), "(ite %s %s %s)", cond, then, els);
            free(cond); free(then); free(els);
            return strdup(buf);
        }
        case NBLOCK: {
            if (n->block.count == 2 && n->block.stmts[0]->type == NIF && !n->block.stmts[0]->ifn.els) {
                Node *ifnode = n->block.stmts[0];
                Node *retnode = n->block.stmts[1];
                if (ifnode->ifn.then->type == NBLOCK && ifnode->ifn.then->block.count == 1 &&
                    ifnode->ifn.then->block.stmts[0]->type == NRETURN) {
                    char *cond = ASTToSMT(ifnode->ifn.cond);
                    char *then = ASTToSMT(ifnode->ifn.then->block.stmts[0]->single.expr);
                    char *els = ASTToSMT(retnode->single.expr);
                    snprintf(buf, sizeof(buf), "(ite %s %s %s)", cond, then, els);
                    free(cond); free(then); free(els);
                    return strdup(buf);
                }
            }
            if (n->block.count > 0) {
                return ASTToSMT(n->block.stmts[n->block.count - 1]);
            }
            return strdup("0");
        }
        case NRETURN:
            if (n->single.expr) return ASTToSMT(n->single.expr);
            return strdup("0");
        default:
            return strdup("0");
    }
}

static void VerifyFnContracts(Compiler *c, Node *n) {
    if (n->fn.req_count == 0 && n->fn.ens_count == 0) return;
    
    char query[8192] = "";
    int len = 0;
    
    for (int i = 0; i < n->fn.pc; i++) {
        len += snprintf(query + len, sizeof(query) - len, "(declare-const %s Int)\n", n->fn.pnames[i]);
    }
    len += snprintf(query + len, sizeof(query) - len, "(declare-const result Int)\n");
    
    for (int i = 0; i < n->fn.req_count; i++) {
        char *smt_req = ASTToSMT(n->fn.requires[i]);
        if (smt_req) {
            len += snprintf(query + len, sizeof(query) - len, "(assert %s)\n", smt_req);
            free(smt_req);
        }
    }
    
    char *smt_body = ASTToSMT(n->fn.body);
    if (smt_body && strcmp(smt_body, "0") != 0) {
        len += snprintf(query + len, sizeof(query) - len, "(assert (= result %s))\n", smt_body);
        free(smt_body);
    }
    
    if (n->fn.ens_count > 0) {
        if (n->fn.ens_count == 1) {
            char *smt_ens = ASTToSMT(n->fn.ensures[0]);
            if (smt_ens) {
                len += snprintf(query + len, sizeof(query) - len, "(assert (not %s))\n", smt_ens);
                free(smt_ens);
            }
        } else {
            len += snprintf(query + len, sizeof(query) - len, "(assert (not (and");
            for (int i = 0; i < n->fn.ens_count; i++) {
                char *smt_ens = ASTToSMT(n->fn.ensures[i]);
                if (smt_ens) {
                    len += snprintf(query + len, sizeof(query) - len, " %s", smt_ens);
                    free(smt_ens);
                }
            }
            len += snprintf(query + len, sizeof(query) - len, ")))\n");
        }
    }
    
    len += snprintf(query + len, sizeof(query) - len, "(check-sat)\n");
    
    FILE *tmp = fopen("temp_query.smt2", "w");
    if (tmp) {
        fputs(query, tmp);
        fclose(tmp);
        
        FILE *z3_run = _popen("z3 temp_query.smt2 2>nul", "r");
        if (!z3_run) {
            z3_run = _popen("z3 temp_query.smt2", "r");
        }
        if (z3_run) {
            char response[128] = "";
            if (fgets(response, sizeof(response), z3_run)) {
                char *end = response + strlen(response) - 1;
                while (end >= response && (*end == '\r' || *end == '\n' || *end == ' ')) {
                    *end = '\0';
                    end--;
                }
                if (strcmp(response, "unsat") == 0) {
                    printf("[SMT Verifier] Function '%s' contract verified successfully by Z3.\n", n->fn.name);
                } else if (strcmp(response, "sat") == 0) {
                    printf("[SMT Verifier] Warning: Function '%s' contract verification failed (Z3 returned sat, counterexample found).\n", n->fn.name);
                } else {
                    printf("[SMT Verifier] Warning: Z3 returned: %s\n", response);
                }
            } else {
                // printf("[SMT Verifier] Warning: Z3 solver not found in PATH. Static contract verification skipped.\n");
            }
            _pclose(z3_run);
        } else {
            printf("[SMT Verifier] Warning: Z3 solver execution failed. Static contract verification skipped.\n");
        }
        remove("temp_query.smt2");
    }
}

static void ChkEffects(Compiler *c, Node *n, const char *eff) {
    if (!n) return;
    switch (n->type) {
        case NCALL: {
            if (n->call.callee && n->call.callee->type == NIDENT && n->call.callee->sval) {
                int fid = ResolveGlobal(c, n->call.callee->sval, n->line);
                if (fid >= 0 && fid < c->fncount && c->fns[fid] && c->fns[fid]->ast) {
                    Node *decl = c->fns[fid]->ast;
                    if (decl && decl->type == NFNDECL && decl->fn.effect) {
                        if (!eff || strcmp(eff, decl->fn.effect) != 0) {
                            char buf[256];
                            snprintf(buf, sizeof(buf), "EffectError: call to effectful function '%s' requires effect '%s'", n->call.callee->sval, decl->fn.effect);
                            Error(c, n->line, buf);
                        }
                    }
                }
            }
            for (int i = 0; i < n->call.argc; i++) {
                ChkEffects(c, n->call.args[i], eff);
            }
            break;
        }
        case NBLOCK:
        case NPROGRAM: {
            for (int i = 0; i < n->block.count; i++) {
                ChkEffects(c, n->block.stmts[i], eff);
            }
            break;
        }
        case NIF: {
            ChkEffects(c, n->ifn.cond, eff);
            ChkEffects(c, n->ifn.then, eff);
            ChkEffects(c, n->ifn.els, eff);
            break;
        }
        case NWHILE: {
            ChkEffects(c, n->whilen.cond, eff);
            ChkEffects(c, n->whilen.body, eff);
            break;
        }
        case NFOR: {
            ChkEffects(c, n->forn.init, eff);
            ChkEffects(c, n->forn.cond, eff);
            ChkEffects(c, n->forn.post, eff);
            ChkEffects(c, n->forn.body, eff);
            break;
        }
        case NRETURN:
        case NEXPRSTMT:
        case NPRINT:
        case NTHROW:
        case NYIELD:
        case NAWAIT:
        case NSPAWN:
        case NTRYPROPAGATE: {
            ChkEffects(c, n->single.expr, eff);
            break;
        }
        case NTRYCATCH: {
            ChkEffects(c, n->trycatch.tryblock, eff);
            ChkEffects(c, n->trycatch.catchblock, eff);
            ChkEffects(c, n->trycatch.finallyblock, eff);
            break;
        }
        case NSWITCH: {
            ChkEffects(c, n->sw.expr, eff);
            for (int i = 0; i < n->sw.casecount; i++) {
                ChkEffects(c, n->sw.casevals[i], eff);
                ChkEffects(c, n->sw.casebodies[i], eff);
            }
            ChkEffects(c, n->sw.defaultbody, eff);
            break;
        }
        case NMATCH: {
            ChkEffects(c, n->match.expr, eff);
            for (int i = 0; i < n->match.count; i++) {
                ChkEffects(c, n->match.patterns[i], eff);
                ChkEffects(c, n->match.bodies[i], eff);
                if (n->match.guards) ChkEffects(c, n->match.guards[i], eff);
            }
            break;
        }
        case NTUPLE: {
            for (int i = 0; i < n->tuple.count; i++) {
                ChkEffects(c, n->tuple.elems[i], eff);
            }
            break;
        }
        case NARRLIT: {
            for (int i = 0; i < n->arrlit.count; i++) {
                ChkEffects(c, n->arrlit.elems[i], eff);
            }
            break;
        }
        case NMAPLIT: {
            for (int i = 0; i < n->maplit.count; i++) {
                ChkEffects(c, n->maplit.vals[i], eff);
            }
            break;
        }
        case NMEMBER: {
            ChkEffects(c, n->member.obj, eff);
            break;
        }
        case NINDEX: {
            ChkEffects(c, n->index.arr, eff);
            ChkEffects(c, n->index.idx, eff);
            break;
        }
        case NASSIGN: {
            ChkEffects(c, n->assign.target, eff);
            ChkEffects(c, n->assign.value, eff);
            break;
        }
        case NBINOP: {
            ChkEffects(c, n->binop.left, eff);
            ChkEffects(c, n->binop.right, eff);
            break;
        }
        case NUNOP: {
            ChkEffects(c, n->binop.left, eff);
            break;
        }
        case NTERNARY: {
            ChkEffects(c, n->ternary.cond, eff);
            ChkEffects(c, n->ternary.tval, eff);
            ChkEffects(c, n->ternary.fval, eff);
            break;
        }
        case NVARDECL:
        case NCONSTDECL: {
            ChkEffects(c, n->decl.init, eff);
            break;
        }
        default: break;
    }
}

static Node *TransformGenBody(Node *n) {
    if (!n) return NULL;
    switch (n->type) {
        case NYIELD: {
            Node *call = NodeNew(NCALL, n->line);
            call->call.callee = NodeNew(NIDENT, n->line);
            call->call.callee->sval = strdup("ChannelSend");
            call->call.argc = 2;
            call->call.args = malloc(2 * sizeof(Node*));
            call->call.isspread = calloc(2, sizeof(int));
            call->call.tc = 0;
            call->call.targs = NULL;
            
            Node *chanident = NodeNew(NIDENT, n->line);
            chanident->sval = strdup("__chan");
            call->call.args[0] = chanident;
            call->call.args[1] = TransformGenBody(n->yieldstmt.expr);
            
            n->yieldstmt.expr = NULL;
            NodeFree(n);
            return call;
        }
        case NRETURN: {
            Node *block = NodeNew(NBLOCK, n->line);
            block->block.count = 2;
            block->block.stmts = malloc(2 * sizeof(Node*));
            
            Node *closecall = NodeNew(NCALL, n->line);
            closecall->call.callee = NodeNew(NIDENT, n->line);
            closecall->call.callee->sval = strdup("ChannelClose");
            closecall->call.argc = 1;
            closecall->call.args = malloc(sizeof(Node*));
            closecall->call.isspread = calloc(1, sizeof(int));
            closecall->call.tc = 0;
            closecall->call.targs = NULL;
            Node *chanident = NodeNew(NIDENT, n->line);
            chanident->sval = strdup("__chan");
            closecall->call.args[0] = chanident;
            
            block->block.stmts[0] = closecall;
            
            Node *ret = NodeNew(NRETURN, n->line);
            ret->single.expr = NULL;
            block->block.stmts[1] = ret;
            
            if (n->single.expr) {
                NodeFree(n->single.expr);
            }
            free(n);
            return block;
        }
        case NBLOCK:
        case NPROGRAM: {
            for (int i = 0; i < n->block.count; i++) {
                n->block.stmts[i] = TransformGenBody(n->block.stmts[i]);
            }
            break;
        }
        case NIF: {
            n->ifn.cond = TransformGenBody(n->ifn.cond);
            n->ifn.then = TransformGenBody(n->ifn.then);
            n->ifn.els = TransformGenBody(n->ifn.els);
            break;
        }
        case NTERNARY: {
            n->ternary.cond = TransformGenBody(n->ternary.cond);
            n->ternary.tval = TransformGenBody(n->ternary.tval);
            n->ternary.fval = TransformGenBody(n->ternary.fval);
            break;
        }
        case NWHILE: {
            n->whilen.cond = TransformGenBody(n->whilen.cond);
            n->whilen.body = TransformGenBody(n->whilen.body);
            break;
        }
        case NFOR: {
            n->forn.init = TransformGenBody(n->forn.init);
            n->forn.cond = TransformGenBody(n->forn.cond);
            n->forn.post = TransformGenBody(n->forn.post);
            n->forn.body = TransformGenBody(n->forn.body);
            break;
        }
        case NFORIN: {
            n->forin.iter = TransformGenBody(n->forin.iter);
            n->forin.body = TransformGenBody(n->forin.body);
            break;
        }
        case NTRYCATCH: {
            n->trycatch.tryblock = TransformGenBody(n->trycatch.tryblock);
            n->trycatch.catchblock = TransformGenBody(n->trycatch.catchblock);
            n->trycatch.finallyblock = TransformGenBody(n->trycatch.finallyblock);
            break;
        }
        case NSWITCH: {
            n->sw.expr = TransformGenBody(n->sw.expr);
            for (int i = 0; i < n->sw.casecount; i++) {
                n->sw.casevals[i] = TransformGenBody(n->sw.casevals[i]);
                n->sw.casebodies[i] = TransformGenBody(n->sw.casebodies[i]);
            }
            n->sw.defaultbody = TransformGenBody(n->sw.defaultbody);
            break;
        }
        case NASSIGN: {
            n->assign.target = TransformGenBody(n->assign.target);
            n->assign.value = TransformGenBody(n->assign.value);
            break;
        }
        case NCALL: {
            n->call.callee = TransformGenBody(n->call.callee);
            for (int i = 0; i < n->call.argc; i++) {
                n->call.args[i] = TransformGenBody(n->call.args[i]);
            }
            break;
        }
        case NINDEX: {
            n->index.arr = TransformGenBody(n->index.arr);
            n->index.idx = TransformGenBody(n->index.idx);
            break;
        }
        case NMEMBER: {
            n->member.obj = TransformGenBody(n->member.obj);
            break;
        }
        case NARRLIT: {
            for (int i = 0; i < n->arrlit.count; i++) {
                n->arrlit.elems[i] = TransformGenBody(n->arrlit.elems[i]);
            }
            break;
        }
        case NMAPLIT: {
            for (int i = 0; i < n->maplit.count; i++) {
                n->maplit.vals[i] = TransformGenBody(n->maplit.vals[i]);
            }
            break;
        }
        case NCOALESCE: {
            n->coalesce.left = TransformGenBody(n->coalesce.left);
            n->coalesce.right = TransformGenBody(n->coalesce.right);
            break;
        }
        case NOPTCHAIN: {
            n->optchain.obj = TransformGenBody(n->optchain.obj);
            if (n->optchain.idx) {
                n->optchain.idx = TransformGenBody(n->optchain.idx);
            }
            break;
        }
        case NSTRUCTINIT: {
            if (n->structinit.spread) {
                n->structinit.spread = TransformGenBody(n->structinit.spread);
            }
            for (int i = 0; i < n->structinit.fc; i++) {
                n->structinit.fvals[i] = TransformGenBody(n->structinit.fvals[i]);
            }
            break;
        }
        case NCHAINEDCMP: {
            for (int i = 0; i < n->chainedcmp.count + 1; i++) {
                n->chainedcmp.exprs[i] = TransformGenBody(n->chainedcmp.exprs[i]);
            }
            break;
        }
        case NRANGE: {
            n->range.start = TransformGenBody(n->range.start);
            n->range.end = TransformGenBody(n->range.end);
            break;
        }
        case NSPREAD: {
            n->spread.expr = TransformGenBody(n->spread.expr);
            break;
        }
        case NEXPRSTMT: {
            n->single.expr = TransformGenBody(n->single.expr);
            break;
        }
        default: break;
    }
    return n;
}

static void DesugArgEn(Compiler *c, Node *n) {
    char helpername[256];
    snprintf(helpername, sizeof(helpername), "__gen_helper_%s", n->fn.name);
    
    Node *helperdecl = NodeNew(NFNDECL, n->line);
    helperdecl->fn.name = strdup(helpername);
    helperdecl->fn.isgen = 0;
    helperdecl->fn.isasync = 0;
    if (n->fn.ret) helperdecl->fn.ret = strdup(n->fn.ret);
    else helperdecl->fn.ret = NULL;
    
    helperdecl->fn.pc = n->fn.pc + 1;
    helperdecl->fn.pnames = malloc((n->fn.pc + 1) * sizeof(char*));
    helperdecl->fn.ptypes = malloc((n->fn.pc + 1) * sizeof(char*));
    helperdecl->fn.pdefaults = calloc(n->fn.pc + 1, sizeof(Node*));
    
    helperdecl->fn.pnames[0] = strdup("__chan");
    helperdecl->fn.ptypes[0] = strdup("any");
    for (int i = 0; i < n->fn.pc; i++) {
        helperdecl->fn.pnames[i + 1] = strdup(n->fn.pnames[i]);
        if (n->fn.ptypes[i]) helperdecl->fn.ptypes[i + 1] = strdup(n->fn.ptypes[i]);
        else helperdecl->fn.ptypes[i + 1] = strdup("any");
    }
    
    Node *helperbody = TransformGenBody(NodeCopy(n->fn.body));
    if (helperbody && helperbody->type == NBLOCK) {
        helperbody->block.count++;
        helperbody->block.stmts = realloc(helperbody->block.stmts, helperbody->block.count * sizeof(Node*));
        
        Node *closecall = NodeNew(NCALL, n->line);
        closecall->call.callee = NodeNew(NIDENT, n->line);
        closecall->call.callee->sval = strdup("ChannelClose");
        closecall->call.argc = 1;
        closecall->call.args = malloc(sizeof(Node*));
        closecall->call.isspread = calloc(1, sizeof(int));
        closecall->call.tc = 0;
        closecall->call.targs = NULL;
        Node *chanident = NodeNew(NIDENT, n->line);
        chanident->sval = strdup("__chan");
        closecall->call.args[0] = chanident;
        
        helperbody->block.stmts[helperbody->block.count - 1] = closecall;
    }
    helperdecl->fn.body = helperbody;
    
    if (ResolveGlobal(c, helpername, 0) < 0) {
        NewFn(c, helpername);
    }
    EmitNode(c, helperdecl);
    int helper_fid = ResolveGlobal(c, helpername, 0);
    if (helper_fid >= 0) {
        c->fns[helper_fid]->ast = NULL;
    }
    NodeFree(helperdecl);
    
    NodeFree(n->fn.body);
    n->fn.isgen = 0;
    
    Node *wrapperbody = NodeNew(NBLOCK, n->line);
    wrapperbody->block.count = 3;
    wrapperbody->block.stmts = malloc(3 * sizeof(Node*));
    
    Node *chandecl = NodeNew(NVARDECL, n->line);
    chandecl->decl.name = strdup("__chan");
    chandecl->decl.tname = strdup("any");
    chandecl->decl.isconstflag = 0;
    Node *cccall = NodeNew(NCALL, n->line);
    cccall->call.callee = NodeNew(NIDENT, n->line);
    cccall->call.callee->sval = strdup("Channel");
    cccall->call.argc = 1;
    cccall->call.args = malloc(sizeof(Node*));
    cccall->call.args[0] = NodeNew(NINT, n->line);
    cccall->call.args[0]->ival = 1;
    cccall->call.isspread = calloc(1, sizeof(int));
    cccall->call.tc = 0;
    cccall->call.targs = NULL;
    chandecl->decl.init = cccall;
    wrapperbody->block.stmts[0] = chandecl;
    
    Node *spawncall = NodeNew(NCALL, n->line);
    spawncall->call.callee = NodeNew(NIDENT, n->line);
    spawncall->call.callee->sval = strdup(helpername);
    spawncall->call.argc = n->fn.pc + 1;
    spawncall->call.args = malloc(spawncall->call.argc * sizeof(Node*));
    spawncall->call.isspread = calloc(spawncall->call.argc, sizeof(int));
    spawncall->call.tc = 0;
    spawncall->call.targs = NULL;
    
    Node *chanident = NodeNew(NIDENT, n->line);
    chanident->sval = strdup("__chan");
    spawncall->call.args[0] = chanident;
    for (int i = 0; i < n->fn.pc; i++) {
        Node *paramident = NodeNew(NIDENT, n->line);
        paramident->sval = strdup(n->fn.pnames[i]);
        spawncall->call.args[i + 1] = paramident;
    }
    
    Node *spawnstmt = NodeNew(NSPAWN, n->line);
    spawnstmt->spawnn.expr = spawncall;
    wrapperbody->block.stmts[1] = spawnstmt;
    
    Node *retstmt = NodeNew(NRETURN, n->line);
    Node *maplit = NodeNew(NMAPLIT, n->line);
    maplit->maplit.count = 2;
    maplit->maplit.keys = malloc(2 * sizeof(char*));
    maplit->maplit.keys[0] = strdup("chan");
    maplit->maplit.keys[1] = strdup("methodmissing");
    maplit->maplit.vals = malloc(2 * sizeof(Node*));
    
    Node *chanident2 = NodeNew(NIDENT, n->line);
    chanident2->sval = strdup("__chan");
    maplit->maplit.vals[0] = chanident2;
    
    Node *nextident = NodeNew(NIDENT, n->line);
    nextident->sval = strdup("__GenNext");
    maplit->maplit.vals[1] = nextident;
    
    retstmt->single.expr = maplit;
    wrapperbody->block.stmts[2] = retstmt;
    
    n->fn.body = wrapperbody;
}

static void EmitDestructureStore(Compiler *c, Node *target, int line) {
    Chunk *ch = &c->current->chunk;
    if (target->type == NIDENT) {
        int slot = ResolveLocal(c, target->sval);
        if (slot >= 0) {
            if (c->current->locals[slot].isconst) {
                char buf[256];
                snprintf(buf, sizeof(buf), "ConstError: cannot assign to immutable variable '%s'", target->sval);
                Error(c, line, buf);
            }
            c->current->locals[slot].ismoved = 0;
            ChunkWrite(ch, OPDUP, line);
            ChunkWrite(ch, OPSTORE, line);
            ChunkWriteU16(ch, (uint16_t)slot, line);
            ChunkWrite(ch, OPPOP, line);
        } else {
            ChunkWrite(ch, OPDUP, line);
            ChunkWrite(ch, OPSTOREGLOBAL, line);
            int gsid = AddStr(c, target->sval);
            ChunkWriteU16(ch, (uint16_t)gsid, line);
            ChunkWrite(ch, OPPOP, line);
        }
    } else if (target->type == NINDEX) {
        EmitExpr(c, target->index.arr);
        EmitExpr(c, target->index.idx);
        ChunkWrite(ch, OPARRSET, line);
        ChunkWrite(ch, OPPOP, line);
    } else if (target->type == NMEMBER) {
        EmitExpr(c, target->member.obj);
        int fid = AddStr(c, target->member.field);
        ChunkWrite(ch, OPSETFIELD, line);
        ChunkWriteU16(ch, (uint16_t)fid, line);
        ChunkWrite(ch, OPPOP, line);
    } else if (target->type == NARRLIT) {
        char tmpvar[64];
        snprintf(tmpvar, sizeof(tmpvar), "destructtmp%d", c->current->localcount);
        int tmpslot = AddLocal(c, tmpvar, 0);
        strcpy(c->current->locals[tmpslot].typename, "any");
        ChunkWrite(ch, OPSTORE, line);
        ChunkWriteU16(ch, (uint16_t)tmpslot, line);
        for (int i = 0; i < target->arrlit.count; i++) {
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, line);
            ChunkWrite(ch, OPPUSHINT, line);
            ChunkWriteI64(ch, i, line);
            ChunkWrite(ch, OPARRGET, line);
            EmitDestructureStore(c, target->arrlit.elems[i], line);
        }
    } else if (target->type == NMAPLIT) {
        char tmpvar[64];
        snprintf(tmpvar, sizeof(tmpvar), "destructtmp%d", c->current->localcount);
        int tmpslot = AddLocal(c, tmpvar, 0);
        strcpy(c->current->locals[tmpslot].typename, "any");
        ChunkWrite(ch, OPSTORE, line);
        ChunkWriteU16(ch, (uint16_t)tmpslot, line);
        for (int i = 0; i < target->maplit.count; i++) {
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, line);
            int fid = AddStr(c, target->maplit.keys[i]);
            ChunkWrite(ch, OPGETFIELD, line);
            ChunkWriteU16(ch, (uint16_t)fid, line);
            EmitDestructureStore(c, target->maplit.vals[i], line);
        }
    }
}

static Node *ValToNode(AKSValue val, int line) {
    Node *n = NULL;
    switch (ValKind(val)) {
        case VALINT:
            n = NodeNew(NINT, line);
            n->ival = ValToInt(val);
            break;
        case VALFLOAT:
            n = NodeNew(NFLOAT, line);
            n->fval = ValToFloat(val);
            break;
        case VALBOOL:
            n = NodeNew(NBOOL, line);
            n->ival = ValToBool(val);
            break;
        case VALNULL:
            n = NodeNew(NNULL, line);
            break;
        case VALSTR:
            n = NodeNew(NSTR, line);
            n->sval = strdup(ValToStrPtr(val)->data ? ValToStrPtr(val)->data : "");
            break;
        case VALARR: {
            n = NodeNew(NARRLIT, line);
            n->arrlit.count = ValToArr(val)->count;
            n->arrlit.elems = malloc(ValToArr(val)->count * sizeof(Node*));
            n->arrlit.isspread = calloc(ValToArr(val)->count, sizeof(int));
            for (int i = 0; i < ValToArr(val)->count; i++) {
                n->arrlit.elems[i] = ValToNode(ValToArr(val)->items[i], line);
            }
            break;
        }
        case VALSTRUCT: {
            n = NodeNew(NMAPLIT, line);
            n->maplit.count = ValToObj(val)->fieldcount;
            n->maplit.keys = malloc(ValToObj(val)->fieldcount * sizeof(char*));
            n->maplit.vals = malloc(ValToObj(val)->fieldcount * sizeof(Node*));
            for (int i = 0; i < ValToObj(val)->fieldcount; i++) {
                n->maplit.keys[i] = strdup(ValToObj(val)->fieldnames[i]);
                n->maplit.vals[i] = ValToNode(ValToObj(val)->fieldvals[i], line);
            }
            break;
        }
        default:
            n = NodeNew(NNULL, line);
            break;
    }
    return n;
}

static char *CompReadFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *buffer = malloc(size + 1);
    if (!buffer) { fclose(file); return NULL; }
    size_t readbytes = fread(buffer, 1, size, file);
    buffer[readbytes] = '\0';
    fclose(file);
    return buffer;
}

// Aks bytecode compiler
// walk AST and spit out bytecodes

// chunk helpers
void ChunkWrite(Chunk *ch, uint8_t byte, int line) {
    if(ch->count==ch->cap){
        ch->cap=ch->cap?ch->cap*2:64;
        ch->code=realloc(ch->code,ch->cap);
        ch->lines=realloc(ch->lines,ch->cap*sizeof(int));
    }
    ch->lines[ch->count]=line;
    ch->code[ch->count++]=byte;
}

void ChunkWriteU16(Chunk *ch, uint16_t v, int line) {
    ChunkWrite(ch,(uint8_t)(v>>8),line);
    ChunkWrite(ch,(uint8_t)(v&0xFF),line);
}

void ChunkWriteI32(Chunk *ch, int32_t v, int line) {
    ChunkWrite(ch,(uint8_t)((v>>24)&0xFF),line);
    ChunkWrite(ch,(uint8_t)((v>>16)&0xFF),line);
    ChunkWrite(ch,(uint8_t)((v>>8)&0xFF),line);
    ChunkWrite(ch,(uint8_t)(v&0xFF),line);
}

void ChunkWriteI64(Chunk *ch, int64_t v, int line) {
    for(int i=56;i>=0;i-=8)
        ChunkWrite(ch,(uint8_t)((v>>i)&0xFF),line);
}

void ChunkWriteF64(Chunk *ch, double v, int line) {
    uint64_t bits; memcpy(&bits,&v,8);
    for(int i=56;i>=0;i-=8)
        ChunkWrite(ch,(uint8_t)((bits>>i)&0xFF),line);
}

int ChunkEmitJump(Chunk *ch, uint8_t op, int line) {
    ChunkWrite(ch,op,line);
    // placeholder 4-byte offset
    int offset=ch->count;
    ChunkWriteI32(ch,0,line);
    return offset;
}

void ChunkPatchJump(Chunk *ch, int offset) {
    int32_t jump=(int32_t)(ch->count - offset - 4);
    ch->code[offset  ]=(uint8_t)((jump>>24)&0xFF);
    ch->code[offset+1]=(uint8_t)((jump>>16)&0xFF);
    ch->code[offset+2]=(uint8_t)((jump>>8)&0xFF);
    ch->code[offset+3]=(uint8_t)(jump&0xFF);
}

// compiler helpers
static void Error(Compiler *c, int line, const char *msg) {
    if (c->trydepth > 0) {
        if (!c->is_type_checking_pass && c->current) {
            Chunk *ch = &c->current->chunk;
            int id = AddStr(c, msg);
            ChunkWrite(ch, OPPUSHSTR, line);
            ChunkWriteU16(ch, (uint16_t)id, line);
            ChunkWrite(ch, OPTHROW, line);
        }
        return;
    }
    fprintf(stderr,"Compiler error on line %d: %s\n",line,msg);
    c->haderror=1;
}

static uint32_t StrPoolHash(const char *str) {
    uint32_t hash = 0x811c9dc5;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 0x01000193;
    }
    return hash;
}

int AddStr(Compiler *c, const char *s) {
    if (!s) s = "";
    uint32_t h = StrPoolHash(s) % STRPOOL_BUCKETS;
    for (StrPoolEntry *e = c->str_hash[h]; e; e = e->next) {
        if (strcmp(e->str, s) == 0) {
            return e->idx;
        }
    }
    if (c->strcount == c->strcap) {
        c->strcap = c->strcap ? c->strcap * 2 : 16;
        c->strpool = realloc(c->strpool, c->strcap * sizeof(char*));
    }
    int idx = c->strcount++;
    c->strpool[idx] = strdup(s);

    StrPoolEntry *e = malloc(sizeof(StrPoolEntry));
    e->str = c->strpool[idx];
    e->idx = idx;
    e->next = c->str_hash[h];
    c->str_hash[h] = e;

    return idx;
}

static int AddLocal(Compiler *c, const char *name, int isconst) {
    FnProto *fn=c->current;
    if(fn->localcount>=MAXLOCALS){ Error(c,0,"Too many locals"); return -1; }
    Local *l=&fn->locals[fn->localcount];
    strncpy(l->name,name,127);
    l->slot=fn->localcount;
    l->depth=fn->scopedepth;
    l->isconst=isconst;
    return fn->localcount++;
}

static int ResolveLocal(Compiler *c, const char *name) {
    FnProto *fn=c->current;
    if (!fn) return -1;
    for(int i=fn->localcount-1;i>=0;i--)
        if(!strcmp(fn->locals[i].name,name)) return fn->locals[i].slot;
    return -1;
}

static char *ReplAceTypeParam(const char *src, const char *param, const char *arg) {
    size_t paramlen = strlen(param);
    size_t arglen = strlen(arg);
    size_t srclen = strlen(src);
    size_t cap = srclen * 2 + arglen + 16;
    char *dest = malloc(cap);
    size_t d = 0;
    
    size_t i = 0;
    while (i < srclen) {
        if (strncmp(src + i, param, paramlen) == 0) {
            int leftboundary = (i == 0 || (!isalnum((unsigned char)src[i-1]) && src[i-1] != '_'));
            int rightboundary = (src[i + paramlen] == '\0' || (!isalnum((unsigned char)src[i + paramlen]) && src[i + paramlen] != '_'));
            if (leftboundary && rightboundary) {
                if (d + arglen >= cap) {
                    cap = cap * 2 + arglen;
                    dest = realloc(dest, cap);
                }
                memcpy(dest + d, arg, arglen);
                d += arglen;
                i += paramlen;
                continue;
            }
        }
        if (d + 1 >= cap) {
            cap *= 2;
            dest = realloc(dest, cap);
        }
        dest[d++] = src[i++];
    }
    dest[d] = '\0';
    return dest;
}

static void SubstituteTypes(char **typeptr, char **tparams, int tpc, char **targs, int tc) {
    if (!typeptr || !*typeptr) return;
    for (int k = 0; k < tpc && k < tc; k++) {
        char *replaced = ReplAceTypeParam(*typeptr, tparams[k], targs[k]);
        free(*typeptr);
        *typeptr = replaced;
    }
}

/* Module System Helpers */

/* Register a symbol with visibility info */
static void RegisterSymbol(Compiler *c, const char *name, int visibility,
                            const char *source_file, const char *ns) {
    if (c->mod_symbol_count >= 4096) return;
    CompilerSymbol *sym = &c->mod_symbols[c->mod_symbol_count++];
    strncpy(sym->name, name, 255); sym->name[255] = '\0';
    sym->visibility = visibility;
    strncpy(sym->source_file, source_file ? source_file : "", 511);
    sym->source_file[511] = '\0';
    strncpy(sym->namespace_, ns ? ns : "", 255);
    sym->namespace_[255] = '\0';
}

/* Check if a symbol is visible from current context; emit error if not */
static void CheckSymbolVisibility(Compiler *c, const char *name, int line) {
    if (!name) return;
    for (int i = 0; i < c->mod_symbol_count; i++) {
        CompilerSymbol *sym = &c->mod_symbols[i];
        if (strcmp(sym->name, name) != 0) continue;
        if (sym->visibility == VIS_PUB || sym->visibility == VIS_DEFAULT) return;
        if (sym->visibility == VIS_PRIVATE) {
            /* Private: caller must be in the same namespace */
            if (strcmp(c->current_namespace, sym->namespace_) != 0) {
                char errbuf[512];
                snprintf(errbuf, sizeof(errbuf),
                         "VisibilityError: '%s' is private to module '%s'",
                         name, sym->namespace_[0] ? sym->namespace_ : "(top-level)");
                Error(c, line, errbuf);
            }
            return;
        }
        if (sym->visibility == VIS_INTERNAL) {
            /* Internal: caller must be in the same file */
            if (strcmp(c->currentfilepath, sym->source_file) != 0) {
                char errbuf[512];
                snprintf(errbuf, sizeof(errbuf),
                         "VisibilityError: '%s' is internal to file '%s'",
                         name, sym->source_file);
                Error(c, line, errbuf);
            }
            return;
        }
    }
}

/* Build a mangled name: "namespace::name" or "name" if no namespace */
static char *MangleName(Compiler *c, const char *name) {
    if (c->current_namespace[0] == '\0') return NULL; /* no mangling */
    int len = (int)(strlen(c->current_namespace) + 2 + strlen(name) + 1);
    char *mangled = malloc(len);
    snprintf(mangled, len, "%s::%s", c->current_namespace, name);
    return mangled;
}

/* Register a use-alias */
static void RegisterUseAlias(Compiler *c, const char *alias, const char *target,
                              int visibility, const char *source_file) {
    for (int i = 0; i < c->use_alias_count; i++) {
        if (strcmp(c->use_aliases[i].name, alias) == 0 && strcmp(c->use_aliases[i].target, target) == 0) {
            return;
        }
    }
    if (c->use_alias_count >= 512) return;
    UseAlias *ua = &c->use_aliases[c->use_alias_count++];
    strncpy(ua->name, alias, 255); ua->name[255] = '\0';
    strncpy(ua->target, target, 255); ua->target[255] = '\0';
    ua->visibility = visibility;
    strncpy(ua->source_file, source_file ? source_file : "", 511);
    ua->source_file[511] = '\0';
}

static void RegisterAliasHelper(Compiler *c, const char *alias, const char *target,
                                 int visibility, const char *source_file) {
    RegisterUseAlias(c, alias, target, visibility, source_file);
    if (c->current_namespace[0] != '\0') {
        char *mangled = MangleName(c, alias);
        if (mangled) {
            RegisterUseAlias(c, mangled, target, visibility, source_file);
            free(mangled);
        }
    }
}

/* Forward declaration needed by preregister_all_uses */
static void RegisterUseNode(Compiler *c, Node *n);

/* Recursively pre-register all NUSE aliases in the AST tree, including
 * those inside NMODULE blocks (pub use re-exports). Push/pop namespace
 * so that e.g. module wrapper { pub use foo::bar; } registers
 * both 'bar->foo::bar' and 'wrapper::bar->foo::bar'. */
static void PreregisterAllUses(Compiler *c, Node *n) {
    if (!n) return;
    if (n->type == NUSE) {
        RegisterUseNode(c, n);
        return;
    }
    if (n->type == NMODULE) {
        char saved_ns[512];
        strncpy(saved_ns, c->current_namespace, 511);
        saved_ns[511] = '\0';
        if (c->current_namespace[0] != '\0') {
            char new_ns[512];
            snprintf(new_ns, sizeof(new_ns), "%s::%s", c->current_namespace, n->single.var);
            strncpy(c->current_namespace, new_ns, 511);
        } else {
            strncpy(c->current_namespace, n->single.var, 511);
        }
        c->current_namespace[511] = '\0';
        PreregisterAllUses(c, n->single.expr);
        strncpy(c->current_namespace, saved_ns, 511);
        c->current_namespace[511] = '\0';
        return;
    }
    if (n->type == NPROGRAM || n->type == NBLOCK) {
        for (int i = 0; i < n->block.count; i++) {
            PreregisterAllUses(c, n->block.stmts[i]);
        }
    }
}

static void RegisterUseNode(Compiler *c, Node *n) {
    if (!n || n->type != NUSE) return;
    const char *target = n->decl.tname;
    if (!target) return;
    int vis = n->decl.visibility;
    size_t tlen = strlen(target);
    if (tlen >= 3 && strcmp(target + tlen - 3, "::*") == 0) {
        char ns_prefix[256];
        strncpy(ns_prefix, target, tlen - 3);
        ns_prefix[tlen - 3] = '\0';
        size_t pfx_len = strlen(ns_prefix);
        for (int fi = 0; fi < c->fncount; fi++) {
            const char *fname = c->fns[fi]->name;
            if (strncmp(fname, ns_prefix, pfx_len) == 0 && fname[pfx_len] == ':' && fname[pfx_len+1] == ':') {
                const char *basename = fname + pfx_len + 2;
                int sym_vis = VIS_PUB;
                for (int si = 0; si < c->mod_symbol_count; si++) {
                    if (strcmp(c->mod_symbols[si].name, fname) == 0) {
                        sym_vis = c->mod_symbols[si].visibility;
                        break;
                    }
                }
                if (sym_vis == VIS_PUB || sym_vis == VIS_DEFAULT) {
                    RegisterAliasHelper(c, basename, fname, vis, c->currentfilepath);
                }
            }
        }
    } else {
        const char *last_sep = strrchr(target, ':');
        if (last_sep && last_sep > target && *(last_sep-1) == ':') {
            const char *basename = last_sep + 1;
            RegisterAliasHelper(c, basename, target, vis, c->currentfilepath);
            RegisterAliasHelper(c, target, target, vis, c->currentfilepath);
        }
    }
}

/* Resolve a name through the alias table; returns target name or NULL */
static const char *ResolveAlias(Compiler *c, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < c->use_alias_count; i++) {
        if (strcmp(c->use_aliases[i].name, name) == 0) {
            return c->use_aliases[i].target;
        }
    }
    return NULL;
}


/* Compile a lazy module on first use. Finds the entry by path, runs
 * preregistertypes + emitnode on the stored AST, then marks it loaded. */
static void LoadLazyModule(Compiler *c, const char *filepath) {
    for (int i = 0; i < c->lazy_mod_count; i++) {
        if (c->lazy_mods[i].loaded) continue;
        if (strcmp(c->lazy_mods[i].path, filepath) != 0) continue;
        /* Mark loaded first to prevent re-entry */
        c->lazy_mods[i].loaded = 1;
        Node *ast = c->lazy_mods[i].ast;
        if (!ast) return;
        /* Save compiler context before compiling lazy module */
        FnProto *saved_current = c->current;
        char saved_fp[512];
        strncpy(saved_fp, c->currentfilepath, 511);
        saved_fp[511] = '\0';
        char saved_ns[512];
        strncpy(saved_ns, c->current_namespace, 511);
        saved_ns[511] = '\0';

        strncpy(c->currentfilepath, c->lazy_mods[i].path, 511);
        c->currentfilepath[511] = '\0';
        c->current_namespace[0] = '\0';

        /* Run type pre-registration (structs etc.) then emit */
        PreregisterTypes(c, ast);
        EmitNode(c, ast);

        /* Restore compiler context */
        c->current = saved_current;
        strncpy(c->currentfilepath, saved_fp, 511);
        c->currentfilepath[511] = '\0';
        strncpy(c->current_namespace, saved_ns, 511);
        c->current_namespace[511] = '\0';

        /* Clear stubs for all fns from this module */
        for (int fi = 0; fi < c->fncount; fi++) {
            if (strcmp(c->fns[fi]->file_name, filepath) == 0) {
                c->fns[fi]->is_lazy_stub = 0;
            }
        }
        /* Keep AST alive until compiler cleanup (same as eager imports) */
        if (c->imported_ast_count < 1024) {
            c->imported_asts[c->imported_ast_count++] = ast;
        } else {
            NodeFree(ast);
        }
        c->lazy_mods[i].ast = NULL;
        return;
    }
}

static int ResolveGlobal(Compiler *c, const char *name, int line) {
    if (!name) return -1;
    /* Follow alias chain transitively (max 16 hops to prevent cycles) */
    const char *resolved_name = name;
    for (int hop = 0; hop < 16; hop++) {
        const char *next = ResolveAlias(c, resolved_name);
        if (!next || strcmp(next, resolved_name) == 0) break;
        resolved_name = next;
    }
    for (int i = 0; i < c->fncount; i++) {
        if (c->fns[i] && strcmp(c->fns[i]->name, resolved_name) == 0) {
            /* Lazy stub: compile the module on first reference */
            if (c->fns[i]->is_lazy_stub) {
                LoadLazyModule(c, c->fns[i]->file_name);
            }
            if (line > 0) CheckSymbolVisibility(c, resolved_name, line);
            return i;
        }
    }
    return -1;
}


static FnProto *NewFn(Compiler *c, const char *name) {
    if(c->fncount>=MAXFNS){ Error(c,0,"Too many functions"); return NULL; }
    FnProto *fn=calloc(1,sizeof(FnProto));
    strncpy(fn->name,name,127);
    strncpy(fn->file_name, c->currentfilepath, sizeof(fn->file_name) - 1);
    fn->file_name[sizeof(fn->file_name) - 1] = '\0';
    fn->id=c->fncount;
    c->fns[c->fncount++]=fn;
    return fn;
}

static const char *GetPayLoadType(Compiler *c, const char *type) {
    if (type && (strncmp(type, "Result<", 7) == 0 || strncmp(type, "Option<", 7) == 0)) {
        const char *start = strchr(type, '<');
        if (start) {
            start++;
            const char *comma = strchr(start, ',');
            const char *gt = strchr(start, '>');
            const char *end = (comma && comma < gt) ? comma : gt;
            if (end) {
                size_t len = end - start;
                char *buf = malloc(len + 1);
                strncpy(buf, start, len);
                buf[len] = '\0';
                int sid = AddStr(c, buf);
                free(buf);
                return c->strpool[sid];
            }
        }
    }
    return "any";
}

static const char *InferExprType(Compiler *c, Node *expr) {
    if (!expr) return "void";
    switch (expr->type) {
        case NINT: return "int";
        case NFLOAT: return "float";
        case NSTR: return "string";
        case NBOOL: return "bool";
        case NNULL: return "null";
        case NARRLIT: {
            const char *elemtype = "any";
            if (expr->arrlit.count > 0) {
                elemtype = InferExprType(c, expr->arrlit.elems[0]);
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "Array<%s, %d>", elemtype, expr->arrlit.count);
            int sid = AddStr(c, buf);
            return c->strpool[sid];
        }
        case NTUPLE: {
            char buf[512] = "(";
            int len = 1;
            for (int i = 0; i < expr->tuple.count; i++) {
                if (i > 0) {
                    if (len + 1 < (int)sizeof(buf)) {
                        strcat(buf, ",");
                        len++;
                    }
                }
                const char *etype = InferExprType(c, expr->tuple.elems[i]);
                if (len < (int)sizeof(buf) - 1) {
                    strncat(buf, etype, sizeof(buf) - len - 1);
                    len = (int)strlen(buf);
                }
            }
            if (len + 1 < (int)sizeof(buf)) {
                strcat(buf, ")");
            }
            int sid = AddStr(c, buf);
            return c->strpool[sid];
        }
        case NNEW: {
            if (expr->newobj.tc > 0) {
                char mangled[256];
                int len = snprintf(mangled, sizeof(mangled), "%s", expr->newobj.name);
                for (int i = 0; i < expr->newobj.tc; i++) {
                    char argmangled[128];
                    MangleTypeName(expr->newobj.targs[i], argmangled, sizeof(argmangled));
                    len += snprintf(mangled + len, sizeof(mangled) - len, "_%s", argmangled);
                }
                int sid = AddStr(c, mangled);
                return c->strpool[sid];
            }
            return expr->newobj.name;
        }
        case NSTRUCTINIT: {
            if (expr->structinit.spread) {
                return InferExprType(c, expr->structinit.spread);
            }
            return "Object";
        }
        case NCHAINEDCMP: {
            return "bool";
        }
        case NIDENT: {
            int slot = ResolveLocal(c, expr->sval);
            if (slot >= 0) {
                return c->current->locals[slot].typename;
            }
            return "any";
        }
        case NCAST: {
            if (expr->cast.targettype) return expr->cast.targettype;
            return "any";
        }
        case NBINOP: {
            const char *op = expr->binop.op;
            if (strcmp(op, "as") == 0) {
                return expr->binop.right->sval;
            }
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
                strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
                return "bool";
            }
            const char *lt = InferExprType(c, expr->binop.left);
            const char *rt = InferExprType(c, expr->binop.right);
            if (strcmp(op, "+") == 0 && (strcmp(lt, "string") == 0 || strcmp(rt, "string") == 0)) return "string";
            if (strcmp(lt, "float") == 0 || strcmp(rt, "float") == 0) return "float";
            if (strcmp(lt, "int") == 0 && strcmp(rt, "int") == 0) return "int";
            return "any";
        }
        case NTRYPROPAGATE: {
            const char *type = InferExprType(c, expr->single.expr);
            return GetPayLoadType(c, type);
        }
        case NUNOP: {
            if (strcmp(expr->binop.op, "!") == 0) return "bool";
            return InferExprType(c, expr->binop.left);
        }
        case NCALL: {
            if (expr->call.callee->type == NIDENT) {
                int fnid = ResolveGlobal(c, expr->call.callee->sval, 0);
                if (fnid >= 0) {
                    return c->fns[fnid]->rettype;
                }
            }
            return "any";
        }
        case NMEMBER: {
            const char *objtype = InferExprType(c, expr->member.obj);
            if (objtype) {
                char cleantype[128];
                strncpy(cleantype, objtype, sizeof(cleantype) - 1);
                cleantype[sizeof(cleantype) - 1] = '\0';
                char *brace = strchr(cleantype, '{');
                if (brace) *brace = '\0';
                
                for (int i = 0; i < c->structcount; i++) {
                    if (strcmp(c->structs[i].name, cleantype) == 0) {
                        for (int j = 0; j < c->structs[i].fc; j++) {
                            if (strcmp(c->structs[i].fnames[j], expr->member.field) == 0) {
                                return c->structs[i].ftypes[j];
                            }
                        }
                    }
                }
            }
            return "any";
        }
        case NINDEX:
            return "any";
        default: return "any";
    }
}

static void GetNodeStr(Node *n, char *buf, size_t bufsize) {
    if (!n) {
        strncpy(buf, "null", bufsize);
        return;
    }
    if (n->type == NINT) {
        snprintf(buf, bufsize, "%lld", (long long)n->ival);
    } else if (n->type == NFLOAT) {
        snprintf(buf, bufsize, "%g", n->fval);
    } else if (n->type == NBOOL) {
        strncpy(buf, n->sval, bufsize);
    } else if (n->type == NSTR) {
        snprintf(buf, bufsize, "\"%s\"", n->sval);
    } else if (n->type == NNULL) {
        strncpy(buf, "null", bufsize);
    } else if (n->type == NIDENT) {
        strncpy(buf, n->sval, bufsize);
    } else if (n->type == NBINOP) {
        char lstr[128], rstr[128];
        GetNodeStr(n->binop.left, lstr, sizeof(lstr));
        GetNodeStr(n->binop.right, rstr, sizeof(rstr));
        snprintf(buf, bufsize, "%s %s %s", lstr, n->binop.op, rstr);
    } else {
        strncpy(buf, "?", bufsize);
    }
}

static int IsNonPrimitiveType(const char *type) {
    if (!type || type[0] == '\0') return 0;
    // extract base type for refinement checks
    char base[128];
    const char *brace = strchr(type, '{');
    if (brace) {
        int len = (int)(brace - type);
        if (len >= 128) len = 127;
        strncpy(base, type, len);
        base[len] = '\0';
    } else {
        strncpy(base, type, 127);
        base[127] = '\0';
    }
    // trim trailing whitespace
    int blen = (int)strlen(base);
    while (blen > 0 && (base[blen-1] == ' ' || base[blen-1] == '\t')) base[--blen] = '\0';
    if (strcmp(base, "any") == 0) return 0;
    if (strcmp(base, "int") == 0) return 0;
    if (strcmp(base, "float") == 0) return 0;
    if (strcmp(base, "bool") == 0) return 0;
    if (strcmp(base, "string") == 0) return 0;
    if (strcmp(base, "void") == 0) return 0;
    return 1;
}

static void MangleTypeName(const char *type, char *dest, size_t destsize) {
    size_t d = 0;
    for (size_t s = 0; type[s] != '\0' && d + 1 < destsize; s++) {
        if (type[s] == '<') {
            dest[d++] = '_';
        } else if (type[s] == '>') {
            // skip
        } else if (type[s] == ',') {
            dest[d++] = '_';
        } else if (type[s] == ' ' || type[s] == '\t' || type[s] == '\n' || type[s] == '\r') {
            // skip whitespace
        } else {
            dest[d++] = type[s];
        }
    }
    dest[d] = '\0';
}

static void SplitTypeTopLevel(const char *type, char op, char ***parts, int *count) {
    int cap = 4;
    *parts = malloc(cap * sizeof(char*));
    *count = 0;
    
    int depth = 0;
    const char *start = type;
    const char *p = type;
    while (*p) {
        if (*p == '<' || *p == '(') depth++;
        else if (*p == '>' || *p == ')') depth--;
        else if (*p == op && depth == 0) {
            size_t len = p - start;
            char *part = malloc(len + 1);
            memcpy(part, start, len);
            part[len] = '\0';
            if (*count == cap) {
                cap *= 2;
                *parts = realloc(*parts, cap * sizeof(char*));
            }
            (*parts)[(*count)++] = part;
            start = p + 1;
        }
        p++;
    }
    size_t len = p - start;
    char *part = malloc(len + 1);
    memcpy(part, start, len);
    part[len] = '\0';
    if (*count == cap) {
        cap *= 2;
        *parts = realloc(*parts, cap * sizeof(char*));
    }
    (*parts)[(*count)++] = part;
}

static void AppendUnionDetails(char *buf, size_t bufsize, const char *expected) {
    if (!expected) return;
    char expclean[256];
    strncpy(expclean, expected, sizeof(expclean) - 1);
    expclean[sizeof(expclean) - 1] = '\0';
    
    char **parts = NULL;
    int count = 0;
    SplitTypeTopLevel(expclean, '|', &parts, &count);
    if (count > 1) {
        char listbuf[256] = "";
        for (int i = 0; i < count; i++) {
            char *brace = strchr(parts[i], '{');
            if (brace) *brace = '\0';
            
            strncat(listbuf, parts[i], sizeof(listbuf) - strlen(listbuf) - 1);
            if (i + 1 < count) {
                strncat(listbuf, ", ", sizeof(listbuf) - strlen(listbuf) - 1);
            }
            free(parts[i]);
        }
        free(parts);
        size_t len = strlen(buf);
        snprintf(buf + len, bufsize - len, " (valid types: %s)", listbuf);
    } else {
        if (count > 0) {
            free(parts[0]);
            free(parts);
        }
    }
}

static void Warning(Compiler *c, int line, const char *msg) {
    fprintf(stderr, "Compiler warning on line %d: %s\n", line, msg);
}

static void SubstituteVarWithConst(Node *node, const char *varname, Node *valnode) {
    if (!node || !valnode) return;
    if (node->type == NIDENT && strcmp(node->sval, varname) == 0) {
        free(node->sval);
        node->type = valnode->type;
        if (valnode->type == NINT) {
            node->ival = valnode->ival;
        } else if (valnode->type == NFLOAT) {
            node->fval = valnode->fval;
        } else if (valnode->type == NBOOL || valnode->type == NSTR) {
            node->sval = strdup(valnode->sval);
        }
        return;
    }
    switch (node->type) {
        case NBINOP:
        case NUNOP:
            SubstituteVarWithConst(node->binop.left, varname, valnode);
            SubstituteVarWithConst(node->binop.right, varname, valnode);
            break;
        case NCALL:
            SubstituteVarWithConst(node->call.callee, varname, valnode);
            for (int i = 0; i < node->call.argc; i++) {
                SubstituteVarWithConst(node->call.args[i], varname, valnode);
            }
            break;
        case NMEMBER:
            SubstituteVarWithConst(node->member.obj, varname, valnode);
            break;
        case NINDEX:
            SubstituteVarWithConst(node->index.arr, varname, valnode);
            SubstituteVarWithConst(node->index.idx, varname, valnode);
            break;
        default: break;
    }
}

static int ParseRefParts(const char *typename, char *basetype, char *varname, char *condstr);

static int IsRefSubType(const char *expected, const char *actual) {
    char expbase[128], expvar[128], expcond[512];
    char actbase[128], actvar[128], actcond[512];
    if (!ParseRefParts(expected, expbase, expvar, expcond)) return 0;
    if (!ParseRefParts(actual, actbase, actvar, actcond)) return 0;
    
    if (strcmp(expbase, actbase) != 0) return 0;
    
    int compatible = 0;
    char expop[16] = "";
    double expval = 0;
    char actop[16] = "";
    double actval = 0;
    
    char expcondclean[512];
    strcpy(expcondclean, expcond);
    char *p = expcondclean;
    while (*p == ' ' || *p == '\t') p++;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    while (*p == ' ' || *p == '\t') p++;
    char *opstart = p;
    while (*p == '>' || *p == '<' || *p == '=' || *p == '!') p++;
    int oplen = (int)(p - opstart);
    if (oplen > 0 && oplen < 16) {
        strncpy(expop, opstart, oplen);
        expop[oplen] = '\0';
    }
    while (*p == ' ' || *p == '\t') p++;
    expval = atof(p);
    
    char actcondclean[512];
    strcpy(actcondclean, actcond);
    p = actcondclean;
    while (*p == ' ' || *p == '\t') p++;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    while (*p == ' ' || *p == '\t') p++;
    opstart = p;
    while (*p == '>' || *p == '<' || *p == '=' || *p == '!') p++;
    oplen = (int)(p - opstart);
    if (oplen > 0 && oplen < 16) {
        strncpy(actop, opstart, oplen);
        actop[oplen] = '\0';
    }
    while (*p == ' ' || *p == '\t') p++;
    actval = atof(p);
    
    int expisgt = (strcmp(expop, ">") == 0 || strcmp(expop, ">=") == 0);
    int actisgt = (strcmp(actop, ">") == 0 || strcmp(actop, ">=") == 0);
    int expislt = (strcmp(expop, "<") == 0 || strcmp(expop, "<=") == 0);
    int actislt = (strcmp(actop, "<") == 0 || strcmp(actop, "<=") == 0);
    
    if (expisgt && actisgt) {
        if (actval >= expval) compatible = 1;
    } else if (expislt && actislt) {
        if (actval <= expval) compatible = 1;
    } else if (strcmp(expop, "==") == 0 && strcmp(actop, "==") == 0) {
        if (actval == expval) compatible = 1;
    }
    return compatible;
}

static int ParseRefParts(const char *typename, char *basetype, char *varname, char *condstr) {
    const char *brace = strchr(typename, '{');
    if (!brace) return 0;
    int baselen = (int)(brace - typename);
    if (baselen >= 128) baselen = 127;
    strncpy(basetype, typename, baselen);
    basetype[baselen] = '\0';
    const char *start = brace + 1;
    const char *sep = strchr(start, ':');
    if (!sep) sep = strchr(start, '|');
    if (!sep) return 0;
    int varlen = (int)(sep - start);
    if (varlen >= 128) varlen = 127;
    strncpy(varname, start, varlen);
    varname[varlen] = '\0';
    const char *condstart = sep + 1;
    int len = (int)strlen(condstart);
    if (len > 0 && condstart[len - 1] == '}') len--;
    if (len >= 512) len = 511;
    strncpy(condstr, condstart, len);
    condstr[len] = '\0';
    return 1;
}

static void ChkRefConst(Compiler *c, const char *typename, int line, Node *valnode) {
    char basetype[128], varname[128], condstr[512];
    if (!ParseRefParts(typename, basetype, varname, condstr)) return;
    
    char *condptr = condstr;
    while (*condptr == ' ' || *condptr == '\t') condptr++;
    int clen = (int)strlen(condptr);
    while (clen > 0 && (condptr[clen-1] == ' ' || condptr[clen-1] == '\t')) condptr[--clen] = '\0';
    
    int parseerr = 0;
    Node *condast = ParseExpr(condptr, &parseerr);
    if (parseerr || !condast) {
        if (condast) NodeFree(condast);
        return;
    }
    
    if (valnode) {
        Node *folded = NodeOpt(NodeCopy(valnode));
        if (folded && (folded->type == NINT || folded->type == NFLOAT || folded->type == NBOOL || folded->type == NSTR)) {
            Node *condastcopy = NodeCopy(condast);
            SubstituteVarWithConst(condastcopy, varname, folded);
            Node *optimized = NodeOpt(condastcopy);
            if (optimized->type == NBOOL) {
                if (strcmp(optimized->sval, "false") == 0) {
                    char errmsg[1024];
                    snprintf(errmsg, sizeof(errmsg), "TypeError: value does not satisfy refinement condition '%s'", condptr);
                    Error(c, line, errmsg);
                }
            }
            NodeFree(optimized);
        }
        if (folded) NodeFree(folded);
    }
    NodeFree(condast);
}

static void EmitRefCheck(Compiler *c, const char *typename, int line, Node *valnode) {
    char basetype[128];
    char varname[128];
    char condstr[512];
    if (!ParseRefParts(typename, basetype, varname, condstr)) {
        return;
    }
    
    char *condptr = condstr;
    while (*condptr == ' ' || *condptr == '\t') condptr++;
    int clen = (int)strlen(condptr);
    while (clen > 0 && (condptr[clen-1] == ' ' || condptr[clen-1] == '\t')) condptr[--clen] = '\0';
    
    int parseerr = 0;
    Node *condast = ParseExpr(condptr, &parseerr);
    if (parseerr || !condast) {
        Error(c, line, "Failed to parse refinement type condition");
        if (condast) NodeFree(condast);
        return;
    }
    
    if (valnode) {
        Node *folded = NodeOpt(NodeCopy(valnode));
        if (folded && (folded->type == NINT || folded->type == NFLOAT || folded->type == NBOOL || folded->type == NSTR)) {
            Node *condastcopy = NodeCopy(condast);
            SubstituteVarWithConst(condastcopy, varname, folded);
            Node *optimized = NodeOpt(condastcopy);
            if (optimized->type == NBOOL) {
                if (strcmp(optimized->sval, "false") == 0) {
                    char errmsg[1024];
                    snprintf(errmsg, sizeof(errmsg), "TypeError: value does not satisfy refinement condition '%s'", condptr);
                    Error(c, line, errmsg);
                }
                NodeFree(optimized);
                NodeFree(condast);
                NodeFree(folded);
                return;
            }
            NodeFree(optimized);
        }
        if (folded) NodeFree(folded);
    }
    
    Chunk *ch = &c->current->chunk;
    ChunkWrite(ch, OPDUP, line);
    int scope = c->current->scopedepth;
    c->current->scopedepth++;
    int slot = AddLocal(c, varname, 0);
    c->current->scopedepth--;
    if (slot < 0) {
        ChunkWrite(ch, OPPOP, line);
        NodeFree(condast);
        return;
    }
    ChunkWrite(ch, OPSTORE, line);
    ChunkWriteU16(ch, (uint16_t)slot, line);
    
    EmitExpr(c, condast);
    NodeFree(condast);
    int jumpiffalse = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
    int skipthrow = ChunkEmitJump(ch, OPJUMP, line);
    ChunkPatchJump(ch, jumpiffalse);
    char errmsg[1024];
    snprintf(errmsg, sizeof(errmsg), "DependentTypeError: value does not satisfy refinement condition '%s'", condptr);
    int msgsid = AddStr(c, errmsg);
    ChunkWrite(ch, OPPUSHSTR, line);
    ChunkWriteU16(ch, (uint16_t)msgsid, line);
    ChunkWrite(ch, OPTHROW, line);
    ChunkPatchJump(ch, skipthrow);
    c->current->localcount--;
}

static int ParseFnTypeParts(const char *fntype, char ***params, int *paramcount, char **ret) {
    if (strncmp(fntype, "fn(", 3) != 0) return 0;
    const char *p = fntype + 3;
    int cap = 4;
    *params = malloc(cap * sizeof(char*));
    *paramcount = 0;
    int depth = 0;
    const char *start = p;
    while (*p) {
        if (*p == '<' || *p == '(') depth++;
        else if (*p == '>' || *p == ')') depth--;
        
        if (depth < 0) {
            break;
        }
        if (*p == ',' && depth == 0) {
            size_t len = p - start;
            char *part = malloc(len + 1);
            memcpy(part, start, len);
            part[len] = '\0';
            if (*paramcount == cap) {
                cap *= 2;
                *params = realloc(*params, cap * sizeof(char*));
            }
            (*params)[(*paramcount)++] = part;
            start = p + 1;
        }
        p++;
    }
    if (p > start) {
        size_t len = p - start;
        char *part = malloc(len + 1);
        memcpy(part, start, len);
        part[len] = '\0';
        if (*paramcount == cap) {
            cap *= 2;
            *params = realloc(*params, cap * sizeof(char*));
        }
        (*params)[(*paramcount)++] = part;
    }
    
    if (*p == ')') p++;
    if (strncmp(p, "->", 2) == 0) {
        *ret = strdup(p + 2);
    } else {
        *ret = strdup("void");
    }
    return 1;
}

static void ParseGenericArgs(const char *typename, char ***args, int *argcount) {
    const char *start = strchr(typename, '<');
    if (!start) {
        *args = NULL;
        *argcount = 0;
        return;
    }
    start++;
    const char *end = strrchr(start, '>');
    if (!end || end <= start) {
        *args = NULL;
        *argcount = 0;
        return;
    }
    size_t len = end - start;
    char *inner = malloc(len + 1);
    strncpy(inner, start, len);
    inner[len] = '\0';
    SplitTypeTopLevel(inner, ',', args, argcount);
    free(inner);
}

static const char *ResolveType(Compiler *c, const char *typename) {
    if (!typename) return NULL;
    const char *lt = strchr(typename, '<');
    if (lt) {
        char base[128];
        size_t len = lt - typename;
        if (len >= 128) len = 127;
        strncpy(base, typename, len);
        base[len] = '\0';
        for (int i = 0; i < c->aliascount; i++) {
            if (strcmp(c->aliases[i].name, base) == 0) {
                char **targs = NULL;
                int tc = 0;
                ParseGenericArgs(typename, &targs, &tc);
                char *target = strdup(c->aliases[i].target);
                for (int k = 0; k < c->aliases[i].tpc && k < tc; k++) {
                    char *replaced = ReplAceTypeParam(target, c->aliases[i].tparams[k], targs[k]);
                    free(target);
                    target = replaced;
                }
                const char *res = ResolveType(c, target);
                int sid = AddStr(c, res);
                free(target);
                for (int k = 0; k < tc; k++) free(targs[k]);
                if (targs) free(targs);
                return c->strpool[sid];
            }
        }
    }
    for (int i = 0; i < c->aliascount; i++) {
        if (strcmp(c->aliases[i].name, typename) == 0) {
            if (c->aliases[i].isopaque) {
                return typename;
            }
            return ResolveType(c, c->aliases[i].target);
        }
    }
    return typename;
}

static int TypeNameMatches(const char *t1, const char *t2) {
    if (!t1 || !t2) return 0;
    if (strcmp(t1, t2) == 0) return 1;
    const char *p1 = strrchr(t1, ':');
    p1 = p1 ? p1 + 1 : t1;
    const char *p2 = strrchr(t2, ':');
    p2 = p2 ? p2 + 1 : t2;
    return strcmp(p1, p2) == 0;
}

static int TypesCompatibleInternal(Compiler *c, const char *expected, const char *actual) {
    expected = ResolveType(c, expected);
    actual = ResolveType(c, actual);
    if (!expected || expected[0] == '\0' || strcmp(expected, "any") == 0) return 1;
    if (!actual || actual[0] == '\0' || strcmp(actual, "any") == 0) return 1;
    if (expected[0] == '$' || actual[0] == '$') return 1;
    if (strcmp(actual, "null") == 0) return 1;

    if (strcmp(expected, "array") == 0 && (actual[0] == '(' || strncmp(actual, "Array<", 6) == 0 || actual[0] == '[')) return 1;
    if (strcmp(actual, "array") == 0 && (expected[0] == '(' || strncmp(expected, "Array<", 6) == 0 || expected[0] == '[')) return 1;
    if ((expected[0] == '[' || !strncmp(expected, "Array<", 6) || !strcmp(expected, "array")) &&
        (actual[0] == '[' || !strncmp(actual, "Array<", 6) || !strcmp(actual, "array"))) return 1;

    if (expected[0] == '(' && expected[strlen(expected) - 1] == ')' &&
        actual[0] == '(' && actual[strlen(actual) - 1] == ')') {
        char expinner[256];
        char actinner[256];
        size_t explen = strlen(expected);
        size_t actlen = strlen(actual);
        if (explen > 2 && actlen > 2) {
            strncpy(expinner, expected + 1, explen - 2);
            expinner[explen - 2] = '\0';
            strncpy(actinner, actual + 1, actlen - 2);
            actinner[actlen - 2] = '\0';
            
            char **expparts = NULL;
            int expcount = 0;
            SplitTypeTopLevel(expinner, ',', &expparts, &expcount);
            
            char **actparts = NULL;
            int actcount = 0;
            SplitTypeTopLevel(actinner, ',', &actparts, &actcount);
            
            int match = 1;
            if (expcount == actcount) {
                for (int i = 0; i < expcount; i++) {
                    if (!TypesCompatibleInternal(c, expparts[i], actparts[i])) {
                        match = 0;
                        break;
                    }
                }
            } else {
                match = 0;
            }
            
            for (int i = 0; i < expcount; i++) free(expparts[i]);
            free(expparts);
            for (int i = 0; i < actcount; i++) free(actparts[i]);
            free(actparts);
            
            if (match) return 1;
            return 0;
        }
    }
    
    if (strchr(expected, '{') || strchr(actual, '{')) {
        char expclean[256];
        char actclean[256];
        strncpy(expclean, expected, sizeof(expclean) - 1);
        expclean[sizeof(expclean) - 1] = '\0';
        char *brace = strchr(expclean, '{');
        if (brace) *brace = '\0';
        
        strncpy(actclean, actual, sizeof(actclean) - 1);
        actclean[sizeof(actclean) - 1] = '\0';
        brace = strchr(actclean, '{');
        if (brace) *brace = '\0';
        
        if (!TypesCompatibleInternal(c, expclean, actclean)) {
            return 0;
        }
        
        if (strchr(expected, '{')) {
            if (strchr(actual, '{')) {
                return IsRefSubType(expected, actual);
            } else {
                return 1;
            }
        }
        return 1;
    }
    
    char expclean[256];
    char actclean[256];
    strncpy(expclean, expected, sizeof(expclean) - 1);
    expclean[sizeof(expclean) - 1] = '\0';
    char *brace = strchr(expclean, '{');
    if (brace) *brace = '\0';

    strncpy(actclean, actual, sizeof(actclean) - 1);
    actclean[sizeof(actclean) - 1] = '\0';
    brace = strchr(actclean, '{');
    if (brace) *brace = '\0';

    char expmangled[256];
    char actmangled[256];
    MangleTypeName(expclean, expmangled, sizeof(expmangled));
    MangleTypeName(actclean, actmangled, sizeof(actmangled));
    
    if (TypeNameMatches(expmangled, actmangled)) return 1;
    
    if (strchr(expmangled, '|')) {
        char **parts = NULL;
        int count = 0;
        SplitTypeTopLevel(expmangled, '|', &parts, &count);
        int match = 0;
        for (int i = 0; i < count; i++) {
            if (TypesCompatibleInternal(c, parts[i], actmangled)) {
                match = 1;
                break;
            }
        }
        for (int i = 0; i < count; i++) free(parts[i]);
        free(parts);
        if (match) return 1;
    }
    
    if (strchr(actmangled, '|')) {
        char **parts = NULL;
        int count = 0;
        SplitTypeTopLevel(actmangled, '|', &parts, &count);
        int allmatch = 1;
        for (int i = 0; i < count; i++) {
            if (!TypesCompatibleInternal(c, expmangled, parts[i])) {
                allmatch = 0;
                break;
            }
        }
        for (int i = 0; i < count; i++) free(parts[i]);
        free(parts);
        if (allmatch) return 1;
        return 0;
    }
    
    if (strchr(expmangled, '&')) {
        char **parts = NULL;
        int count = 0;
        SplitTypeTopLevel(expmangled, '&', &parts, &count);
        int allmatch = 1;
        for (int i = 0; i < count; i++) {
            if (!TypesCompatibleInternal(c, parts[i], actmangled)) {
                allmatch = 0;
                break;
            }
        }
        for (int i = 0; i < count; i++) free(parts[i]);
        free(parts);
        return allmatch;
    }
    
    if (strchr(actmangled, '&')) {
        char **parts = NULL;
        int count = 0;
        SplitTypeTopLevel(actmangled, '&', &parts, &count);
        int match = 0;
        for (int i = 0; i < count; i++) {
            if (TypesCompatibleInternal(c, expmangled, parts[i])) {
                match = 1;
                break;
            }
        }
        for (int i = 0; i < count; i++) free(parts[i]);
        free(parts);
        if (match) return 1;
    }
    
    if (strncmp(expmangled, "fn(", 3) == 0 && strncmp(actmangled, "fn(", 3) == 0) {
        char **expparams = NULL;
        int expparamcount = 0;
        char *expret = NULL;
        char **actparams = NULL;
        int actparamcount = 0;
        char *actret = NULL;
        
        int expok = ParseFnTypeParts(expmangled, &expparams, &expparamcount, &expret);
        int actok = ParseFnTypeParts(actmangled, &actparams, &actparamcount, &actret);
        
        int match = 0;
        if (expok && actok && expparamcount == actparamcount) {
            match = 1;
            for (int i = 0; i < expparamcount; i++) {
                if (!TypesCompatibleInternal(c, actparams[i], expparams[i])) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                if (!TypesCompatibleInternal(c, expret, actret)) {
                    match = 0;
                }
            }
        }
        
        if (expok) {
            for (int i = 0; i < expparamcount; i++) free(expparams[i]);
            free(expparams);
            free(expret);
        }
        if (actok) {
            for (int i = 0; i < actparamcount; i++) free(actparams[i]);
            free(actparams);
            free(actret);
        }
        return match;
    }
    
    int intfidx = -1;
    for (int i = 0; i < c->interfacecount; i++) {
        if (strcmp(c->interfaces[i].name, expmangled) == 0) {
            intfidx = i;
            break;
        }
    }
    
    if (intfidx >= 0) {
        int structidx = -1;
        for (int i = 0; i < c->structcount; i++) {
            if (strcmp(c->structs[i].name, actmangled) == 0) {
                structidx = i;
                break;
            }
        }
        
        if (structidx >= 0) {
            for (int j = 0; j < c->structs[structidx].ic; j++) {
                if (strcmp(c->structs[structidx].interfaces[j], expmangled) == 0) {
                    return 1;
                }
            }
            CompilerInterface *intf = &c->interfaces[intfidx];
            CompilerStruct *str = &c->structs[structidx];
            
            char *resolvedassoc[32] = {NULL};
            for (int i = 0; i < intf->assoccount && i < 32; i++) {
                for (int m = 0; m < intf->count; m++) {
                    int foundsf = -1;
                    for (int sfield = 0; sfield < str->fc; sfield++) {
                        if (strcmp(str->fnames[sfield], intf->fnames[m]) == 0) {
                            foundsf = sfield;
                            break;
                        }
                    }
                    if (foundsf >= 0) {
                        char **sparams = NULL;
                        int sparamcount = 0;
                        char *sret = NULL;
                        if (ParseFnTypeParts(str->ftypes[foundsf], &sparams, &sparamcount, &sret)) {
                            if (strcmp(intf->rettypes[m], intf->assoctypes[i]) == 0) {
                                resolvedassoc[i] = strdup(sret);
                            }
                            for (int k = 0; k < intf->paramcounts[m] && k < sparamcount; k++) {
                                if (strcmp(intf->paramtypes[m][k], intf->assoctypes[i]) == 0) {
                                    resolvedassoc[i] = strdup(sparams[k]);
                                }
                            }
                            for (int k = 0; k < sparamcount; k++) free(sparams[k]);
                            free(sparams);
                            free(sret);
                        }
                    }
                    if (resolvedassoc[i]) break;
                }
            }

            int allmethodsok = 1;
            for (int i = 0; i < intf->count; i++) {
                int foundfield = 0;
                for (int j = 0; j < str->fc; j++) {
                    if (strcmp(str->fnames[j], intf->fnames[i]) == 0) {
                        foundfield = 1;
                        char expectedsig[512];
                        strcpy(expectedsig, "fn(");
                        for (int k = 0; k < intf->paramcounts[i]; k++) {
                            const char *ptype = intf->paramtypes[i][k];
                            for (int a = 0; a < intf->assoccount && a < 32; a++) {
                                if (resolvedassoc[a] && strcmp(ptype, intf->assoctypes[a]) == 0) {
                                    ptype = resolvedassoc[a];
                                    break;
                                }
                            }
                            strcat(expectedsig, ptype);
                            if (k + 1 < intf->paramcounts[i]) {
                                strcat(expectedsig, ",");
                            }
                        }
                        strcat(expectedsig, ")->");
                        const char *rtype = intf->rettypes[i];
                        for (int a = 0; a < intf->assoccount && a < 32; a++) {
                            if (resolvedassoc[a] && strcmp(rtype, intf->assoctypes[a]) == 0) {
                                rtype = resolvedassoc[a];
                                break;
                            }
                        }
                        strcat(expectedsig, rtype);
                        
                        if (!TypesCompatibleInternal(c, expectedsig, str->ftypes[j])) {
                            allmethodsok = 0;
                        }
                        break;
                    }
                }
                if (!foundfield) {
                    allmethodsok = 0;
                }
                if (!allmethodsok) break;
            }
            
            for (int a = 0; a < intf->assoccount && a < 32; a++) {
                if (resolvedassoc[a]) free(resolvedassoc[a]);
            }
            return allmethodsok;
        }
        return 0;
    }
    
    return strcmp(expmangled, actmangled) == 0;
}

static int TypesCompatible(Compiler *c, const char *expected, const char *actual) {
    return TypesCompatibleInternal(c, expected, actual);
}

static void ChkStaticRecUrsion(Compiler *c, Node *node, const char *fnname, int incond) {
    if (!node) return;
    switch (node->type) {
        case NCALL:
            if (node->call.callee->type == NIDENT && strcmp(node->call.callee->sval, fnname) == 0) {
                if (!incond) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "StaticInfiniteRecursionError: unconditional recursive call to '%s' will result in stack overflow.", fnname);
                    Error(c, node->line, buf);
                }
            }
            for (int i = 0; i < node->call.argc; i++) {
                ChkStaticRecUrsion(c, node->call.args[i], fnname, incond);
            }
            break;
        case NIF:
            ChkStaticRecUrsion(c, node->ifn.cond, fnname, incond);
            ChkStaticRecUrsion(c, node->ifn.then, fnname, 1);
            ChkStaticRecUrsion(c, node->ifn.els, fnname, 1);
            break;
        case NTERNARY:
            ChkStaticRecUrsion(c, node->ternary.cond, fnname, incond);
            ChkStaticRecUrsion(c, node->ternary.tval, fnname, 1);
            ChkStaticRecUrsion(c, node->ternary.fval, fnname, 1);
            break;
        case NWHILE:
            ChkStaticRecUrsion(c, node->whilen.cond, fnname, incond);
            ChkStaticRecUrsion(c, node->whilen.body, fnname, 1);
            break;
        case NFOR:
            if (node->forn.init) ChkStaticRecUrsion(c, node->forn.init, fnname, incond);
            if (node->forn.cond) ChkStaticRecUrsion(c, node->forn.cond, fnname, incond);
            if (node->forn.post) ChkStaticRecUrsion(c, node->forn.post, fnname, incond);
            ChkStaticRecUrsion(c, node->forn.body, fnname, 1);
            break;
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < node->block.count; i++) {
                ChkStaticRecUrsion(c, node->block.stmts[i], fnname, incond);
            }
            break;
        case NTRYCATCH:
            ChkStaticRecUrsion(c, node->trycatch.tryblock, fnname, 1);
            ChkStaticRecUrsion(c, node->trycatch.catchblock, fnname, 1);
            if (node->trycatch.finallyblock) ChkStaticRecUrsion(c, node->trycatch.finallyblock, fnname, 1);
            break;
        case NSWITCH:
            ChkStaticRecUrsion(c, node->sw.expr, fnname, incond);
            for (int i = 0; i < node->sw.casecount; i++) {
                ChkStaticRecUrsion(c, node->sw.casebodies[i], fnname, 1);
            }
            if (node->sw.defaultbody) ChkStaticRecUrsion(c, node->sw.defaultbody, fnname, 1);
            break;
        case NVARDECL:
        case NCONSTDECL:
            if (node->decl.init) ChkStaticRecUrsion(c, node->decl.init, fnname, incond);
            break;
        case NASSIGN:
            ChkStaticRecUrsion(c, node->assign.value, fnname, incond);
            break;
        case NRETURN:
            if (node->single.expr) ChkStaticRecUrsion(c, node->single.expr, fnname, incond);
            break;
        case NPRINT:
        case NEXPRSTMT:
            if (node->single.expr) ChkStaticRecUrsion(c, node->single.expr, fnname, incond);
            break;
        default:
            break;
    }
}

static void TypeSubstitute(Node *n, char **tparams, char **targs, int tc) {
    if (!n) return;
    if (n->type == NFNDECL) {
        if (n->fn.ret) {
            for (int j = 0; j < tc; j++) {
                if (strcmp(n->fn.ret, tparams[j]) == 0) {
                    free(n->fn.ret);
                    n->fn.ret = strdup(targs[j]);
                    break;
                }
            }
        }
        for (int i = 0; i < n->fn.pc; i++) {
            if (n->fn.ptypes && n->fn.ptypes[i]) {
                for (int j = 0; j < tc; j++) {
                    if (strcmp(n->fn.ptypes[i], tparams[j]) == 0) {
                        free(n->fn.ptypes[i]);
                        n->fn.ptypes[i] = strdup(targs[j]);
                        break;
                    }
                }
            }
        }
        TypeSubstitute(n->fn.body, tparams, targs, tc);
    } else if (n->type == NVARDECL || n->type == NCONSTDECL) {
        if (n->decl.tname) {
            for (int j = 0; j < tc; j++) {
                if (strcmp(n->decl.tname, tparams[j]) == 0) {
                    free(n->decl.tname);
                    n->decl.tname = strdup(targs[j]);
                    break;
                }
            }
        }
        TypeSubstitute(n->decl.init, tparams, targs, tc);
        TypeSubstitute(n->decl.isconst, tparams, targs, tc);
    } else {
        switch (n->type) {
            case NBINOP: case NUNOP:
                TypeSubstitute(n->binop.left, tparams, targs, tc);
                TypeSubstitute(n->binop.right, tparams, targs, tc);
                break;
            case NBLOCK: case NPROGRAM:
                for (int i = 0; i < n->block.count; i++) {
                    TypeSubstitute(n->block.stmts[i], tparams, targs, tc);
                }
                break;
            case NIF:
                TypeSubstitute(n->ifn.cond, tparams, targs, tc);
                TypeSubstitute(n->ifn.then, tparams, targs, tc);
                TypeSubstitute(n->ifn.els, tparams, targs, tc);
                break;
            case NWHILE:
                TypeSubstitute(n->whilen.cond, tparams, targs, tc);
                TypeSubstitute(n->whilen.body, tparams, targs, tc);
                break;
            case NFOR:
                TypeSubstitute(n->forn.init, tparams, targs, tc);
                TypeSubstitute(n->forn.cond, tparams, targs, tc);
                TypeSubstitute(n->forn.post, tparams, targs, tc);
                TypeSubstitute(n->forn.body, tparams, targs, tc);
                break;
            case NFORIN:
                TypeSubstitute(n->forin.iter, tparams, targs, tc);
                TypeSubstitute(n->forin.body, tparams, targs, tc);
                break;
            case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT:
                TypeSubstitute(n->single.expr, tparams, targs, tc);
                break;
            case NTRYCATCH:
                TypeSubstitute(n->trycatch.tryblock, tparams, targs, tc);
                TypeSubstitute(n->trycatch.catchblock, tparams, targs, tc);
                TypeSubstitute(n->trycatch.finallyblock, tparams, targs, tc);
                break;
            case NTHROW:
                TypeSubstitute(n->throwstmt.expr, tparams, targs, tc);
                break;
            case NCOALESCE:
                TypeSubstitute(n->coalesce.left, tparams, targs, tc);
                TypeSubstitute(n->coalesce.right, tparams, targs, tc);
                break;
            case NOPTCHAIN:
                TypeSubstitute(n->optchain.obj, tparams, targs, tc);
                TypeSubstitute(n->optchain.idx, tparams, targs, tc);
                break;
            case NSTRUCTINIT:
                TypeSubstitute(n->structinit.spread, tparams, targs, tc);
                for (int i = 0; i < n->structinit.fc; i++) {
                    TypeSubstitute(n->structinit.fvals[i], tparams, targs, tc);
                }
                break;
            case NCHAINEDCMP:
                for (int i = 0; i < n->chainedcmp.count + 1; i++) {
                    TypeSubstitute(n->chainedcmp.exprs[i], tparams, targs, tc);
                }
                break;
            case NRANGE:
                TypeSubstitute(n->range.start, tparams, targs, tc);
                TypeSubstitute(n->range.end, tparams, targs, tc);
                break;
            case NASSIGN:
                TypeSubstitute(n->assign.target, tparams, targs, tc);
                TypeSubstitute(n->assign.value, tparams, targs, tc);
                break;
            case NCALL:
            case NMACROCALL:
            case NTEMPLATECALL:
                TypeSubstitute(n->call.callee, tparams, targs, tc);
                for (int i = 0; i < n->call.argc; i++) {
                    TypeSubstitute(n->call.args[i], tparams, targs, tc);
                }
                break;
            case NINDEX:
                TypeSubstitute(n->index.arr, tparams, targs, tc);
                TypeSubstitute(n->index.idx, tparams, targs, tc);
                break;
            case NMEMBER:
                TypeSubstitute(n->member.obj, tparams, targs, tc);
                break;
            case NARRLIT:
                for (int i = 0; i < n->arrlit.count; i++) {
                    TypeSubstitute(n->arrlit.elems[i], tparams, targs, tc);
                }
                break;
            case NMAPLIT:
                for (int i = 0; i < n->maplit.count; i++) {
                    TypeSubstitute(n->maplit.vals[i], tparams, targs, tc);
                }
                break;
            case NINC: case NDEC:
                TypeSubstitute(n->incdec.expr, tparams, targs, tc);
                break;
            case NAWAIT:
                TypeSubstitute(n->awaitn.expr, tparams, targs, tc);
                break;
            case NSPAWN:
                TypeSubstitute(n->spawnn.expr, tparams, targs, tc);
                break;
            case NSWITCH:
                TypeSubstitute(n->sw.expr, tparams, targs, tc);
                for (int i = 0; i < n->sw.casecount; i++) {
                    TypeSubstitute(n->sw.casevals[i], tparams, targs, tc);
                    TypeSubstitute(n->sw.casebodies[i], tparams, targs, tc);
                }
                TypeSubstitute(n->sw.defaultbody, tparams, targs, tc);
                break;
            case NYIELD:
                TypeSubstitute(n->yieldstmt.expr, tparams, targs, tc);
                break;
            case NSPREAD:
                TypeSubstitute(n->spread.expr, tparams, targs, tc);
                break;
            case NDESTRUCTARR:
                TypeSubstitute(n->destructarr.value, tparams, targs, tc);
                break;
            case NDESTRUCTOBJ:
                TypeSubstitute(n->destructobj.value, tparams, targs, tc);
                break;
            case NMULTIRETURN:
                for (int i = 0; i < n->multiret.count; i++) {
                    TypeSubstitute(n->multiret.exprs[i], tparams, targs, tc);
                }
                break;
            default: break;
        }
    }
}

// forward decls
static void EmitNode(Compiler *c, Node *n);
static void EmitExpr(Compiler *c, Node *n);

static void RunCTFE(Compiler *c, Node *expr, int line) {
    if (!expr) {
        ChunkWrite(&c->current->chunk, OPPUSHNULL, line);
        return;
    }
    Compiler *tmpc = CompNew();
    for (int i = 0; i < c->strcount; i++) {
        AddStr(tmpc, c->strpool[i]);
    }
    for (int i = 1; i < c->fncount; i++) {
        FnProto *srcfn = c->fns[i];
        FnProto *newproto = calloc(1, sizeof(FnProto));
        strncpy(newproto->name, srcfn->name, 127);
        strncpy(newproto->file_name, srcfn->file_name, sizeof(newproto->file_name) - 1);
        newproto->file_name[sizeof(newproto->file_name) - 1] = '\0';
        newproto->id = srcfn->id;
        newproto->paramcount = srcfn->paramcount;
        newproto->localcount = srcfn->localcount;
        newproto->scopedepth = srcfn->scopedepth;
        newproto->isasync = srcfn->isasync;
        strncpy(newproto->rettype, srcfn->rettype, 63);
        for (int p = 0; p < srcfn->paramcount; p++) {
            strncpy(newproto->paramtypes[p], srcfn->paramtypes[p], 63);
        }
        newproto->chunk.count = srcfn->chunk.count;
        newproto->chunk.cap = srcfn->chunk.cap;
        if (srcfn->chunk.count > 0) {
            newproto->chunk.code = malloc(srcfn->chunk.count);
            memcpy(newproto->chunk.code, srcfn->chunk.code, srcfn->chunk.count);
            newproto->chunk.lines = malloc(srcfn->chunk.count * sizeof(int));
            memcpy(newproto->chunk.lines, srcfn->chunk.lines, srcfn->chunk.count * sizeof(int));
        }
        if (i < tmpc->fncount && tmpc->fns[i]) {
            free(tmpc->fns[i]->chunk.code);
            free(tmpc->fns[i]->chunk.lines);
            if (tmpc->fns[i]->chunk.globalcache) free(tmpc->fns[i]->chunk.globalcache);
            if (tmpc->fns[i]->chunk.fieldcache) free(tmpc->fns[i]->chunk.fieldcache);
            if (tmpc->fns[i]->chunk.loopcounters) free(tmpc->fns[i]->chunk.loopcounters);
            free(tmpc->fns[i]);
        }
        tmpc->fns[i] = newproto;
    }
    tmpc->fncount = c->fncount;
    tmpc->iseval = 1;
    if (expr->type == NBLOCK || expr->type == NPROGRAM) {
        int sc = expr->block.count;
        for (int si = 0; si < sc - 1; si++) {
            EmitNode(tmpc, expr->block.stmts[si]);
        }
        if (sc > 0) {
            Node *last = expr->block.stmts[sc - 1];
            if (last && last->type == NEXPRSTMT) {
                EmitExpr(tmpc, last->single.expr);
            } else if (last && last->type == NRETURN) {
                if (last->single.expr) {
                    EmitExpr(tmpc, last->single.expr);
                } else {
                    ChunkWrite(&tmpc->fns[0]->chunk, OPPUSHNULL, expr->line);
                }
            } else {
                EmitNode(tmpc, last);
                ChunkWrite(&tmpc->fns[0]->chunk, OPPUSHNULL, expr->line);
            }
        } else {
            ChunkWrite(&tmpc->fns[0]->chunk, OPPUSHNULL, expr->line);
        }
    } else {
        EmitExpr(tmpc, expr);
    }
    ChunkWrite(&tmpc->fns[0]->chunk, OPHALT, expr->line);
    VM *tmpvm = VMNew();
    VMSync(tmpvm, tmpc, 0);
    VMRun(tmpvm);
    AKSValue ctferes = ValNull();
    if (tmpvm->maintask && tmpvm->maintask->stacktop > 0) {
        ctferes = ValCopy(tmpvm->maintask->stack[tmpvm->maintask->stacktop - 1]);
    }
    Node *constnode = ValToNode(ctferes, line);
    EmitExpr(c, constnode);
    NodeFree(constnode);
    ValFree(&ctferes);
    VMFree(tmpvm);
    CompFree(tmpc);
}

static void EmitExpr(Compiler *c, Node *n) {
    if(!n) return;
    Chunk *ch=&c->current->chunk;
    switch(n->type){
        case NINT:
            ChunkWrite(ch,OPPUSHINT,n->line);
            ChunkWriteI64(ch,n->ival,n->line);
            break;
        case NFLOAT:
            ChunkWrite(ch,OPPUSHFLOAT,n->line);
            ChunkWriteF64(ch,n->fval,n->line);
            break;
        case NSTR: {
            int sid=AddStr(c,n->sval);
            ChunkWrite(ch,OPPUSHSTR,n->line);
            ChunkWriteU16(ch,(uint16_t)sid,n->line);
            break;
        }
        case NBOOL:
            ChunkWrite(ch,OPPUSHBOOL,n->line);
            ChunkWrite(ch,strcmp(n->sval,"true")==0?1:0,n->line);
            break;
        case NNULL:
            ChunkWrite(ch,OPPUSHNULL,n->line);
            break;
        case NINPUT: {
            uint16_t flags = 0;
            if (n->single.expr) {
                EmitExpr(c, n->single.expr);
                flags |= 1;
            }
            ChunkWrite(ch, OPINPUTSTR, n->line);
            ChunkWriteU16(ch, flags, n->line);
            break;
        }
        case NCAST: {
            EmitExpr(c, n->cast.expr);
            if (!n->cast.targettype || strcmp(n->cast.targettype, "int") == 0) {
                ChunkWrite(ch, OPCASTINT, n->line);
            } else if (strcmp(n->cast.targettype, "float") == 0) {
                ChunkWrite(ch, OPCASTFLOAT, n->line);
            } else if (strcmp(n->cast.targettype, "bool") == 0) {
                ChunkWrite(ch, OPCASTBOOL, n->line);
            } else if (strcmp(n->cast.targettype, "string") == 0 || strcmp(n->cast.targettype, "str") == 0) {
                ChunkWrite(ch, OPCASTSTR, n->line);
            } else {
                ChunkWrite(ch, OPCASTINT, n->line);
            }
            break;
        }
        case NREF:
        case NMUTREF: {
            Node *operand = n->refnode.expr;
            if (operand && operand->type == NIDENT) {
                int slot = ResolveLocal(c, operand->sval);
                if (slot >= 0) {
                    ChunkWrite(ch, OPPUSHREF, n->line);
                    ChunkWriteU16(ch, (uint16_t)slot, n->line);
                } else {
                    EmitExpr(c, operand);
                }
            } else {
                EmitExpr(c, operand);
            }
            break;
        }
        case NIDENT: {
            int slot=ResolveLocal(c,n->sval);
            if(slot>=0){
                if (gBorrowCheckerEnabled && c->current->locals[slot].ismoved) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "UseOfMovedValueError: use of moved value '%s'", n->sval);
                    Error(c, n->line, buf);
                }
                ChunkWrite(ch,OPLOAD,n->line);
                ChunkWriteU16(ch,(uint16_t)slot,n->line);
            } else {
                int fnid=ResolveGlobal(c, n->sval, n->line);
                if(fnid>=0){
                    ChunkWrite(ch,OPPUSHFN,n->line);
                    ChunkWriteU16(ch,(uint16_t)fnid,n->line);
                } else {
                    // resolve as global variable
                    ChunkWrite(ch,OPLOADGLOBAL,n->line);
                    int gsid=AddStr(c,n->sval);
                    ChunkWriteU16(ch,(uint16_t)gsid,n->line);
                }
            }
            break;
        }
        case NBINOP: {
            const char *op=n->binop.op;
            if (op && !strcmp(op, "as")) {
                EmitExpr(c, n->binop.left);
                const char *tt = "int";
                if (n->binop.right && n->binop.right->sval) {
                    tt = n->binop.right->sval;
                }
                if (!strcmp(tt, "int")) ChunkWrite(ch, OPCASTINT, n->line);
                else if (!strcmp(tt, "float")) ChunkWrite(ch, OPCASTFLOAT, n->line);
                else if (!strcmp(tt, "bool")) ChunkWrite(ch, OPCASTBOOL, n->line);
                else if (!strcmp(tt, "string") || !strcmp(tt, "str")) ChunkWrite(ch, OPCASTSTR, n->line);
                else ChunkWrite(ch, OPCASTINT, n->line);
                break;
            }
            EmitExpr(c,n->binop.left);
            EmitExpr(c,n->binop.right);
            if(!strcmp(op,"+"))  ChunkWrite(ch,OPADD,n->line);
            else if(!strcmp(op,"-"))  ChunkWrite(ch,OPSUB,n->line);
            else if(!strcmp(op,"*"))  ChunkWrite(ch,OPMUL,n->line);
            else if(!strcmp(op,"/"))  ChunkWrite(ch,OPDIV,n->line);
            else if(!strcmp(op,"%"))  ChunkWrite(ch,OPMOD,n->line);
            else if(!strcmp(op,"==")) ChunkWrite(ch,OPEQ,n->line);
            else if(!strcmp(op,"!=")) ChunkWrite(ch,OPNEQ,n->line);
            else if(!strcmp(op,"<"))  ChunkWrite(ch,OPLT,n->line);
            else if(!strcmp(op,">"))  ChunkWrite(ch,OPGT,n->line);
            else if(!strcmp(op,"<=")) ChunkWrite(ch,OPLTE,n->line);
            else if(!strcmp(op,">=")) ChunkWrite(ch,OPGTE,n->line);
            else if(!strcmp(op,"&&")) ChunkWrite(ch,OPAND,n->line);
            else if(!strcmp(op,"||")) ChunkWrite(ch,OPOR,n->line);
            else if(!strcmp(op,"<<")) ChunkWrite(ch,OPSHL,n->line);
            else if(!strcmp(op,">>")) ChunkWrite(ch,OPSHR,n->line);
            else if(!strcmp(op,"&"))  ChunkWrite(ch,OPBITAND,n->line);
            else if(!strcmp(op,"|"))  ChunkWrite(ch,OPBITOR,n->line);
            else if(!strcmp(op,"^"))  ChunkWrite(ch,OPBITXOR,n->line);
            else if(!strcmp(op,"@")) {
                int fnid = ResolveGlobal(c, "MatMul", 0);
                if (fnid >= 0) {
                    ChunkWrite(ch, OPCALL, n->line);
                    ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                    ChunkWrite(ch, 2, n->line);
                } else {
                    int gsid = AddStr(c, "MatMul");
                    ChunkWrite(ch, OPLOADGLOBAL, n->line);
                    ChunkWriteU16(ch, (uint16_t)gsid, n->line);
                    ChunkWrite(ch, OPCALLDYNAMIC, n->line);
                    ChunkWrite(ch, 2, n->line);
                }
            }
            break;
        }
        case NTRYPROPAGATE: {
            EmitExpr(c, n->single.expr);
            char tmpvar[64];
            snprintf(tmpvar, sizeof(tmpvar), "trytmp%d", c->current->localcount);
            int slot = AddLocal(c, tmpvar, 0);
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)slot, n->line);
            ChunkWrite(ch, OPLOAD, n->line);
            ChunkWriteU16(ch, (uint16_t)slot, n->line);
            int tagfid = AddStr(c, "$tag");
            ChunkWrite(ch, OPGETFIELD, n->line);
            ChunkWriteU16(ch, (uint16_t)tagfid, n->line);
            ChunkWrite(ch, OPDUP, n->line);
            int errid = AddStr(c, "Result.Err");
            ChunkWrite(ch, OPPUSHSTR, n->line);
            ChunkWriteU16(ch, (uint16_t)errid, n->line);
            ChunkWrite(ch, OPEQ, n->line);
            ChunkWrite(ch, OPNOT, n->line);
            int errjump = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
            ChunkWrite(ch, OPDUP, n->line);
            int noneid = AddStr(c, "Option.None");
            ChunkWrite(ch, OPPUSHSTR, n->line);
            ChunkWriteU16(ch, (uint16_t)noneid, n->line);
            ChunkWrite(ch, OPEQ, n->line);
            ChunkWrite(ch, OPNOT, n->line);
            int nonejump = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
            ChunkWrite(ch, OPPOP, n->line);
            ChunkWrite(ch, OPLOAD, n->line);
            ChunkWriteU16(ch, (uint16_t)slot, n->line);
            int f0fid = AddStr(c, "field0");
            ChunkWrite(ch, OPGETFIELD, n->line);
            ChunkWriteU16(ch, (uint16_t)f0fid, n->line);
            int successjump = ChunkEmitJump(ch, OPJUMP, n->line);
            ChunkPatchJump(ch, errjump);
            ChunkPatchJump(ch, nonejump);
            ChunkWrite(ch, OPPOP, n->line);
            ChunkWrite(ch, OPLOAD, n->line);
            ChunkWriteU16(ch, (uint16_t)slot, n->line);
            for (int d = c->current->scopedepth; d >= 0; d--) {
                for (int i = 0; i < c->current->localcount; i++) {
                    Local *l = &c->current->locals[i];
                    if (l->depth == d && !l->ismoved) {
                        char dropname[256];
                        snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                        int fnid = ResolveGlobal(c, dropname, 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPLOAD, n->line);
                            ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                            ChunkWrite(ch, 1, n->line);
                            ChunkWrite(ch, OPPOP, n->line);
                        }
                    }
                }
            }
            ChunkWrite(ch, OPRETURNVAL, n->line);
            ChunkWrite(ch, OPPUSHNULL, n->line);
            ChunkPatchJump(ch, successjump);
            break;
        }
        case NUNOP: {
            if (!strcmp(n->binop.op, "&") || !strcmp(n->binop.op, "&mut")) {
                EmitExpr(c, n->binop.left);
            } else {
                EmitExpr(c, n->binop.left);
                if(!strcmp(n->binop.op,"!")) ChunkWrite(ch,OPNOT,n->line);
                else if(!strcmp(n->binop.op,"-")) ChunkWrite(ch,OPNEG,n->line);
                else if(!strcmp(n->binop.op,"~")) ChunkWrite(ch,OPBITNOT,n->line);
            }
            break;
        }
        case NASSIGN: {
            Node *target=n->assign.target;
            if (target->type == NARRLIT || target->type == NMAPLIT) {
                if (target->type == NARRLIT) {
                    EmitExpr(c, n->assign.value);
                    ChunkWrite(ch, OPDUP, n->line);
                    
                    char tmpvar[64];
                    snprintf(tmpvar, sizeof(tmpvar), "destructtmp%d", c->current->localcount);
                    int tmpslot = AddLocal(c, tmpvar, 0);
                    strcpy(c->current->locals[tmpslot].typename, "any");
                    ChunkWrite(ch, OPSTORE, n->line);
                    ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                    
                    for (int i = 0; i < target->arrlit.count; i++) {
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                        ChunkWrite(ch, OPPUSHINT, n->line);
                        ChunkWriteI64(ch, i, n->line);
                        ChunkWrite(ch, OPARRGET, n->line);
                        EmitDestructureStore(c, target->arrlit.elems[i], n->line);
                    }
                } else {
                    EmitExpr(c, n->assign.value);
                    ChunkWrite(ch, OPDUP, n->line);
                    
                    char tmpvar[64];
                    snprintf(tmpvar, sizeof(tmpvar), "destructtmp%d", c->current->localcount);
                    int tmpslot = AddLocal(c, tmpvar, 0);
                    strcpy(c->current->locals[tmpslot].typename, "any");
                    ChunkWrite(ch, OPSTORE, n->line);
                    ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                    
                    for (int i = 0; i < target->maplit.count; i++) {
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                        int fid = AddStr(c, target->maplit.keys[i]);
                        ChunkWrite(ch, OPGETFIELD, n->line);
                        ChunkWriteU16(ch, (uint16_t)fid, n->line);
                        EmitDestructureStore(c, target->maplit.vals[i], n->line);
                    }
                }
                break;
            }
            if(target->type==NIDENT){
                int slot=ResolveLocal(c,target->sval);
                if(slot>=0){
                    if (c->current->locals[slot].isconst) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "ConstError: cannot assign to immutable variable '%s'", target->sval);
                        Error(c, n->line, buf);
                    }
                    const char *expectedtype = c->current->locals[slot].typename;
                    const char *actualtype = InferExprType(c, n->assign.value);
                    if (strcmp(expectedtype, "any") == 0 && strcmp(actualtype, "any") != 0 && strcmp(actualtype, "null") != 0) {
                        strncpy(c->current->locals[slot].typename, actualtype, 63);
                        expectedtype = c->current->locals[slot].typename;
                    }
                    if (gTypeCheckingEnabled) {
                        if (!TypesCompatible(c, expectedtype, actualtype)) {
                            char buf[256];
                            snprintf(buf, sizeof(buf), "TypeError: cannot assign type '%s' to variable of type '%s'", actualtype, expectedtype);
                            AppendUnionDetails(buf, sizeof(buf), expectedtype);
                            Error(c, n->line, buf);
                        } else if (strchr(expectedtype, '{')) {
                            ChkRefConst(c, expectedtype, n->line, n->assign.value);
                        }
                    }
                    c->current->locals[slot].ismoved = 0;
                }
            }
            EmitExpr(c,n->assign.value);
            if (target->type == NIDENT) {
                int slot = ResolveLocal(c, target->sval);
                if (slot >= 0) {
                    const char *expectedtype = c->current->locals[slot].typename;
                    if (strchr(expectedtype, '{')) {
                        char basetype[128], varname[128], condstr[512];
                        if (ParseRefParts(expectedtype, basetype, varname, condstr)) {
                            if (strncmp(varname, "_narrow_", 8) == 0) {
                                strncpy(c->current->locals[slot].typename, basetype, 63);
                                expectedtype = c->current->locals[slot].typename;
                            }
                        }
                    }
                    if (strchr(expectedtype, '{')) {
                        EmitRefCheck(c, expectedtype, n->line, n->assign.value);
                    }
                    if (strchr(expectedtype, '&')) {
                        int tid = AddStr(c, expectedtype);
                        ChunkWrite(ch, OPPUSHSTR, n->line);
                        ChunkWriteU16(ch, (uint16_t)tid, n->line);
                        int fnid = ResolveGlobal(c, "CheckIntersect", 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                            ChunkWrite(ch, 2, n->line);
                        }
                    }
                }
            }
            if (n->assign.value->type == NIDENT) {
                int rslot = ResolveLocal(c, n->assign.value->sval);
                if (rslot >= 0) {
                    const char *type = c->current->locals[rslot].typename;
                    if (IsNonPrimitiveType(type)) {
                        c->current->locals[rslot].ismoved = 1;
                    }
                }
            }
            if(target->type==NIDENT){
                int slot=ResolveLocal(c,target->sval);
                if(slot>=0){
                    ChunkWrite(ch,OPDUP,n->line);
                    ChunkWrite(ch,OPSTORE,n->line);
                    ChunkWriteU16(ch,(uint16_t)slot,n->line);
                } else {
                    ChunkWrite(ch,OPDUP,n->line);
                    ChunkWrite(ch,OPSTOREGLOBAL,n->line);
                    int gsid=AddStr(c,target->sval);
                    ChunkWriteU16(ch,(uint16_t)gsid,n->line);
                }
            } else if(target->type==NINDEX){
                EmitExpr(c,target->index.arr);
                EmitExpr(c,target->index.idx);
                ChunkWrite(ch,OPARRSET,n->line);
            } else if(target->type==NMEMBER){
                EmitExpr(c,target->member.obj);
                int fid=AddStr(c,target->member.field);
                ChunkWrite(ch,OPSETFIELD,n->line);
                ChunkWriteU16(ch,(uint16_t)fid,n->line);
            }
            break;
        }
        case NCALL: {
            if (n->call.callee->type == NIDENT && strcmp(n->call.callee->sval, "ctfe") == 0) {
                if (n->call.argc != 1) {
                    Error(c, n->line, "ctfe() expects exactly 1 argument");
                    break;
                }
                RunCTFE(c, n->call.args[0], n->line);
                break;
            }
            if (n->call.callee->type == NIDENT) {
                int fnid = ResolveGlobal(c, n->call.callee->sval, n->line);
                if (fnid >= 0) {
                    FnProto *fn = c->fns[fnid];
                    if (gTypeCheckingEnabled) {
                        for (int i = 0; i < n->call.argc && i < fn->paramcount; i++) {
                            const char *expected = fn->paramtypes[i];
                            const char *actual = InferExprType(c, n->call.args[i]);
                            if (!TypesCompatible(c, expected, actual)) {
                                char buf[256];
                                snprintf(buf, sizeof(buf), "TypeError: argument %d of call to '%s' expects type '%s', got '%s'", i + 1, fn->name, expected, actual);
                                AppendUnionDetails(buf, sizeof(buf), expected);
                                Error(c, n->line, buf);
                            } else if (strchr(expected, '{')) {
                                ChkRefConst(c, expected, n->line, n->call.args[i]);
                            }
                        }
                    }
                }
            }
            int hasspread = 0;
            if (n->call.isspread) {
                for (int i = 0; i < n->call.argc; i++) {
                    if (n->call.isspread[i]) { hasspread = 1; break; }
                }
            }
            if (hasspread) {
                char tmpvar[64];
                snprintf(tmpvar, sizeof(tmpvar), "argstmp%d", c->current->localcount);
                int tmpslot = AddLocal(c, tmpvar, 0);
                strcpy(c->current->locals[tmpslot].typename, "any");
                
                ChunkWrite(ch, OPNEWARR, n->line);
                ChunkWriteU16(ch, 0, n->line);
                ChunkWrite(ch, OPSTORE, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                
                for (int i = 0; i < n->call.argc; i++) {
                    if (n->call.isspread[i]) {
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                        EmitExpr(c, n->call.args[i]);
                        int fnid = ResolveGlobal(c, "Concatenate", 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                            ChunkWrite(ch, 2, n->line);
                        } else {
                            ChunkWrite(ch, OPLOADGLOBAL, n->line);
                            int gsid = AddStr(c, "Concatenate");
                            ChunkWriteU16(ch, (uint16_t)gsid, n->line);
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, 65535, n->line);
                            ChunkWrite(ch, 2, n->line);
                        }
                        ChunkWrite(ch, OPSTORE, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                    } else {
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                        EmitExpr(c, n->call.args[i]);
                        int fnid = ResolveGlobal(c, "append", 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                            ChunkWrite(ch, 2, n->line);
                        } else {
                            ChunkWrite(ch, OPLOADGLOBAL, n->line);
                            int gsid = AddStr(c, "append");
                            ChunkWriteU16(ch, (uint16_t)gsid, n->line);
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, 65535, n->line);
                            ChunkWrite(ch, 2, n->line);
                        }
                        ChunkWrite(ch, OPPOP, n->line);
                    }
                }
                ChunkWrite(ch, OPLOAD, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                EmitExpr(c, n->call.callee);
                ChunkWrite(ch, OPAPPLY, n->line);
            } else {
                for(int i=0;i<n->call.argc;i++) EmitExpr(c,n->call.args[i]);
                int fnid = -1;
                if (n->call.callee->type == NIDENT) {
                    fnid = ResolveGlobal(c, n->call.callee->sval, n->line);
                }
                for (int i = 0; i < n->call.argc; i++) {
                    if (n->call.args[i]->type == NIDENT) {
                        int rslot = ResolveLocal(c, n->call.args[i]->sval);
                        if (rslot >= 0) {
                            const char *type = c->current->locals[rslot].typename;
                            if (IsNonPrimitiveType(type)) {
                                int shouldmove = 0;
                                if (fnid >= 0 && i < c->fns[fnid]->paramcount) {
                                    const char *ptype = c->fns[fnid]->paramtypes[i];
                                    if (ptype && ptype[0] != '\0' && ptype[0] != '&' && strcmp(ptype, "any") != 0) {
                                        shouldmove = 1;
                                    }
                                }
                                if (shouldmove) {
                                    c->current->locals[rslot].ismoved = 1;
                                }
                            }
                        }
                    }
                }
                int isadtcall = 0;
                if (n->call.callee->type == NMEMBER && n->call.callee->member.obj->type == NIDENT) {
                    for (int i = 0; i < c->enumcount; i++) {
                        if (strcmp(c->enums[i].name, n->call.callee->member.obj->sval) == 0) {
                            char constructorname[256];
                            snprintf(constructorname, sizeof(constructorname), "%s.%s", c->enums[i].name, n->call.callee->member.field);
                            int fnid = ResolveGlobal(c, constructorname, n->line);
                            if (fnid >= 0) {
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, (uint8_t)n->call.argc, n->line);
                                isadtcall = 1;
                            }
                            break;
                        }
                    }
                    if (!isadtcall) {
                        const char *objname = n->call.callee->member.obj->sval;
                        int slot = ResolveLocal(c, objname);
                        const char *tname = (slot >= 0) ? c->current->locals[slot].typename : objname;
                        if (slot >= 0 && (strcmp(tname, "any") == 0 || strcmp(tname, "") == 0) && strcmp(objname, "self") == 0 && c->current && strchr(c->current->name, '.')) {
                            static char self_type_buf[128];
                            const char *dot = strchr(c->current->name, '.');
                            size_t tlen = (size_t)(dot - c->current->name);
                            if (tlen < sizeof(self_type_buf)) {
                                strncpy(self_type_buf, c->current->name, tlen);
                                self_type_buf[tlen] = '\0';
                                tname = self_type_buf;
                            }
                        }
                        char fullname[512];
                        snprintf(fullname, sizeof(fullname), "%s.%s", tname, n->call.callee->member.field);
                        int fnid = ResolveGlobal(c, fullname, n->line);
                        if (fnid >= 0) {
                            if (slot >= 0) {
                                EmitExpr(c, n->call.callee->member.obj);
                                for (int a = 0; a < n->call.argc; a++) {
                                    EmitExpr(c, n->call.args[a]);
                                }
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, (uint8_t)(n->call.argc + 1), n->line);
                            } else {
                                for (int a = 0; a < n->call.argc; a++) {
                                    EmitExpr(c, n->call.args[a]);
                                }
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, (uint8_t)n->call.argc, n->line);
                            }
                            isadtcall = 1;
                        }
                    }
                }
                if (isadtcall) {
                } else if(n->call.callee->type==NIDENT){
                    int slot=ResolveLocal(c,n->call.callee->sval);
                    if(slot>=0){
                        ChunkWrite(ch,OPLOAD,n->line);
                        ChunkWriteU16(ch,(uint16_t)slot,n->line);
                        ChunkWrite(ch,OPCALLDYNAMIC,n->line);
                        ChunkWrite(ch,(uint8_t)n->call.argc,n->line);
                    } else {
                        int fnid=ResolveGlobal(c, n->call.callee->sval, n->line);
                        if(fnid >= 0){
                            ChunkWrite(ch,OPCALL,n->line);
                            ChunkWriteU16(ch,(uint16_t)fnid,n->line);
                            ChunkWrite(ch,(uint8_t)n->call.argc,n->line);
                        } else {
                            ChunkWrite(ch,OPLOADGLOBAL,n->line);
                            int gsid=AddStr(c,n->call.callee->sval);
                            ChunkWriteU16(ch,(uint16_t)gsid,n->line);
                            ChunkWrite(ch,OPCALLDYNAMIC,n->line);
                            ChunkWrite(ch,(uint8_t)n->call.argc,n->line);
                        }
                    }
                } else {
                    EmitExpr(c,n->call.callee);
                    ChunkWrite(ch,OPCALLDYNAMIC,n->line);
                    ChunkWrite(ch,(uint8_t)n->call.argc,n->line);
                }
            }
            break;
        }
        case NMACROCALL: {
            if (n->call.callee->type != NIDENT) {
                Error(c, n->line, "Macro callee must be an identifier");
                break;
            }
            
            if (strcmp(n->call.callee->sval, "proc_macro") == 0) {
                if (n->call.argc != 2) {
                    Error(c, n->line, "proc_macro() expects exactly 2 arguments");
                    break;
                }
                Node *fn_ident = n->call.args[0];
                Node *input_expr = n->call.args[1];
                if (fn_ident->type != NIDENT) {
                    Error(c, n->line, "First argument to proc_macro() must be a function identifier");
                    break;
                }
                
                Compiler *tmpc = CompNew();
                for (int i = 0; i < c->strcount; i++) {
                    AddStr(tmpc, c->strpool[i]);
                }
                for (int i = 1; i < c->fncount; i++) {
                    FnProto *srcfn = c->fns[i];
                    FnProto *newproto = calloc(1, sizeof(FnProto));
                    strncpy(newproto->name, srcfn->name, 127);
                    strncpy(newproto->file_name, srcfn->file_name, sizeof(newproto->file_name) - 1);
                    newproto->file_name[sizeof(newproto->file_name) - 1] = '\0';
                    newproto->id = srcfn->id;
                    newproto->paramcount = srcfn->paramcount;
                    newproto->localcount = srcfn->localcount;
                    newproto->scopedepth = srcfn->scopedepth;
                    newproto->isasync = srcfn->isasync;
                    strncpy(newproto->rettype, srcfn->rettype, 63);
                    for (int p = 0; p < srcfn->paramcount; p++) {
                        strncpy(newproto->paramtypes[p], srcfn->paramtypes[p], 63);
                    }
                    newproto->chunk.count = srcfn->chunk.count;
                    newproto->chunk.cap = srcfn->chunk.cap;
                    if (srcfn->chunk.count > 0) {
                        newproto->chunk.code = malloc(srcfn->chunk.count);
                        memcpy(newproto->chunk.code, srcfn->chunk.code, srcfn->chunk.count);
                        newproto->chunk.lines = malloc(srcfn->chunk.count * sizeof(int));
                        memcpy(newproto->chunk.lines, srcfn->chunk.lines, srcfn->chunk.count * sizeof(int));
                    }
                    tmpc->fns[i] = newproto;
                }
                tmpc->fncount = c->fncount;
                EmitExpr(tmpc, input_expr);
                ChunkWrite(&tmpc->fns[0]->chunk, OPHALT, input_expr->line);
                VM *tmpvm = VMNew();
                VMSync(tmpvm, tmpc, 0);
                VMRun(tmpvm);
                
                AKSValue input_val = ValNull();
                if (tmpvm->maintask && tmpvm->maintask->stacktop > 0) {
                    input_val = ValCopy(tmpvm->maintask->stack[tmpvm->maintask->stacktop - 1]);
                }
                VMFree(tmpvm);
                CompFree(tmpc);
                
                if (ValKind(input_val) != VALSTR) {
                    Error(c, n->line, "Second argument to proc_macro() must evaluate to a string");
                    ValFree(&input_val);
                    break;
                }
                
                tmpc = CompNew();
                for (int i = 0; i < c->strcount; i++) AddStr(tmpc, c->strpool[i]);
                for (int i = 1; i < c->fncount; i++) {
                    FnProto *srcfn = c->fns[i];
                    FnProto *newproto = calloc(1, sizeof(FnProto));
                    strncpy(newproto->name, srcfn->name, 127);
                    strncpy(newproto->file_name, srcfn->file_name, sizeof(newproto->file_name) - 1);
                    newproto->file_name[sizeof(newproto->file_name) - 1] = '\0';
                    newproto->id = srcfn->id;
                    newproto->paramcount = srcfn->paramcount;
                    newproto->localcount = srcfn->localcount;
                    newproto->scopedepth = srcfn->scopedepth;
                    newproto->isasync = srcfn->isasync;
                    strncpy(newproto->rettype, srcfn->rettype, 63);
                    for (int p = 0; p < srcfn->paramcount; p++) {
                        strncpy(newproto->paramtypes[p], srcfn->paramtypes[p], 63);
                    }
                    newproto->chunk.count = srcfn->chunk.count;
                    newproto->chunk.cap = srcfn->chunk.cap;
                    if (srcfn->chunk.count > 0) {
                        newproto->chunk.code = malloc(srcfn->chunk.count);
                        memcpy(newproto->chunk.code, srcfn->chunk.code, srcfn->chunk.count);
                        newproto->chunk.lines = malloc(srcfn->chunk.count * sizeof(int));
                        memcpy(newproto->chunk.lines, srcfn->chunk.lines, srcfn->chunk.count * sizeof(int));
                    }
                    tmpc->fns[i] = newproto;
                }
                tmpc->fncount = c->fncount;
                
                Node *macro_call = NodeNew(NCALL, n->line);
                macro_call->call.callee = NodeCopy(fn_ident);
                macro_call->call.argc = 1;
                macro_call->call.args = malloc(sizeof(Node *));
                macro_call->call.args[0] = ValToNode(input_val, n->line);
                macro_call->call.isspread = calloc(1, sizeof(int));
                macro_call->call.targs = NULL;
                macro_call->call.tc = 0;
                
                EmitExpr(tmpc, macro_call);
                ChunkWrite(&tmpc->fns[0]->chunk, OPHALT, n->line);
                
                tmpvm = VMNew();
                VMSync(tmpvm, tmpc, 0);
                VMRun(tmpvm);
                
                AKSValue macro_res = ValNull();
                if (tmpvm->maintask && tmpvm->maintask->stacktop > 0) {
                    macro_res = ValCopy(tmpvm->maintask->stack[tmpvm->maintask->stacktop - 1]);
                }
                
                NodeFree(macro_call);
                ValFree(&input_val);
                VMFree(tmpvm);
                CompFree(tmpc);
                
                if (ValKind(macro_res) != VALSTR) {
                    Error(c, n->line, "Procedural macro function must return a string of code");
                    ValFree(&macro_res);
                    break;
                }
                
                const char *code_str = ValToStrPtr(macro_res)->data;
                int parser_err = 0;
                extern Node *Parse(const char *source, int *haderror);
                Node *parsed_ast = Parse(code_str, &parser_err);
                ValFree(&macro_res);
                if (parser_err || !parsed_ast) {
                    Error(c, n->line, "Failed to parse code generated by procedural macro");
                    break;
                }
                
                EmitNode(c, parsed_ast);
                NodeFree(parsed_ast);
                break;
            }
            
            int found = -1;
            for (int i = 0; i < c->macrocount; i++) {
                if (strcmp(c->macros[i].name, n->call.callee->sval) == 0 && c->macros[i].pc == n->call.argc) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                char buf[256];
                snprintf(buf, sizeof(buf), "Macro '%s' with %d arguments not found", n->call.callee->sval, n->call.argc);
                Error(c, n->line, buf);
                break;
            }
            MacroDecl *macro = &c->macros[found];
            Node *expanded = ASTMacroExpand(macro->body, macro->pnames, n->call.args, macro->pc);
            EmitExpr(c, expanded);
            NodeFree(expanded);
            break;
        }
        case NTEMPLATECALL: {
            if (n->call.callee->type != NIDENT) {
                Error(c, n->line, "Template callee must be an identifier");
                break;
            }
            int found = -1;
            for (int i = 0; i < c->templatecount; i++) {
                if (strcmp(c->templates[i].name, n->call.callee->sval) == 0) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                char buf[256];
                snprintf(buf, sizeof(buf), "Template function '%s' not found", n->call.callee->sval);
                Error(c, n->line, buf);
                break;
            }
            TemplateDecl *tmpl = &c->templates[found];
            if (n->call.tc != tmpl->ast->fn.tpc) {
                char buf[256];
                snprintf(buf, sizeof(buf), "Template function '%s' expects %d type arguments, got %d",
                         tmpl->name, tmpl->ast->fn.tpc, n->call.tc);
                Error(c, n->line, buf);
                break;
            }
            char mangledname[256];
            snprintf(mangledname, sizeof(mangledname), "%s", tmpl->name);
            for (int i = 0; i < n->call.tc; i++) {
                char cleantarg[64];
                snprintf(cleantarg, sizeof(cleantarg), "_%s", n->call.targs[i]);
                strncat(mangledname, cleantarg, sizeof(mangledname) - strlen(mangledname) - 1);
            }
            int fnid = ResolveGlobal(c, mangledname, n->line);
            if (fnid < 0) {
                Node *inst = NodeCopy(tmpl->ast);
                free(inst->fn.name);
                inst->fn.name = strdup(mangledname);
                TypeSubstitute(inst, tmpl->ast->fn.tparams, n->call.targs, n->call.tc);
                if (inst->fn.tparams) {
                    for (int i = 0; i < tmpl->ast->fn.tpc; i++) free(inst->fn.tparams[i]);
                    free(inst->fn.tparams);
                    inst->fn.tparams = NULL;
                }
                inst->fn.tpc = 0;
                PreregisterFns(c, inst);
                EmitNode(c, inst);
                NodeFree(inst);
            }
            Node *oldcallee = n->call.callee;
            n->call.callee = NodeNew(NIDENT, n->line);
            n->call.callee->sval = strdup(mangledname);
            n->type = NCALL;
            EmitExpr(c, n);
            NodeFree(n->call.callee);
            n->call.callee = oldcallee;
            n->type = NTEMPLATECALL;
            break;
        }
        case NTUPLE: {
            for(int i=0;i<n->tuple.count;i++) EmitExpr(c,n->tuple.elems[i]);
            ChunkWrite(ch,OPNEWTUPLE,n->line);
            uint16_t countval = (uint16_t)n->tuple.count;
            if (n->isstack) countval |= 0x8000;
            ChunkWriteU16(ch,countval,n->line);
            break;
        }
        case NARRLIT: {
            int hasspread = 0;
            if (n->arrlit.isspread) {
                for (int i = 0; i < n->arrlit.count; i++) {
                    if (n->arrlit.isspread[i]) { hasspread = 1; break; }
                }
            }
            if (!hasspread) {
                for(int i=0;i<n->arrlit.count;i++) EmitExpr(c,n->arrlit.elems[i]);
                ChunkWrite(ch,OPNEWARR,n->line);
                uint16_t countval = (uint16_t)n->arrlit.count;
                if (n->isstack) countval |= 0x8000;
                ChunkWriteU16(ch,countval,n->line);
            } else {
                char tmpvar[64];
                snprintf(tmpvar, sizeof(tmpvar), "arrtmp%d", c->current->localcount);
                int tmpslot = AddLocal(c, tmpvar, 0);
                strcpy(c->current->locals[tmpslot].typename, "any");
                
                ChunkWrite(ch, OPNEWARR, n->line);
                uint16_t countval = 0;
                if (n->isstack) countval |= 0x8000;
                ChunkWriteU16(ch, countval, n->line);
                ChunkWrite(ch, OPSTORE, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                
                for (int i = 0; i < n->arrlit.count; i++) {
                    if (n->arrlit.isspread[i]) {
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                        EmitExpr(c, n->arrlit.elems[i]);
                        int fnid = ResolveGlobal(c, "Concatenate", 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                            ChunkWrite(ch, 2, n->line);
                        } else {
                            ChunkWrite(ch, OPLOADGLOBAL, n->line);
                            int gsid = AddStr(c, "Concatenate");
                            ChunkWriteU16(ch, (uint16_t)gsid, n->line);
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, 65535, n->line);
                            ChunkWrite(ch, 2, n->line);
                        }
                        ChunkWrite(ch, OPSTORE, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                    } else {
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                        EmitExpr(c, n->arrlit.elems[i]);
                        int fnid = ResolveGlobal(c, "append", 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                            ChunkWrite(ch, 2, n->line);
                        } else {
                            ChunkWrite(ch, OPLOADGLOBAL, n->line);
                            int gsid = AddStr(c, "append");
                            ChunkWriteU16(ch, (uint16_t)gsid, n->line);
                            ChunkWrite(ch, OPCALL, n->line);
                            ChunkWriteU16(ch, 65535, n->line);
                            ChunkWrite(ch, 2, n->line);
                        }
                        ChunkWrite(ch, OPPOP, n->line);
                    }
                }
                ChunkWrite(ch, OPLOAD, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            }
            break;
        }
        case NMAPLIT: {
            char tmpvar[64];
            snprintf(tmpvar, sizeof(tmpvar), "maptmp%d", c->current->localcount);
            int tmpslot = AddLocal(c, tmpvar, 0);
            strcpy(c->current->locals[tmpslot].typename, "any");
            
            int tid = AddStr(c, "Object");
            ChunkWrite(ch, OPNEWSTRUCT, n->line);
            uint16_t structval = (uint16_t)tid;
            if (n->isstack) structval |= 0x8000;
            ChunkWriteU16(ch, structval, n->line);
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            
            for (int i = 0; i < n->maplit.count; i++) {
                EmitExpr(c, n->maplit.vals[i]);
                ChunkWrite(ch, OPLOAD, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                int fid = AddStr(c, n->maplit.keys[i]);
                ChunkWrite(ch, OPSETFIELD, n->line);
                ChunkWriteU16(ch, (uint16_t)fid, n->line);
                ChunkWrite(ch, OPPOP, n->line);
            }
            ChunkWrite(ch, OPLOAD, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            break;
        }
        case NINDEX: {
            if (n->index.idx->type == NRANGE) {
                EmitExpr(c, n->index.arr);
                EmitExpr(c, n->index.idx->range.start);
                EmitExpr(c, n->index.idx->range.end);
                int fnid = ResolveGlobal(c, "slice", 0);
                if (fnid >= 0) {
                    ChunkWrite(ch, OPCALL, n->line);
                    ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                    ChunkWrite(ch, 3, n->line);
                } else {
                    Error(c, n->line, "Compiler internal error: slice helper not found");
                }
            } else {
                EmitExpr(c,n->index.arr);
                EmitExpr(c,n->index.idx);
                ChunkWrite(ch,OPARRGET,n->line);
            }
            break;
        }
        case NMEMBER: {
            int isenum = 0;
            if (n->member.obj->type == NIDENT) {
                for (int i = 0; i < c->enumcount; i++) {
                    if (strcmp(c->enums[i].name, n->member.obj->sval) == 0) {
                        isenum = 1;
                        int varidx = -1;
                        for (int v = 0; v < c->enums[i].count; v++) {
                            if (strcmp(c->enums[i].members[v], n->member.field) == 0) {
                                varidx = v;
                                break;
                            }
                        }
                        if (varidx >= 0) {
                            if (c->enums[i].isadt) {
                                char variantfullname[256];
                                snprintf(variantfullname, sizeof(variantfullname), "%s.%s", c->enums[i].name, c->enums[i].members[varidx]);
                                int fnid = ResolveGlobal(c, variantfullname, 0);
                                if (fnid >= 0) {
                                    if (c->fns[fnid]->paramcount == 0) {
                                        ChunkWrite(ch, OPCALL, n->line);
                                        ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                        ChunkWrite(ch, 0, n->line);
                                    } else {
                                        ChunkWrite(ch, OPPUSHFN, n->line);
                                        ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                    }
                                } else {
                                    Error(c, n->line, "Could not resolve ADT variant constructor");
                                }
                            } else {
                                int64_t val = c->enums[i].values[varidx];
                                ChunkWrite(ch, OPPUSHINT, n->line);
                                ChunkWriteI64(ch, val, n->line);
                            }
                        } else {
                            Error(c, n->line, "Enum does not contain variant");
                        }
                        break;
                    }
                }
            }
            int isglobalfn = 0;
            if (!isenum && n->member.obj->type == NIDENT) {
                char fullname[512];
                snprintf(fullname, sizeof(fullname), "%s.%s", n->member.obj->sval, n->member.field);
                int fnid = ResolveGlobal(c, fullname, n->line);
                if (fnid >= 0) {
                    ChunkWrite(ch, OPPUSHFN, n->line);
                    ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                    isglobalfn = 1;
                }
            }
            if (!isenum && !isglobalfn) {
                EmitExpr(c,n->member.obj);
                int fid=AddStr(c,n->member.field);
                ChunkWrite(ch,OPGETFIELD,n->line);
                ChunkWriteU16(ch,(uint16_t)fid,n->line);
            }
            break;
        }
        case NINC: {
            if(n->incdec.expr->type==NIDENT){
                int slot=ResolveLocal(c,n->incdec.expr->sval);
                if(slot>=0){
                    ChunkWrite(ch,OPLOAD,n->line);
                    ChunkWriteU16(ch,(uint16_t)slot,n->line);
                    ChunkWrite(ch,OPDUP,n->line);
                    ChunkWrite(ch,OPINC,n->line);
                    ChunkWrite(ch,OPSTORE,n->line);
                    ChunkWriteU16(ch,(uint16_t)slot,n->line);
                }
            }
            break;
        }
        case NDEC: {
            if(n->incdec.expr->type==NIDENT){
                int slot=ResolveLocal(c,n->incdec.expr->sval);
                if(slot>=0){
                    ChunkWrite(ch,OPLOAD,n->line);
                    ChunkWriteU16(ch,(uint16_t)slot,n->line);
                    ChunkWrite(ch,OPDUP,n->line);
                    ChunkWrite(ch,OPDEC,n->line);
                    ChunkWrite(ch,OPSTORE,n->line);
                    ChunkWriteU16(ch,(uint16_t)slot,n->line);
                }
            }
            break;
        }
        case NNEW: {
            char mangledname[256];
            if (n->newobj.tc > 0) {
                int len = snprintf(mangledname, sizeof(mangledname), "%s", n->newobj.name);
                for (int i = 0; i < n->newobj.tc; i++) {
                    char argmangled[128];
                    MangleTypeName(n->newobj.targs[i], argmangled, sizeof(argmangled));
                    len += snprintf(mangledname + len, sizeof(mangledname) - len, "_%s", argmangled);
                }
                
                int templateidx = -1;
                for (int i = 0; i < c->templatecount; i++) {
                    if (strcmp(c->templates[i].name, n->newobj.name) == 0) {
                        templateidx = i;
                        break;
                    }
                }
                
                if (templateidx >= 0) {
                    int instcount = 0;
                    size_t tmpllen = strlen(c->templates[templateidx].name);
                    for (int i = 0; i < c->structcount; i++) {
                        if (strncmp(c->structs[i].name, c->templates[templateidx].name, tmpllen) == 0 &&
                            c->structs[i].name[tmpllen] == '_') {
                            instcount++;
                        }
                    }
                    if (instcount >= 16) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Warning: template '%s' instantiated with more than 16 different type arguments", c->templates[templateidx].name);
                        Warning(c, n->line, buf);
                    }

                    int alreadycompiled = 0;
                    for (int i = 0; i < c->structcount; i++) {
                        if (strcmp(c->structs[i].name, mangledname) == 0) {
                            alreadycompiled = 1;
                            break;
                        }
                    }
                    
                    if (!alreadycompiled) {
                        Node *templateast = c->templates[templateidx].ast;
                        
                        for (int i = 0; i < c->templates[templateidx].constraintcount; i++) {
                            const char *paramname = c->templates[templateidx].constraintparams[i];
                            const char *boundtype = c->templates[templateidx].constrainttypes[i];
                            
                            int targidx = -1;
                            for (int j = 0; j < templateast->strct.tpc; j++) {
                                if (strcmp(templateast->strct.tparams[j], paramname) == 0) {
                                    targidx = j;
                                    break;
                                }
                            }
                            if (targidx >= 0 && targidx < n->newobj.tc) {
                                const char *targtype = n->newobj.targs[targidx];
                                char *boundsubstituted = strdup(boundtype);
                                SubstituteTypes(&boundsubstituted, templateast->strct.tparams, templateast->strct.tpc, n->newobj.targs, n->newobj.tc);
                                
                                if (!TypesCompatible(c, boundsubstituted, targtype)) {
                                    char buf[512];
                                    snprintf(buf, sizeof(buf), "TypeError: type '%s' does not satisfy constraint '%s' for parameter '%s'", targtype, boundsubstituted, paramname);
                                    Error(c, n->line, buf);
                                }
                                free(boundsubstituted);
                            }
                        }

                        Node *concrete = NodeCopy(templateast);
                        
                        free(concrete->strct.name);
                        concrete->strct.name = strdup(mangledname);
                        concrete->strct.tpc = 0;
                        if (concrete->strct.tparams) {
                            for (int i = 0; i < templateast->strct.tpc; i++) {
                                free(concrete->strct.tparams[i]);
                            }
                            free(concrete->strct.tparams);
                            concrete->strct.tparams = NULL;
                        }
                        
                        for (int j = 0; j < concrete->strct.fc; j++) {
                            SubstituteTypes(&concrete->strct.ftypes[j], templateast->strct.tparams, templateast->strct.tpc, n->newobj.targs, n->newobj.tc);
                        }
                        
                        EmitNode(c, concrete);
                        NodeFree(concrete);
                    }
                }
            } else {
                strncpy(mangledname, n->newobj.name, sizeof(mangledname) - 1);
                mangledname[sizeof(mangledname) - 1] = '\0';
            }
            
            int tid = AddStr(c, mangledname);
            ChunkWrite(ch, OPNEWSTRUCT, n->line);
            uint16_t structval = (uint16_t)tid;
            if (n->isstack) structval |= 0x8000;
            ChunkWriteU16(ch, structval, n->line);
            
            int structidx = -1;
            for (int i = 0; i < c->structcount; i++) {
                if (strcmp(c->structs[i].name, mangledname) == 0) {
                    structidx = i;
                    break;
                }
            }
            if (structidx >= 0) {
                CompilerStruct *s = &c->structs[structidx];
                if (s->ic > 0 || s->fc > 0) {
                    int tmpslot = ResolveLocal(c, "newstructtmp");
                    if (tmpslot < 0) {
                        tmpslot = AddLocal(c, "newstructtmp", 0);
                    }
                    ChunkWrite(ch, OPSTORE, n->line);
                    ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                    
                    if (s->ic > 0) {
                        for (int i = 0; i < s->ic; i++) {
                            int sid = AddStr(c, s->interfaces[i]);
                            ChunkWrite(ch, OPPUSHSTR, n->line);
                            ChunkWriteU16(ch, (uint16_t)sid, n->line);
                        }
                        ChunkWrite(ch, OPNEWARR, n->line);
                        ChunkWriteU16(ch, (uint16_t)s->ic, n->line);
                        
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                        
                        int fid = AddStr(c, "$interfaces");
                        ChunkWrite(ch, OPSETFIELD, n->line);
                        ChunkWriteU16(ch, (uint16_t)fid, n->line);
                        ChunkWrite(ch, OPPOP, n->line);
                    }
                    
                    for (int j = 0; j < s->fc; j++) {
                        char defaultfnname[256];
                        snprintf(defaultfnname, sizeof(defaultfnname), "%s.%s", s->name, s->fnames[j]);
                        int fnid = ResolveGlobal(c, defaultfnname, 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPPUSHFN, n->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                            
                            ChunkWrite(ch, OPLOAD, n->line);
                            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                            
                            int fid = AddStr(c, s->fnames[j]);
                            ChunkWrite(ch, OPSETFIELD, n->line);
                            ChunkWriteU16(ch, (uint16_t)fid, n->line);
                            ChunkWrite(ch, OPPOP, n->line);
                        }
                    }
                    
                    ChunkWrite(ch, OPLOAD, n->line);
                    ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                }
            }
            break;
        }
        case NCOALESCE: {
            EmitExpr(c, n->coalesce.left);
            int jumpoffset = ChunkEmitJump(ch, OPCOALESCE, n->line);
            EmitExpr(c, n->coalesce.right);
            ChunkPatchJump(ch, jumpoffset);
            break;
        }
        case NOPTCHAIN: {
            EmitExpr(c, n->optchain.obj);
            int nulljump = ChunkEmitJump(ch, OPJUMPIFNULL, n->line);
            if (n->optchain.field) {
                int fid = AddStr(c, n->optchain.field);
                ChunkWrite(ch, OPGETFIELD, n->line);
                ChunkWriteU16(ch, (uint16_t)fid, n->line);
            } else {
                EmitExpr(c, n->optchain.idx);
                ChunkWrite(ch, OPARRGET, n->line);
            }
            ChunkPatchJump(ch, nulljump);
            break;
        }
        case NRANGE: {
            EmitExpr(c, n->range.start);
            EmitExpr(c, n->range.end);
            ChunkWrite(ch, OPRANGE, n->line);
            break;
        }
        case NAWAIT: {
            EmitExpr(c, n->awaitn.expr);
            if (n->awaitn.timeout) {
                EmitExpr(c, n->awaitn.timeout);
            } else {
                ChunkWrite(ch, OPPUSHNULL, n->line);
            }
            ChunkWrite(ch, OPAWAIT, n->line);
            break;
        }
        case NSPAWN: {
            Node *call = n->spawnn.expr;
            if (call->type == NCALL) {
                for (int i = 0; i < call->call.argc; i++) EmitExpr(c, call->call.args[i]);
                if (call->call.callee->type == NIDENT) {
                    int slot = ResolveLocal(c, call->call.callee->sval);
                    if (slot >= 0) {
                        ChunkWrite(ch, OPLOAD, call->line);
                        ChunkWriteU16(ch, (uint16_t)slot, call->line);
                        ChunkWrite(ch, OPSPAWN, call->line);
                        ChunkWriteU16(ch, 65535, call->line);
                        ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                    } else {
                        int fnid = ResolveGlobal(c, call->call.callee->sval, 0);
                        if (fnid >= 0) {
                            ChunkWrite(ch, OPSPAWN, call->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, call->line);
                            ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                        } else {
                            ChunkWrite(ch, OPLOADGLOBAL, call->line);
                            int gsid = AddStr(c, call->call.callee->sval);
                            ChunkWriteU16(ch, (uint16_t)gsid, call->line);
                            ChunkWrite(ch, OPSPAWN, call->line);
                            ChunkWriteU16(ch, 65535, call->line);
                            ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                        }
                    }
                } else {
                    EmitExpr(c, call->call.callee);
                    ChunkWrite(ch, OPSPAWN, call->line);
                    ChunkWriteU16(ch, 65535, call->line);
                    ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                }
            } else {
                Error(c, n->line, "spawn must be followed by a function call");
            }
            break;
        }
        case NPERFORM: {
            char full_op[256];
            if (n->performn.effectname) {
                snprintf(full_op, sizeof(full_op), "%s.%s", n->performn.effectname, n->performn.opname);
            } else {
                snprintf(full_op, sizeof(full_op), "%s", n->performn.opname);
            }
            int strid = AddStr(c, full_op);
            ChunkWrite(ch, OPPUSHSTR, n->line);
            ChunkWriteU16(ch, (uint16_t)strid, n->line);
            
            for (int i = 0; i < n->performn.argcount; i++) {
                EmitExpr(c, n->performn.args[i]);
            }
            ChunkWrite(ch, OPNEWARR, n->line);
            ChunkWriteU16(ch, (uint16_t)n->performn.argcount, n->line);
            
            int fnid = ResolveGlobal(c, "EffectPerform", 0);
            if (fnid < 0) fnid = NewFn(c, "EffectPerform")->id;
            ChunkWrite(ch, OPCALL, n->line);
            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
            ChunkWrite(ch, 2, n->line);
            break;
        }
        case NHANDLE: {
            FnProto *parent = c->current;
            static int thunkCount = 0;
            char thunkName[64];
            snprintf(thunkName, sizeof(thunkName), "__handle_thunk_%d", thunkCount++);
            FnProto *thunk = NewFn(c, thunkName);
            thunk->paramcount = 0;
            c->current = thunk;
            EmitExpr(c, n->handlen.body);
            ChunkWrite(&thunk->chunk, OPRETURNVAL, n->line);
            c->current = parent;
            
            char hmap_var[64];
            snprintf(hmap_var, sizeof(hmap_var), "__hmap_%d", c->current->localcount);
            int tmpslot = AddLocal(c, hmap_var, 0);
            
            int map_sid = AddStr(c, "EffectHandlerMap");
            ChunkWrite(ch, OPNEWSTRUCT, n->line);
            ChunkWriteU16(ch, (uint16_t)map_sid, n->line);
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            
            for (int i = 0; i < n->handlen.count; i++) {
                int fsid = AddStr(c, n->handlen.opnames[i]);
                EmitExpr(c, n->handlen.handlers[i]);
                ChunkWrite(ch, OPLOAD, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                ChunkWrite(ch, OPSETFIELD, n->line);
                ChunkWriteU16(ch, (uint16_t)fsid, n->line);
                ChunkWrite(ch, OPPOP, n->line);
            }
            
            ChunkWrite(ch, OPPUSHFN, n->line);
            ChunkWriteU16(ch, (uint16_t)thunk->id, n->line);
            ChunkWrite(ch, OPLOAD, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            
            int fnid = ResolveGlobal(c, "EffectHandle", 0);
            if (fnid < 0) fnid = NewFn(c, "EffectHandle")->id;
            ChunkWrite(ch, OPCALL, n->line);
            ChunkWriteU16(ch, (uint16_t)fnid, n->line);
            ChunkWrite(ch, 2, n->line);
            break;
        }
        case NRESUME: {
            if (n->resumen.expr) {
                EmitExpr(c, n->resumen.expr);
            } else {
                ChunkWrite(ch, OPPUSHNULL, n->line);
            }
            break;
        }
        case NATOMIC: {
            int retry_label = ch->count;
            int fn_begin = ResolveGlobal(c, "AtomicBegin", 0);
            if (fn_begin >= 0) {
                ChunkWrite(ch, OPCALL, n->line);
                ChunkWriteU16(ch, (uint16_t)fn_begin, n->line);
                ChunkWrite(ch, 0, n->line);
                ChunkWrite(ch, OPPOP, n->line);
            }
            
            if (n->atomicn.body && n->atomicn.body->type == NBLOCK) {
                EmitNode(c, n->atomicn.body);
            } else if (n->atomicn.body) {
                EmitExpr(c, n->atomicn.body);
                ChunkWrite(ch, OPPOP, n->line);
            }
            
            int fn_commit = ResolveGlobal(c, "AtomicCommit", 0);
            if (fn_commit >= 0) {
                ChunkWrite(ch, OPCALL, n->line);
                ChunkWriteU16(ch, (uint16_t)fn_commit, n->line);
                ChunkWrite(ch, 0, n->line);
            } else {
                ChunkWrite(ch, OPPUSHBOOL, n->line);
                ChunkWrite(ch, 1, n->line);
            }
            
            int commit_ok_jump = ChunkEmitJump(ch, OPJUMP, n->line);
            int retry_jump = ChunkEmitJump(ch, OPJUMP, n->line);
            
            // Rewrite retry logic: if commit returns false, jump back to retry_label
            ChunkPatchJump(ch, commit_ok_jump);
            // check top of stack bool
            int pass_jump = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
            // If commit was true: jump over backward jump
            int done_jump = ChunkEmitJump(ch, OPJUMP, n->line);
            
            // If commit was false: loop back
            ChunkPatchJump(ch, pass_jump);
            ChunkWrite(ch, OPJUMP, n->line);
            int32_t back = (int32_t)(retry_label - ch->count - 4);
            ChunkWriteI32(ch, back, n->line);
            
            ChunkPatchJump(ch, done_jump);
            ChunkWrite(ch, OPPUSHNULL, n->line);
            break;
        }
        case NMACRODECL: {
            int m_idx = -1;
            for (int i = 0; i < c->macrocount; i++) {
                if (strcmp(c->macros[i].name, n->fn.name) == 0 && c->macros[i].pc == n->fn.pc) {
                    m_idx = i;
                    break;
                }
            }
            MacroDecl *m;
            if (m_idx >= 0) {
                m = &c->macros[m_idx];
                NodeFree(m->body);
                for (int p = 0; p < m->pc; p++) free(m->pnames[p]);
                free(m->pnames);
            } else {
                if (c->macrocount >= 128) {
                    Error(c, n->line, "Too many macros registered");
                    break;
                }
                m = &c->macros[c->macrocount++];
            }
            strncpy(m->name, n->fn.name, 127);
            m->body = NodeCopy(n->fn.body);
            m->pc = n->fn.pc;
            m->pnames = malloc(n->fn.pc * sizeof(char*));
            for (int i = 0; i < n->fn.pc; i++) {
                m->pnames[i] = strdup(n->fn.pnames[i]);
            }
            break;
        }
        case NFNDECL: {
            extern int gContractCheck;
            if (n->fn.isgen) {
                DesugArgEn(c, n);
            }
            if (n->fn.tpc > 0) {
                if (c->templatecount >= 128) {
                    Error(c, n->line, "Too many templates registered");
                    break;
                }
                TemplateDecl *t = &c->templates[c->templatecount++];
                strncpy(t->name, n->fn.name, 127);
                t->ast = NodeCopy(n);
                break;
            }
            VerifyFnContracts(c, n);
            if (!n->fn.name) {
                static int anonFnCount = 0;
                char anonName[64];
                snprintf(anonName, sizeof(anonName), "__anon_fn_%d", anonFnCount++);
                n->fn.name = strdup(anonName);
            }
            FnProto *parent = c->current;
            FnProto *fn = NULL;
            for (int i = 0; i < c->fncount; i++) {
                if (c->fns[i] && c->fns[i]->name && strcmp(c->fns[i]->name, n->fn.name) == 0) {
                    fn = c->fns[i];
                    break;
                }
            }
            if (!fn) fn = NewFn(c, n->fn.name);
            if (!fn) break;
            fn->ast = n;
            fn->paramcount = n->fn.pc;
            fn->isasync = n->fn.isasync;
            if (n->fn.ret) strncpy(fn->rettype, n->fn.ret, 63);
            else strcpy(fn->rettype, "any");
            for (int i = 0; i < n->fn.pc; i++) {
                if (n->fn.ptypes[i]) strncpy(fn->paramtypes[i], n->fn.ptypes[i], 63);
                else strcpy(fn->paramtypes[i], "any");
            }
            if (gTypeCheckingEnabled) {
                ChkStaticRecUrsion(c, n->fn.body, n->fn.name, 0);
            }
            c->current = fn;
            fn->scopedepth = 0;
            for (int i = 0; i < n->fn.pc; i++) {
                int slot = AddLocal(c, n->fn.pnames[i], 0);
                if (slot >= 0) {
                    strncpy(c->current->locals[slot].typename, fn->paramtypes[i], 63);
                }
            }
            CheckBorrows(c, n->fn.body, n->fn.body);
            for (int i = 0; i < n->fn.pc; i++) {
                if (n->fn.pdefaults && n->fn.pdefaults[i]) {
                    int slot = ResolveLocal(c, n->fn.pnames[i]);
                    if (slot >= 0) {
                        ChunkWrite(&fn->chunk, OPLOAD, n->line);
                        ChunkWriteU16(&fn->chunk, (uint16_t)slot, n->line);
                        ChunkWrite(&fn->chunk, OPPUSHNULL, n->line);
                        ChunkWrite(&fn->chunk, OPEQ, n->line);
                        
                        int jumpoffset = ChunkEmitJump(&fn->chunk, OPJUMPIFFALSE, n->line);
                        EmitExpr(c, n->fn.pdefaults[i]);
                        ChunkWrite(&fn->chunk, OPSTORE, n->line);
                        ChunkWriteU16(&fn->chunk, (uint16_t)slot, n->line);
                        
                        ChunkPatchJump(&fn->chunk, jumpoffset);
                    }
                }
            }
            if (n->fn.req_count > 0) {
                for (int i = 0; i < n->fn.req_count; i++) {
                    char err_msg[512];
                    snprintf(err_msg, sizeof(err_msg), "PreconditionViolated: %s failed", n->fn.name ? n->fn.name : "fn");
                    CompileContractCheck(c, n->fn.requires[i], err_msg, n->line);
                }
            }
            if (n->fn.ens_count > 0) {
                AddLocal(c, "result", 0);
            }
            if (n->fn.isvariadic) {
                ChunkWrite(&fn->chunk, OPRESTARGS, n->line);
                ChunkWrite(&fn->chunk, (uint8_t)(fn->paramcount - 1), n->line);
            }
            EmitNode(c, n->fn.body);
            if (n->fn.ens_count > 0) {
                int res_slot = ResolveLocal(c, "result");
                if (res_slot >= 0) {
                    ChunkWrite(&fn->chunk, OPPUSHNULL, n->line);
                    ChunkWrite(&fn->chunk, OPSTORE, n->line);
                    ChunkWriteU16(&fn->chunk, (uint16_t)res_slot, n->line);
                }
                for (int i = 0; i < n->fn.ens_count; i++) {
                    char err_msg[512];
                    snprintf(err_msg, sizeof(err_msg), "PostconditionViolated: %s failed", n->fn.name ? n->fn.name : "fn");
                    CompileContractCheck(c, n->fn.ensures[i], err_msg, n->line);
                }
            }
            ChunkWrite(&fn->chunk, OPRETURN, n->line);
            c->current = parent;

            ChunkWrite(ch, OPPUSHFN, n->line);
            ChunkWriteU16(ch, (uint16_t)fn->id, n->line);
            break;
        }
        case NBLOCK: {
            c->current->scopedepth++;
            for (int i = 0; i < n->block.count; i++) {
                if (i == n->block.count - 1) {
                    if (n->block.stmts[i]->type == NRETURN) {
                        Node *retstmt = n->block.stmts[i];
                        if (retstmt->single.expr) {
                            EmitExpr(c, retstmt->single.expr);
                        } else {
                            ChunkWrite(ch, OPPUSHNULL, n->line);
                        }
                    } else if (n->block.stmts[i]->type == NEXPRSTMT) {
                        EmitExpr(c, n->block.stmts[i]->single.expr);
                    } else {
                        EmitNode(c, n->block.stmts[i]);
                        ChunkWrite(ch, OPPUSHNULL, n->line);
                    }
                } else {
                    EmitNode(c, n->block.stmts[i]);
                }
            }
            if (n->block.count == 0) {
                ChunkWrite(ch, OPPUSHNULL, n->line);
            }
            c->current->scopedepth--;
            break;
        }
        case NSTRUCTINIT: {
            /* Struct initializer / spread update.
               { ...base, field1: v1, field2: v2 }  -> clone base then set fields
               TypeName { field1: v1, ... }          -> OPNEWSTRUCT then set fields */
            char tmpvar[64];
            snprintf(tmpvar, sizeof(tmpvar), "__si_tmp%d", c->current->localcount);
            int tmpslot = AddLocal(c, tmpvar, 0);

            if (n->structinit.spread) {
                /* Evaluate the spread (base) expression, clone it */
                EmitExpr(c, n->structinit.spread);
                ChunkWrite(ch, OPCLONESTRUCT, n->line);
            } else {
                /* Create a fresh struct */
                const char *tname = n->structinit.typename ? n->structinit.typename : "Object";
                int tid = AddStr(c, tname);
                ChunkWrite(ch, OPNEWSTRUCT, n->line);
                ChunkWriteU16(ch, (uint16_t)tid, n->line);
            }
            /* Store clone/new struct in temp slot */
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);

            /* Override / add each field */
            for (int i = 0; i < n->structinit.fc; i++) {
                EmitExpr(c, n->structinit.fvals[i]);
                ChunkWrite(ch, OPLOAD, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                int fid = AddStr(c, n->structinit.fnames[i]);
                ChunkWrite(ch, OPSETFIELD, n->line);
                ChunkWriteU16(ch, (uint16_t)fid, n->line);
                ChunkWrite(ch, OPPOP, n->line);
            }
            /* Leave the struct on the stack */
            ChunkWrite(ch, OPLOAD, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            break;
        }
        case NCHAINEDCMP: {
            /* Chained comparison: a < b < c  =>  (a < b) && (b < c)
               To avoid double-evaluating intermediate expressions we store each
               in a fresh temp local after first use. */
            int cnt = n->chainedcmp.count; /* number of expressions (ops = cnt-1) */
            if (cnt < 2) {
                /* degenerate - emit first expr as bool */
                EmitExpr(c, n->chainedcmp.exprs[0]);
                break;
            }

            /* Allocate temp slots for expressions [1 .. cnt-2] (the middle ones). */
            int *slots = malloc((cnt) * sizeof(int));
            slots[0] = -1; /* left-most expr evaluated fresh each time */
            for (int i = 1; i < cnt - 1; i++) {
                char sv[64];
                snprintf(sv, sizeof(sv), "__cmp_t%d_%d", n->line, i);
                slots[i] = AddLocal(c, sv, 0);
            }
            slots[cnt - 1] = -1;

            /* Jump list to short-circuit on false */
            int *falsejumps = malloc((cnt - 1) * sizeof(int));
            int falsejumpcnt = 0;

            for (int i = 0; i < cnt - 1; i++) {
                /* Emit left operand */
                if (i == 0) {
                    EmitExpr(c, n->chainedcmp.exprs[0]);
                } else {
                    /* Already stored in temp slot i */
                    ChunkWrite(ch, OPLOAD, n->line);
                    ChunkWriteU16(ch, (uint16_t)slots[i], n->line);
                }
                /* Emit right operand */
                EmitExpr(c, n->chainedcmp.exprs[i + 1]);
                /* If it's a middle expr, store a copy in its slot */
                if (i + 1 < cnt - 1) {
                    ChunkWrite(ch, OPDUP, n->line);
                    ChunkWrite(ch, OPSTORE, n->line);
                    ChunkWriteU16(ch, (uint16_t)slots[i + 1], n->line);
                }
                /* Emit comparison binary op */
                const char *op = n->chainedcmp.ops[i];
                if      (strcmp(op, "<")  == 0) ChunkWrite(ch, OPLT,  n->line);
                else if (strcmp(op, ">")  == 0) ChunkWrite(ch, OPGT,  n->line);
                else if (strcmp(op, "<=") == 0) ChunkWrite(ch, OPLTE, n->line);
                else if (strcmp(op, ">=") == 0) ChunkWrite(ch, OPGTE, n->line);
                else ChunkWrite(ch, OPLT, n->line); /* fallback */

                /* Short-circuit: if false jump over remaining comparisons */
                if (i < cnt - 2) {
                    falsejumps[falsejumpcnt++] = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
                }
            }

            /* All comparisons true - result is already on stack */
            int endjump = ChunkEmitJump(ch, OPJUMP, n->line);

            /* Patch false jumps to here: push false */
            for (int i = 0; i < falsejumpcnt; i++) {
                ChunkPatchJump(ch, falsejumps[i]);
            }
            /* Pop the leftover comparison result from short-circuited path */
            /* (OPJUMPIFFALSE pops, so nothing to pop here) */
            ChunkWrite(ch, OPPUSHBOOL, n->line);
            ChunkWrite(ch, 0, n->line); /* false */

            ChunkPatchJump(ch, endjump);

            free(slots);
            free(falsejumps);
            break;
        }
        case NNURSERY:
        case NREGION:
        case NMATCH:
            EmitNode(c, n);
            break;
        case NTERNARY: {
            EmitExpr(c, n->ternary.cond);
            int else_jump = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
            EmitExpr(c, n->ternary.tval);
            int end_jump = ChunkEmitJump(ch, OPJUMP, n->line);
            ChunkPatchJump(ch, else_jump);
            EmitExpr(c, n->ternary.fval);
            ChunkPatchJump(ch, end_jump);
            break;
        }
        case NCONST_EVAL: {
            RunCTFE(c, n->single.expr, n->line);
            break;
        }
        case NASM: {
            int strid = AddStr(c, n->asmstmt.code ? n->asmstmt.code : "");
            ChunkWrite(ch, OPASM, n->line);
            ChunkWriteU16(ch, (uint16_t)strid, n->line);
            break;
        }
        default:
            fprintf(stderr, "[emitexpr] unknown node type: %d\n", n->type);
            Error(c,n->line,"Unknown expression node");
    }
}

static void EmitPatternCheck(Compiler *c, Node *pat, int valslot, int *failjumps, int *failcount) {
    Chunk *ch = &c->current->chunk;
    int line = pat->line;
    
    switch (pat->type) {
        case NIDENT: {
            if (strcmp(pat->sval, "_") == 0) {
                break;
            }
            int slot = AddLocal(c, pat->sval, 0);
            if (slot >= 0) {
                strcpy(c->current->locals[slot].typename, "any");
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                ChunkWrite(ch, OPSTORE, line);
                ChunkWriteU16(ch, (uint16_t)slot, line);
            }
            break;
        }
        case NINT:
        case NFLOAT:
        case NSTR:
        case NBOOL:
        case NNULL: {
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)valslot, line);
            EmitExpr(c, pat);
            ChunkWrite(ch, OPEQ, line);
            failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            break;
        }
        case NORPAT: {
            int leftfails[256];
            int leftfailcount = 0;
            EmitPatternCheck(c, pat->orpat.left, valslot, leftfails, &leftfailcount);
            int successjump = ChunkEmitJump(ch, OPJUMP, line);
            for (int j = 0; j < leftfailcount; j++) {
                ChunkPatchJump(ch, leftfails[j]);
            }
            EmitPatternCheck(c, pat->orpat.right, valslot, failjumps, failcount);
            ChunkPatchJump(ch, successjump);
            break;
        }
        case NSTRUCTPAT: {
            if (pat->structpat.name) {
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                ChunkWrite(ch, OPTYPEOF, line);
                int strid = AddStr(c, pat->structpat.name);
                ChunkWrite(ch, OPPUSHSTR, line);
                ChunkWriteU16(ch, (uint16_t)strid, line);
                ChunkWrite(ch, OPEQ, line);
                failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            }
            for (int i = 0; i < pat->structpat.count; i++) {
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                int fid = AddStr(c, pat->structpat.fields[i]);
                ChunkWrite(ch, OPGETFIELD, line);
                ChunkWriteU16(ch, (uint16_t)fid, line);
                
                char fieldtmp[64];
                snprintf(fieldtmp, sizeof(fieldtmp), "fldtmp%d", c->current->localcount);
                int fieldslot = AddLocal(c, fieldtmp, 0);
                if (fieldslot >= 0) {
                    strcpy(c->current->locals[fieldslot].typename, "any");
                    ChunkWrite(ch, OPSTORE, line);
                    ChunkWriteU16(ch, (uint16_t)fieldslot, line);
                    EmitPatternCheck(c, pat->structpat.patterns[i], fieldslot, failjumps, failcount);
                }
            }
            break;
        }
        case NTUPLEPAT: {
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)valslot, line);
            ChunkWrite(ch, OPTYPEOF, line);
            int strid = AddStr(c, "tuple");
            ChunkWrite(ch, OPPUSHSTR, line);
            ChunkWriteU16(ch, (uint16_t)strid, line);
            ChunkWrite(ch, OPEQ, line);
            failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)valslot, line);
            int lenfn = ResolveGlobal(c, "Length", 0);
            if (lenfn >= 0) {
                ChunkWrite(ch, OPCALL, line);
                ChunkWriteU16(ch, (uint16_t)lenfn, line);
                ChunkWrite(ch, 1, line);
            } else {
                ChunkWrite(ch, OPLOADGLOBAL, line);
                int lensid = AddStr(c, "Length");
                ChunkWriteU16(ch, (uint16_t)lensid, line);
                ChunkWrite(ch, OPCALLDYNAMIC, line);
                ChunkWrite(ch, 1, line);
            }
            ChunkWrite(ch, OPPUSHINT, line);
            ChunkWriteI64(ch, pat->tuplepat.count, line);
            ChunkWrite(ch, OPEQ, line);
            failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            
            for (int i = 0; i < pat->tuplepat.count; i++) {
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                ChunkWrite(ch, OPPUSHINT, line);
                ChunkWriteI64(ch, i, line);
                ChunkWrite(ch, OPARRGET, line);
                
                char elemtmp[64];
                snprintf(elemtmp, sizeof(elemtmp), "tupletmp%d", c->current->localcount);
                int elemslot = AddLocal(c, elemtmp, 0);
                if (elemslot >= 0) {
                    strcpy(c->current->locals[elemslot].typename, "any");
                    ChunkWrite(ch, OPSTORE, line);
                    ChunkWriteU16(ch, (uint16_t)elemslot, line);
                    EmitPatternCheck(c, pat->tuplepat.patterns[i], elemslot, failjumps, failcount);
                }
            }
            break;
        }
        case NARRPAT: {
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)valslot, line);
            ChunkWrite(ch, OPTYPEOF, line);
            int strid = AddStr(c, "array");
            ChunkWrite(ch, OPPUSHSTR, line);
            ChunkWriteU16(ch, (uint16_t)strid, line);
            ChunkWrite(ch, OPEQ, line);
            failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)valslot, line);
            int lenfn = ResolveGlobal(c, "Length", 0);
            if (lenfn >= 0) {
                ChunkWrite(ch, OPCALL, line);
                ChunkWriteU16(ch, (uint16_t)lenfn, line);
                ChunkWrite(ch, 1, line);
            } else {
                ChunkWrite(ch, OPLOADGLOBAL, line);
                int lensid = AddStr(c, "Length");
                ChunkWriteU16(ch, (uint16_t)lensid, line);
                ChunkWrite(ch, OPCALLDYNAMIC, line);
                ChunkWrite(ch, 1, line);
            }
            int mincount = pat->arrpat.hasrest ? (pat->arrpat.count - 1) : pat->arrpat.count;
            ChunkWrite(ch, OPPUSHINT, line);
            ChunkWriteI64(ch, mincount, line);
            if (pat->arrpat.hasrest) {
                ChunkWrite(ch, OPGTE, line);
            } else {
                ChunkWrite(ch, OPEQ, line);
            }
            failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            
            int restidx = pat->arrpat.hasrest ? pat->arrpat.restidx : -1;
            for (int i = 0; i < pat->arrpat.count; i++) {
                if (i == restidx) {
                    ChunkWrite(ch, OPLOAD, line);
                    ChunkWriteU16(ch, (uint16_t)valslot, line);
                    ChunkWrite(ch, OPPUSHINT, line);
                    ChunkWriteI64(ch, restidx, line);
                    ChunkWrite(ch, OPLOAD, line);
                    ChunkWriteU16(ch, (uint16_t)valslot, line);
                    if (lenfn >= 0) {
                        ChunkWrite(ch, OPCALL, line);
                        ChunkWriteU16(ch, (uint16_t)lenfn, line);
                        ChunkWrite(ch, 1, line);
                    } else {
                        ChunkWrite(ch, OPLOADGLOBAL, line);
                        int lensid = AddStr(c, "Length");
                        ChunkWriteU16(ch, (uint16_t)lensid, line);
                        ChunkWrite(ch, OPCALLDYNAMIC, line);
                        ChunkWrite(ch, 1, line);
                    }
                    int slicefn = ResolveGlobal(c, "slice", 0);
                    if (slicefn >= 0) {
                        ChunkWrite(ch, OPCALL, line);
                        ChunkWriteU16(ch, (uint16_t)slicefn, line);
                        ChunkWrite(ch, 3, line);
                    } else {
                        int slcid = AddStr(c, "slice");
                        ChunkWrite(ch, OPLOADGLOBAL, line);
                        ChunkWriteU16(ch, (uint16_t)slcid, line);
                        ChunkWrite(ch, OPCALLDYNAMIC, line);
                        ChunkWrite(ch, 3, line);
                    }
                    char resttmp[64];
                    snprintf(resttmp, sizeof(resttmp), "arrresttmp%d", c->current->localcount);
                    int restslot = AddLocal(c, resttmp, 0);
                    if (restslot >= 0) {
                        strcpy(c->current->locals[restslot].typename, "any");
                        ChunkWrite(ch, OPSTORE, line);
                        ChunkWriteU16(ch, (uint16_t)restslot, line);
                        EmitPatternCheck(c, pat->arrpat.patterns[i], restslot, failjumps, failcount);
                    }
                } else {
                    int elem_idx = (restidx >= 0 && i > restidx) ? i - 1 : i;
                    ChunkWrite(ch, OPLOAD, line);
                    ChunkWriteU16(ch, (uint16_t)valslot, line);
                    ChunkWrite(ch, OPPUSHINT, line);
                    ChunkWriteI64(ch, elem_idx, line);
                    ChunkWrite(ch, OPARRGET, line);
                    
                    char elemtmp[64];
                    snprintf(elemtmp, sizeof(elemtmp), "arrelemtmp%d", c->current->localcount);
                    int elemslot = AddLocal(c, elemtmp, 0);
                    if (elemslot >= 0) {
                        strcpy(c->current->locals[elemslot].typename, "any");
                        ChunkWrite(ch, OPSTORE, line);
                        ChunkWriteU16(ch, (uint16_t)elemslot, line);
                        EmitPatternCheck(c, pat->arrpat.patterns[i], elemslot, failjumps, failcount);
                    }
                }
            }
            break;
        }
        case NMEMBER: {
            int isadt = 0;
            if (pat->member.obj->type == NIDENT) {
                for (int i = 0; i < c->enumcount; i++) {
                    if (strcmp(c->enums[i].name, pat->member.obj->sval) == 0) {
                        if (c->enums[i].isadt) {
                            isadt = 1;
                        }
                        break;
                    }
                }
            }
            if (isadt) {
                char variantname[256];
                snprintf(variantname, sizeof(variantname), "%s.%s", pat->member.obj->sval, pat->member.field);
                
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                ChunkWrite(ch, OPTYPEOF, line);
                int enumstr = AddStr(c, pat->member.obj->sval);
                ChunkWrite(ch, OPPUSHSTR, line);
                ChunkWriteU16(ch, (uint16_t)enumstr, line);
                ChunkWrite(ch, OPEQ, line);
                failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
                
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                int tagfid = AddStr(c, "$tag");
                ChunkWrite(ch, OPGETFIELD, line);
                ChunkWriteU16(ch, (uint16_t)tagfid, line);
                
                int varstrid = AddStr(c, variantname);
                ChunkWrite(ch, OPPUSHSTR, line);
                ChunkWriteU16(ch, (uint16_t)varstrid, line);
                ChunkWrite(ch, OPEQ, line);
                failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            } else {
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                EmitExpr(c, pat);
                ChunkWrite(ch, OPEQ, line);
                failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            }
            break;
        }
        case NCALL: {
            int isadt = 0;
            if (pat->call.callee->type == NMEMBER && pat->call.callee->member.obj->type == NIDENT) {
                for (int i = 0; i < c->enumcount; i++) {
                    if (strcmp(c->enums[i].name, pat->call.callee->member.obj->sval) == 0) {
                        if (c->enums[i].isadt) {
                            isadt = 1;
                        }
                        break;
                    }
                }
            }
            if (isadt) {
                char variantname[256];
                snprintf(variantname, sizeof(variantname), "%s.%s", pat->call.callee->member.obj->sval, pat->call.callee->member.field);
                
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                ChunkWrite(ch, OPTYPEOF, line);
                int enumstr = AddStr(c, pat->call.callee->member.obj->sval);
                ChunkWrite(ch, OPPUSHSTR, line);
                ChunkWriteU16(ch, (uint16_t)enumstr, line);
                ChunkWrite(ch, OPEQ, line);
                failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
                
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                int tagfid = AddStr(c, "$tag");
                ChunkWrite(ch, OPGETFIELD, line);
                ChunkWriteU16(ch, (uint16_t)tagfid, line);
                
                int varstrid = AddStr(c, variantname);
                ChunkWrite(ch, OPPUSHSTR, line);
                ChunkWriteU16(ch, (uint16_t)varstrid, line);
                ChunkWrite(ch, OPEQ, line);
                failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
                
                for (int i = 0; i < pat->call.argc; i++) {
                    ChunkWrite(ch, OPLOAD, line);
                    ChunkWriteU16(ch, (uint16_t)valslot, line);
                    
                    char fieldname[32];
                    snprintf(fieldname, sizeof(fieldname), "field%d", i);
                    int fid = AddStr(c, fieldname);
                    ChunkWrite(ch, OPGETFIELD, line);
                    ChunkWriteU16(ch, (uint16_t)fid, line);
                    
                    char fieldtmp[64];
                    snprintf(fieldtmp, sizeof(fieldtmp), "adtfieldtmp%d", c->current->localcount);
                    int fieldslot = AddLocal(c, fieldtmp, 0);
                    if (fieldslot >= 0) {
                        strcpy(c->current->locals[fieldslot].typename, "any");
                        ChunkWrite(ch, OPSTORE, line);
                        ChunkWriteU16(ch, (uint16_t)fieldslot, line);
                        EmitPatternCheck(c, pat->call.args[i], fieldslot, failjumps, failcount);
                    }
                }
            } else {
                ChunkWrite(ch, OPLOAD, line);
                ChunkWriteU16(ch, (uint16_t)valslot, line);
                EmitExpr(c, pat);
                ChunkWrite(ch, OPEQ, line);
                failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            }
            break;
        }
        default: {
            ChunkWrite(ch, OPLOAD, line);
            ChunkWriteU16(ch, (uint16_t)valslot, line);
            EmitExpr(c, pat);
            ChunkWrite(ch, OPEQ, line);
            failjumps[(*failcount)++] = ChunkEmitJump(ch, OPJUMPIFFALSE, line);
            break;
        }
    }
}
static void EmitStructMetadata(Compiler *c, Node *n) {
    /* Safe stub: no bytecode emitted.
     * Derive macro metadata is available at runtime via the reflection
     * API (fields(), has(), get(), set()) which already works without
     * any extra bytecode here.  The previous implementation generated
     * malformed append() call sequences that corrupted the VM stack.
     * A proper implementation should be added later using array-literal
     * opcodes (OPNEWARR with inline element count) once the exact
     * operand contract of OPNEWARR is confirmed. */
    (void)c; (void)n;
}

static int emit_depth = 0;
static void EmitNodeInternal(Compiler *c, Node *n);

static void EmitNode(Compiler *c, Node *n) {
    if (!n) return;
    emit_depth++;
    if (emit_depth > 500) {
        fprintf(stderr, "INFINITE RECURSION in emitnode: type=%d, line=%d\n", n->type, n->line);
        exit(1);
    }
    EmitNodeInternal(c, n);
    emit_depth--;
}

static void EmitNodeInternal(Compiler *c, Node *n) {
    if(!n) return;
    Chunk *ch=&c->current->chunk;
    switch(n->type){
        case NSELECT: {
            int count = n->selectn.count;
            int has_default = n->selectn.defaultbody != NULL;
            
            for (int i = 0; i < count; i++) {
                Node *cnode = n->selectn.cases[i];
                if (cnode->type == NVARDECL) {
                    Node *init = cnode->decl.init;
                    EmitExpr(c, init->call.args[0]);
                    ChunkWrite(ch, OPPUSHNULL, n->line);
                    ChunkWrite(ch, OPPUSHINT, n->line);
                    ChunkWriteI64(ch, 0, n->line);
                } else if (cnode->type == NCALL && cnode->call.callee->type == NIDENT && !strcmp(cnode->call.callee->sval, "Receive")) {
                    EmitExpr(c, cnode->call.args[0]);
                    ChunkWrite(ch, OPPUSHNULL, n->line);
                    ChunkWrite(ch, OPPUSHINT, n->line);
                    ChunkWriteI64(ch, 0, n->line);
                } else if (cnode->type == NCALL && cnode->call.callee->type == NIDENT && !strcmp(cnode->call.callee->sval, "send")) {
                    EmitExpr(c, cnode->call.args[0]);
                    EmitExpr(c, cnode->call.args[1]);
                    ChunkWrite(ch, OPPUSHINT, n->line);
                    ChunkWriteI64(ch, 1, n->line);
                } else {
                    Error(c, n->line, "select case must be a channel send or recv operation");
                }
            }
            
            ChunkWrite(ch, OPSELECT, n->line);
            ChunkWriteU16(ch, (uint16_t)count, n->line);
            ChunkWrite(ch, (uint8_t)has_default, n->line);
            
            int *jump_offsets = malloc(count * sizeof(int));
            for (int i = 0; i < count; i++) {
                jump_offsets[i] = ch->count;
                ChunkWriteU16(ch, 0, n->line);
            }
            int default_jump_offset = -1;
            if (has_default) {
                default_jump_offset = ch->count;
                ChunkWriteU16(ch, 0, n->line);
            }
            
            int end_jumps_cap = 16, end_jumps_count = 0;
            int *end_jumps = malloc(end_jumps_cap * sizeof(int));
            
            for (int i = 0; i < count; i++) {
                int case_pc = ch->count;
                int rel_jump = case_pc - (jump_offsets[i] + 2);
                ch->code[jump_offsets[i]] = (rel_jump >> 8) & 0xFF;
                ch->code[jump_offsets[i] + 1] = rel_jump & 0xFF;
                
                Node *cnode = n->selectn.cases[i];
                if (cnode->type == NVARDECL) {
                    int slot = AddLocal(c, cnode->decl.name, 0);
                    strcpy(c->current->locals[slot].typename, "any");
                    ChunkWrite(ch, OPSTORE, n->line);
                    ChunkWriteU16(ch, (uint16_t)slot, n->line);
                } else {
                    ChunkWrite(ch, OPPOP, n->line);
                }
                
                EmitNode(c, n->selectn.bodies[i]);
                
                if (end_jumps_count == end_jumps_cap) {
                    end_jumps_cap *= 2;
                    end_jumps = realloc(end_jumps, end_jumps_cap * sizeof(int));
                }
                end_jumps[end_jumps_count++] = ChunkEmitJump(ch, OPJUMP, n->line);
            }
            
            if (has_default) {
                int def_pc = ch->count;
                int rel_jump = def_pc - (default_jump_offset + 2);
                ch->code[default_jump_offset] = (rel_jump >> 8) & 0xFF;
                ch->code[default_jump_offset + 1] = rel_jump & 0xFF;
                
                ChunkWrite(ch, OPPOP, n->line);
                EmitNode(c, n->selectn.defaultbody);
                
                if (end_jumps_count == end_jumps_cap) {
                    end_jumps_cap *= 2;
                    end_jumps = realloc(end_jumps, end_jumps_cap * sizeof(int));
                }
                end_jumps[end_jumps_count++] = ChunkEmitJump(ch, OPJUMP, n->line);
            }
            
            int end_pc = ch->count;
            for (int j = 0; j < end_jumps_count; j++) {
                ChunkPatchJump(ch, end_jumps[j]);
            }
            
            free(jump_offsets);
            free(end_jumps);
            break;
        }
        case NMATCH: {
            const char *exprtype = InferExprType(c, n->match.expr);
            int enumidx = -1;
            for (int i = 0; i < c->enumcount; i++) {
                if (strcmp(c->enums[i].name, exprtype) == 0) {
                    enumidx = i; break;
                }
            }
            if (enumidx >= 0) {
                CompilerEnum *en = &c->enums[enumidx];
                int haswildcard = 0;
                for (int i = 0; i < n->match.count; i++) {
                    Node *pat = n->match.patterns[i];
                    if (pat && pat->type == NIDENT &&
                        (strcmp(pat->sval, "_") == 0 || strcmp(pat->sval, "default") == 0 || strcmp(pat->sval, "else") == 0)) {
                        haswildcard = 1; break;
                    }
                }
                if (!haswildcard) {
                    char missingbuf[512] = "";
                    int missingcount = 0;
                    for (int m = 0; m < en->count; m++) {
                        int matched = 0;
                        for (int i = 0; i < n->match.count; i++) {
                            Node *pat = n->match.patterns[i];
                            if (!pat) continue;
                            if (pat->type == NCALL) {
                                if (pat->call.callee->type == NMEMBER && strcmp(pat->call.callee->member.field, en->members[m]) == 0) {
                                    matched = 1; break;
                                }
                                if (pat->call.callee->type == NIDENT && strcmp(pat->call.callee->sval, en->members[m]) == 0) {
                                    matched = 1; break;
                                }
                            } else if (pat->type == NMEMBER) {
                                if (strcmp(pat->member.field, en->members[m]) == 0) {
                                    matched = 1; break;
                                }
                            } else if (pat->type == NIDENT) {
                                if (strcmp(pat->sval, en->members[m]) == 0) {
                                    matched = 1; break;
                                }
                            }
                        }
                        if (!matched) {
                            if (missingcount > 0 && strlen(missingbuf) + 2 < sizeof(missingbuf)) {
                                strcat(missingbuf, ", ");
                            }
                            strncat(missingbuf, en->members[m], sizeof(missingbuf) - strlen(missingbuf) - 1);
                            missingcount++;
                        }
                    }
                    if (missingcount > 0) {
                        char warnmsg[1024];
                        snprintf(warnmsg, sizeof(warnmsg), "Non-exhaustive match on '%s': missing variants: %s", en->name, missingbuf);
                        if (gStrictTypes) {
                            Error(c, n->line, warnmsg);
                        } else {
                            Warning(c, n->line, warnmsg);
                        }
                    }
                }
            }

            EmitExpr(c, n->match.expr);
            int tmpslot = AddLocal(c, "matchtmp", 0);
            if (tmpslot < 0) break;
            strcpy(c->current->locals[tmpslot].typename, "any");
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            
            int *endjumps = malloc(n->match.count * sizeof(int));
            int endjumpcount = 0;
            
            for (int i = 0; i < n->match.count; i++) {
                c->current->scopedepth++;
                int localstart = c->current->localcount;
                
                int armfails[256];
                int armfailcount = 0;
                
                EmitPatternCheck(c, n->match.patterns[i], tmpslot, armfails, &armfailcount);
                
                if (n->match.guards[i]) {
                    EmitExpr(c, n->match.guards[i]);
                    armfails[armfailcount++] = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
                }
                
                Node *bnode = n->match.bodies[i];
                if (bnode->type == NEXPRSTMT) {
                    EmitExpr(c, bnode->single.expr);
                } else {
                    EmitNode(c, bnode);
                }
                
                endjumps[endjumpcount++] = ChunkEmitJump(ch, OPJUMP, n->line);
                
                for (int j = 0; j < armfailcount; j++) {
                    ChunkPatchJump(ch, armfails[j]);
                }
                
                c->current->scopedepth--;
                c->current->localcount = localstart;
            }
            
            for (int j = 0; j < endjumpcount; j++) {
                ChunkPatchJump(ch, endjumps[j]);
            }
            free(endjumps);
            
            c->current->localcount--;
            break;
        }
        case NNURSERY: {
            const char *nvar = (n->nursery.name && strlen(n->nursery.name) > 0) ? n->nursery.name : "n";
            int nslot = AddLocal(c, nvar, 0);
            if (nslot >= 0) strcpy(c->current->locals[nslot].typename, "Nursery");
            
            int newfn = ResolveGlobal(c, "NurseryNew", 0);
            if (newfn >= 0) {
                ChunkWrite(ch, OPCALL, n->line);
                ChunkWriteU16(ch, (uint16_t)newfn, n->line);
                ChunkWrite(ch, 0, n->line);
            } else {
                int tid = AddStr(c, "Nursery");
                ChunkWrite(ch, OPNEWSTRUCT, n->line);
                ChunkWriteU16(ch, (uint16_t)tid, n->line);
            }
            if (nslot >= 0) {
                ChunkWrite(ch, OPSTORE, n->line);
                ChunkWriteU16(ch, (uint16_t)nslot, n->line);
            } else {
                ChunkWrite(ch, OPPOP, n->line);
            }
            
            if (n->nursery.body) {
                EmitNode(c, n->nursery.body);
            }
            break;
        }
        case NREGION: {
            const char *rvar = (n->region.name && strlen(n->region.name) > 0) ? n->region.name : "region";
            int rslot = AddLocal(c, rvar, 0);
            if (rslot >= 0) strcpy(c->current->locals[rslot].typename, "RegionScope");
            
            int newfn = ResolveGlobal(c, "RegionNew", 0);
            if (newfn >= 0) {
                int strid = AddStr(c, rvar);
                ChunkWrite(ch, OPPUSHSTR, n->line);
                ChunkWriteU16(ch, (uint16_t)strid, n->line);
                ChunkWrite(ch, OPCALL, n->line);
                ChunkWriteU16(ch, (uint16_t)newfn, n->line);
                ChunkWrite(ch, 1, n->line);
            } else {
                int tid = AddStr(c, "RegionScope");
                ChunkWrite(ch, OPNEWSTRUCT, n->line);
                ChunkWriteU16(ch, (uint16_t)tid, n->line);
            }
            if (rslot >= 0) {
                ChunkWrite(ch, OPSTORE, n->line);
                ChunkWriteU16(ch, (uint16_t)rslot, n->line);
            } else {
                ChunkWrite(ch, OPPOP, n->line);
            }
            
            if (n->region.body) {
                EmitNode(c, n->region.body);
            }
            break;
        }
        case NEXPRSTMT:
            EmitExpr(c,n->single.expr);
            if (c->isrepl && c->current->scopedepth == 0) {
                ChunkWrite(ch,OPPRINTREPL,n->line);
            } else {
                ChunkWrite(ch,OPPOP,n->line);
            }
            break;
        case NPRINT:
            EmitExpr(c,n->single.expr);
            ChunkWrite(ch,OPPRINTLN,n->line);
            break;
        case NINPUT: {
            uint16_t flags = 0;
            if (n->single.expr) {
                EmitExpr(c, n->single.expr);
                flags |= 1;
            }
            ChunkWrite(ch, OPINPUTSTR, n->line);
            ChunkWriteU16(ch, flags, n->line);

            if (n->single.var) {
                int slot = ResolveLocal(c, n->single.var);
                if (slot >= 0) {
                    ChunkWrite(ch, OPSTORE, n->line);
                    ChunkWriteU16(ch, (uint16_t)slot, n->line);
                } else {
                    int sid = AddStr(c, n->single.var);
                    ChunkWrite(ch, OPSTOREGLOBAL, n->line);
                    ChunkWriteU16(ch, (uint16_t)sid, n->line);
                }
            } else {
                ChunkWrite(ch, OPPOP, n->line);
            }
            break;
        }
        case NDESTRUCTARR: {
            EmitExpr(c, n->destructarr.value);
            
            char tmpvar[64];
            snprintf(tmpvar, sizeof(tmpvar), "destructtmp%d", c->current->localcount);
            int tmpslot = AddLocal(c, tmpvar, 0);
            strcpy(c->current->locals[tmpslot].typename, "any");
            ChunkWrite(ch, OPDUP, n->line);
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            
            for (int i = 0; i < n->destructarr.count; i++) {
                int varslot = AddLocal(c, n->destructarr.names[i], 0);
                strcpy(c->current->locals[varslot].typename, "any");
                
                ChunkWrite(ch, OPLOAD, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                
                ChunkWrite(ch, OPPUSHINT, n->line);
                ChunkWriteI64(ch, i, n->line);
                
                ChunkWrite(ch, OPARRGET, n->line);
                
                ChunkWrite(ch, OPDUP, n->line);
                ChunkWrite(ch, OPSTORE, n->line);
                ChunkWriteU16(ch, (uint16_t)varslot, n->line);
            }
            break;
        }
        case NDESTRUCTOBJ: {
            EmitExpr(c, n->destructobj.value);
            
            char tmpvar[64];
            snprintf(tmpvar, sizeof(tmpvar), "destructtmp%d", c->current->localcount);
            int tmpslot = AddLocal(c, tmpvar, 0);
            strcpy(c->current->locals[tmpslot].typename, "any");
            ChunkWrite(ch, OPDUP, n->line);
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
            
            for (int i = 0; i < n->destructobj.count; i++) {
                int varslot = AddLocal(c, n->destructobj.aliases[i], 0);
                strcpy(c->current->locals[varslot].typename, "any");
                
                ChunkWrite(ch, OPLOAD, n->line);
                ChunkWriteU16(ch, (uint16_t)tmpslot, n->line);
                
                int fid = AddStr(c, n->destructobj.keys[i]);
                ChunkWrite(ch, OPGETFIELD, n->line);
                ChunkWriteU16(ch, (uint16_t)fid, n->line);
                
                ChunkWrite(ch, OPDUP, n->line);
                ChunkWrite(ch, OPSTORE, n->line);
                ChunkWriteU16(ch, (uint16_t)varslot, n->line);
            }
            break;
        }
        case NVARDECL:
        case NCONSTDECL: {
            int isconst = n->decl.isconstflag;
            if (n->type == NVARDECL && gImmutableByDefault) {
                isconst = 1;
            }
            const char *expectedtype = n->decl.tname ? n->decl.tname : "any";
            const char *actualtype = "null";
            if (n->decl.init) {
                actualtype = InferExprType(c, n->decl.init);
            }
            if (strcmp(expectedtype, "any") == 0 && strcmp(actualtype, "any") != 0 && strcmp(actualtype, "null") != 0) {
                expectedtype = actualtype;
            }
            if (gTypeCheckingEnabled && !TypesCompatible(c, expectedtype, actualtype)) {
                char buf[256];
                snprintf(buf, sizeof(buf), "TypeError: declared type '%s' does not match initializer type '%s'", expectedtype, actualtype);
                AppendUnionDetails(buf, sizeof(buf), expectedtype);
                Error(c, n->line, buf);
            } else if (strchr(expectedtype, '{')) {
                ChkRefConst(c, expectedtype, n->line, n->decl.init);
            }
            int slot = AddLocal(c, n->decl.name, isconst);
            if (slot >= 0) {
                if (n->decl.init && n->decl.init->type == NSTRUCTINIT && n->decl.init->structinit.typename) {
                    strncpy(c->current->locals[slot].typename, n->decl.init->structinit.typename, 63);
                } else {
                    strncpy(c->current->locals[slot].typename, expectedtype, 63);
                }
            }
            if(n->decl.init) EmitExpr(c,n->decl.init);
            else ChunkWrite(ch,OPPUSHNULL,n->line);
            if (strchr(expectedtype, '{')) {
                EmitRefCheck(c, expectedtype, n->line, n->decl.init);
            }
            if (strchr(expectedtype, '&')) {
                int tid = AddStr(c, expectedtype);
                ChunkWrite(ch, OPPUSHSTR, n->line);
                ChunkWriteU16(ch, (uint16_t)tid, n->line);
                int fnid = ResolveGlobal(c, "CheckIntersect", 0);
                if (fnid >= 0) {
                    ChunkWrite(ch, OPCALL, n->line);
                    ChunkWriteU16(ch,(uint16_t)fnid,n->line);
                    ChunkWrite(ch, 2, n->line);
                }
            }
            if (n->decl.init && n->decl.init->type == NIDENT) {
                int rslot = ResolveLocal(c, n->decl.init->sval);
                if (rslot >= 0) {
                    const char *type = c->current->locals[rslot].typename;
                    if (IsNonPrimitiveType(type)) {
                        c->current->locals[rslot].ismoved = 1;
                    }
                }
            }
            ChunkWrite(ch,OPSTORE,n->line);
            ChunkWriteU16(ch,(uint16_t)slot,n->line);
            break;
        }
        case NASSIGN:
            EmitExpr(c,n);     // pop result since assign is an expression
            ChunkWrite(ch,OPPOP,n->line);
            break;
        case NIF: {
            EmitExpr(c,n->ifn.cond);
            int elsejump=ChunkEmitJump(ch,OPJUMPIFFALSE,n->line);
            
            int narrowedslot = -1;
            char originaltype[64] = "";
            if (n->ifn.cond->type == NBINOP) {
                Node *left = n->ifn.cond->binop.left;
                Node *right = n->ifn.cond->binop.right;
                const char *op = n->ifn.cond->binop.op;
                
                if (left->type == NIDENT) {
                    int slot = ResolveLocal(c, left->sval);
                    if (slot >= 0) {
                        narrowedslot = slot;
                        strncpy(originaltype, c->current->locals[slot].typename, sizeof(originaltype) - 1);
                        
                        char baseclean[64];
                        strncpy(baseclean, originaltype, sizeof(baseclean) - 1);
                        baseclean[sizeof(baseclean) - 1] = '\0';
                        char *brace = strchr(baseclean, '{');
                        if (brace) *brace = '\0';
                        
                        char rstr[128];
                        GetNodeStr(right, rstr, sizeof(rstr));
                        
                        snprintf(c->current->locals[slot].typename, 63, "%s{_narrow_%s: _narrow_%s %s %s}",
                                 baseclean, left->sval, left->sval, op, rstr);
                    }
                } else if (right->type == NIDENT) {
                    int slot = ResolveLocal(c, right->sval);
                    if (slot >= 0) {
                        narrowedslot = slot;
                        strncpy(originaltype, c->current->locals[slot].typename, sizeof(originaltype) - 1);
                        
                        char baseclean[64];
                        strncpy(baseclean, originaltype, sizeof(baseclean) - 1);
                        baseclean[sizeof(baseclean) - 1] = '\0';
                        char *brace = strchr(baseclean, '{');
                        if (brace) *brace = '\0';
                        
                        char lstr[128];
                        GetNodeStr(left, lstr, sizeof(lstr));
                        
                        snprintf(c->current->locals[slot].typename, 63, "%s{_narrow_%s: %s %s _narrow_%s}",
                                 baseclean, right->sval, lstr, op, right->sval);
                    }
                }
            }
            
            EmitNode(c,n->ifn.then);
            
            if (narrowedslot >= 0) {
                strncpy(c->current->locals[narrowedslot].typename, originaltype, 63);
            }
            if(n->ifn.els){
                int endjump=ChunkEmitJump(ch,OPJUMP,n->line);
                ChunkPatchJump(ch,elsejump);
                EmitNode(c,n->ifn.els);
                ChunkPatchJump(ch,endjump);
            } else {
                ChunkPatchJump(ch,elsejump);
            }
            break;
        }
        case NWHILE: {
            extern int gContractCheck;
            if (c->loopdepth >= 128) {
                fprintf(stderr, "Compiler error on line %d: maximum loop nesting depth (128) exceeded\n", n->line);
                c->haderror = 1;
                break;
            }
            int l_idx = c->loopdepth++;
            c->loopstarts[l_idx] = ch->count;
            c->loopbreakcounts[l_idx] = 0;
            c->loopcontinuecounts[l_idx] = 0;
            c->loopscopes[l_idx] = c->current->scopedepth;
            
            int loopstart = ch->count;
            if (n->whilen.inv_count > 0) {
                for (int i = 0; i < n->whilen.inv_count; i++) {
                    CompileContractCheck(c, n->whilen.invariants[i], "InvariantViolated", n->line);
                }
            }
            EmitExpr(c, n->whilen.cond);
            int exitjump = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
            EmitNode(c, n->whilen.body);
            
            ChunkWrite(ch, OPJUMP, n->line);
            int32_t back = (int32_t)(loopstart - ch->count - 4);
            ChunkWriteI32(ch, back, n->line);
            
            ChunkPatchJump(ch, exitjump);
            
            for (int i = 0; i < c->loopcontinuecounts[l_idx]; i++) {
                int offset = c->loopcontinues[l_idx][i];
                int32_t diff = (int32_t)(loopstart - offset - 4);
                ch->code[offset  ] = (uint8_t)((diff >> 24) & 0xFF);
                ch->code[offset+1] = (uint8_t)((diff >> 16) & 0xFF);
                ch->code[offset+2] = (uint8_t)((diff >> 8) & 0xFF);
                ch->code[offset+3] = (uint8_t)(diff & 0xFF);
            }
            for (int i = 0; i < c->loopbreakcounts[l_idx]; i++) {
                ChunkPatchJump(ch, c->loopbreaks[l_idx][i]);
            }
            c->loopdepth--;
            break;
        }
        case NFOR: {
            extern int gContractCheck;
            if (c->loopdepth >= 128) {
                fprintf(stderr, "Compiler error on line %d: maximum loop nesting depth (128) exceeded\n", n->line);
                c->haderror = 1;
                break;
            }
            c->current->scopedepth++;
            if (n->forn.init) EmitNode(c, n->forn.init);
            
            int l_idx = c->loopdepth++;
            c->loopstarts[l_idx] = ch->count;
            c->loopbreakcounts[l_idx] = 0;
            c->loopcontinuecounts[l_idx] = 0;
            c->loopscopes[l_idx] = c->current->scopedepth;
            
            int loopstart = ch->count;
            if (n->forn.inv_count > 0) {
                for (int i = 0; i < n->forn.inv_count; i++) {
                    CompileContractCheck(c, n->forn.invariants[i], "InvariantViolated", n->line);
                }
            }
            int exitjump = -1;
            if (n->forn.cond) {
                EmitExpr(c, n->forn.cond);
                exitjump = ChunkEmitJump(ch, OPJUMPIFFALSE, n->line);
            }
            EmitNode(c, n->forn.body);
            
            int continue_target = ch->count;
            if (n->forn.post) {
                EmitExpr(c, n->forn.post);
                ChunkWrite(ch, OPPOP, n->line);
            }
            
            ChunkWrite(ch, OPJUMP, n->line);
            int32_t back = (int32_t)(loopstart - ch->count - 4);
            ChunkWriteI32(ch, back, n->line);
            
            if (exitjump >= 0) ChunkPatchJump(ch, exitjump);
            
            for (int i = 0; i < c->loopcontinuecounts[l_idx]; i++) {
                int offset = c->loopcontinues[l_idx][i];
                int32_t diff = (int32_t)(continue_target - offset - 4);
                ch->code[offset  ] = (uint8_t)((diff >> 24) & 0xFF);
                ch->code[offset+1] = (uint8_t)((diff >> 16) & 0xFF);
                ch->code[offset+2] = (uint8_t)((diff >> 8) & 0xFF);
                ch->code[offset+3] = (uint8_t)(diff & 0xFF);
            }
            for (int i = 0; i < c->loopbreakcounts[l_idx]; i++) {
                ChunkPatchJump(ch, c->loopbreaks[l_idx][i]);
            }
            c->loopdepth--;
            c->current->scopedepth--;
            break;
        }
        case NBREAK: {
            if (c->loopdepth > 0) {
                int l_idx = c->loopdepth - 1;
                int loop_scope = c->loopscopes[l_idx];
                for (int d = c->current->scopedepth; d > loop_scope; d--) {
                    for (int i = 0; i < c->current->localcount; i++) {
                        Local *l = &c->current->locals[i];
                        if (l->depth == d && !l->ismoved) {
                            char dropname[256];
                            snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                            int fnid = ResolveGlobal(c, dropname, 0);
                            if (fnid >= 0) {
                                ChunkWrite(ch, OPLOAD, n->line);
                                ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, 1, n->line);
                                ChunkWrite(ch, OPPOP, n->line);
                            }
                        }
                    }
                }
                if (c->loopbreakcounts[l_idx] < 128) {
                    int offset = ChunkEmitJump(ch, OPJUMP, n->line);
                    c->loopbreaks[l_idx][c->loopbreakcounts[l_idx]++] = offset;
                } else {
                    Error(c, n->line, "Too many breaks in loop");
                }
            } else {
                Error(c, n->line, "break statement outside of loop");
            }
            break;
        }
        case NCONTINUE: {
            if (c->loopdepth > 0) {
                int l_idx = c->loopdepth - 1;
                int loop_scope = c->loopscopes[l_idx];
                for (int d = c->current->scopedepth; d > loop_scope; d--) {
                    for (int i = 0; i < c->current->localcount; i++) {
                        Local *l = &c->current->locals[i];
                        if (l->depth == d && !l->ismoved) {
                            char dropname[256];
                            snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                            int fnid = ResolveGlobal(c, dropname, 0);
                            if (fnid >= 0) {
                                ChunkWrite(ch, OPLOAD, n->line);
                                ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, 1, n->line);
                                ChunkWrite(ch, OPPOP, n->line);
                            }
                        }
                    }
                }
                if (c->loopcontinuecounts[l_idx] < 128) {
                    int offset = ChunkEmitJump(ch, OPJUMP, n->line);
                    c->loopcontinues[l_idx][c->loopcontinuecounts[l_idx]++] = offset;
                } else {
                    Error(c, n->line, "Too many continues in loop");
                }
            } else {
                Error(c, n->line, "continue statement outside of loop");
            }
            break;
        }
        case NRETURN: {
            extern int gContractCheck;
            Node *fn_ast = c->current->ast;
            if (fn_ast && fn_ast->type == NFNDECL && fn_ast->fn.ens_count > 0) {
                if (n->single.expr) {
                    EmitExpr(c, n->single.expr);
                } else {
                    ChunkWrite(ch, OPPUSHNULL, n->line);
                }
                int res_slot = ResolveLocal(c, "result");
                if (res_slot >= 0) {
                    ChunkWrite(ch, OPSTORE, n->line);
                    ChunkWriteU16(ch, (uint16_t)res_slot, n->line);
                } else {
                    ChunkWrite(ch, OPPOP, n->line);
                }
                for (int i = 0; i < fn_ast->fn.ens_count; i++) {
                    char err_msg[512];
                    snprintf(err_msg, sizeof(err_msg), "PostconditionViolated: %s failed", fn_ast->fn.name);
                    CompileContractCheck(c, fn_ast->fn.ensures[i], err_msg, n->line);
                }
                if (res_slot >= 0) {
                    ChunkWrite(ch, OPLOAD, n->line);
                    ChunkWriteU16(ch, (uint16_t)res_slot, n->line);
                } else {
                    ChunkWrite(ch, OPPUSHNULL, n->line);
                }
                for (int d = c->current->scopedepth; d >= 0; d--) {
                    for (int i = 0; i < c->current->localcount; i++) {
                        Local *l = &c->current->locals[i];
                        if (l->depth == d && !l->ismoved && strcmp(l->name, "result") != 0) {
                            char dropname[256];
                            snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                            int fnid = ResolveGlobal(c, dropname, 0);
                            if (fnid >= 0) {
                                ChunkWrite(ch, OPLOAD, n->line);
                                ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, 1, n->line);
                                ChunkWrite(ch, OPPOP, n->line);
                            }
                        }
                    }
                }
                ChunkWrite(ch, OPRETURNVAL, n->line);
                break;
            }


            if (gTypeCheckingEnabled && strcmp(c->current->name, "__main__") != 0) {
                const char *expected = c->current->rettype;
                const char *actual = "void";
                if (n->single.expr) {
                    actual = InferExprType(c, n->single.expr);
                }
                if (!TypesCompatible(c, expected, actual)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "TypeError: function '%s' expects return type '%s', got '%s'", c->current->name, expected, actual);
                    AppendUnionDetails(buf, sizeof(buf), expected);
                    Error(c, n->line, buf);
                } else if (strchr(expected, '{')) {
                    ChkRefConst(c, expected, n->line, n->single.expr);
                }
            }

            if(n->single.expr && n->single.expr->type == NCALL && !strchr(c->current->rettype, '{')) {
                Node *call = n->single.expr;
                for(int i=0; i<call->call.argc; i++) EmitExpr(c, call->call.args[i]);
                
                for (int d = c->current->scopedepth; d >= 0; d--) {
                    for (int i = 0; i < c->current->localcount; i++) {
                        Local *l = &c->current->locals[i];
                        if (l->depth == d && !l->ismoved) {
                            char dropname[256];
                            snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                            int fnid = ResolveGlobal(c, dropname, 0);
                            if (fnid >= 0) {
                                ChunkWrite(ch, OPLOAD, n->line);
                                ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, 1, n->line);
                                ChunkWrite(ch, OPPOP, n->line);
                            }
                        }
                    }
                }

                if(call->call.callee->type == NIDENT) {
                    int slot = ResolveLocal(c, call->call.callee->sval);
                    if(slot >= 0) {
                        ChunkWrite(ch, OPLOAD, call->line);
                        ChunkWriteU16(ch, (uint16_t)slot, call->line);
                        ChunkWrite(ch, OPTAILCALL, call->line);
                        ChunkWriteU16(ch, 65535, call->line);
                        ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                    } else {
                        int fnid = ResolveGlobal(c, call->call.callee->sval, 0);
                        if(fnid >= 0) {
                            ChunkWrite(ch, OPTAILCALL, call->line);
                            ChunkWriteU16(ch, (uint16_t)fnid, call->line);
                            ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                        } else {
                            ChunkWrite(ch, OPLOADGLOBAL, call->line);
                            int gsid = AddStr(c, call->call.callee->sval);
                            ChunkWriteU16(ch, (uint16_t)gsid, call->line);
                            ChunkWrite(ch, OPTAILCALL, call->line);
                            ChunkWriteU16(ch, 65535, call->line);
                            ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                        }
                    }
                } else {
                    EmitExpr(c, call->call.callee);
                    ChunkWrite(ch, OPTAILCALL, call->line);
                    ChunkWriteU16(ch, 65535, call->line);
                    ChunkWrite(ch, (uint8_t)call->call.argc, call->line);
                }
            } else if(n->single.expr){
                EmitExpr(c,n->single.expr);
                if (n->single.expr && n->single.expr->type == NIDENT) {
                    int rslot = ResolveLocal(c, n->single.expr->sval);
                    if (rslot >= 0) {
                        const char *type = c->current->locals[rslot].typename;
                        if (IsNonPrimitiveType(type)) {
                            c->current->locals[rslot].ismoved = 1;
                        }
                    }
                }
                if (strchr(c->current->rettype, '{')) {
                    EmitRefCheck(c, c->current->rettype, n->line, n->single.expr);
                }

                for (int d = c->current->scopedepth; d >= 0; d--) {
                    for (int i = 0; i < c->current->localcount; i++) {
                        Local *l = &c->current->locals[i];
                        if (l->depth == d && !l->ismoved) {
                            char dropname[256];
                            snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                            int fnid = ResolveGlobal(c, dropname, 0);
                            if (fnid >= 0) {
                                ChunkWrite(ch, OPLOAD, n->line);
                                ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, 1, n->line);
                                ChunkWrite(ch, OPPOP, n->line);
                            }
                        }
                    }
                }

                ChunkWrite(ch,OPRETURNVAL,n->line);
            } else {
                for (int d = c->current->scopedepth; d >= 0; d--) {
                    for (int i = 0; i < c->current->localcount; i++) {
                        Local *l = &c->current->locals[i];
                        if (l->depth == d && !l->ismoved) {
                            char dropname[256];
                            snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                            int fnid = ResolveGlobal(c, dropname, 0);
                            if (fnid >= 0) {
                                ChunkWrite(ch, OPLOAD, n->line);
                                ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                                ChunkWrite(ch, OPCALL, n->line);
                                ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                                ChunkWrite(ch, 1, n->line);
                                ChunkWrite(ch, OPPOP, n->line);
                            }
                        }
                    }
                }

                ChunkWrite(ch,OPRETURN,n->line);
            }
            break;
        }
        case NRESUME: {
            if (n->resumen.expr) {
                EmitExpr(c, n->resumen.expr);
                ChunkWrite(ch, OPRETURNVAL, n->line);
            } else {
                ChunkWrite(ch, OPPUSHNULL, n->line);
                ChunkWrite(ch, OPRETURNVAL, n->line);
            }
            break;
        }
        case NBLOCK: {
            c->current->scopedepth++;
            for(int i=0;i<n->block.count;i++) EmitNode(c,n->block.stmts[i]);
            for (int i = 0; i < c->current->localcount; i++) {
                Local *l = &c->current->locals[i];
                if (l->depth == c->current->scopedepth && !l->ismoved) {
                    char dropname[256];
                    snprintf(dropname, sizeof(dropname), "%s.drop", l->typename);
                    int fnid = ResolveGlobal(c, dropname, 0);
                    if (fnid >= 0) {
                        ChunkWrite(ch, OPLOAD, n->line);
                        ChunkWriteU16(ch, (uint16_t)l->slot, n->line);
                        ChunkWrite(ch, OPCALL, n->line);
                        ChunkWriteU16(ch, (uint16_t)fnid, n->line);
                        ChunkWrite(ch, 1, n->line);
                        ChunkWrite(ch, OPPOP, n->line);
                    }
                }
            }
            c->current->scopedepth--;
            break;
        }
        case NMACRODECL: {
            EmitExpr(c, n);
            break;
        }
        case NFNDECL: {
            if (n->fn.isgen) {
                DesugArgEn(c, n);
            }
            char *dot = strchr(n->fn.name, '.');
            if (dot) {
                int structnamelen = (int)(dot - n->fn.name);
                char structname[128];
                if (structnamelen < 127) {
                    strncpy(structname, n->fn.name, structnamelen);
                    structname[structnamelen] = '\0';
                    int structidx = -1;
                    for (int i = 0; i < c->structcount; i++) {
                        if (strcmp(c->structs[i].name, structname) == 0) {
                            structidx = i;
                            break;
                        }
                    }
                    if (structidx >= 0) {
                        CompilerStruct *s = &c->structs[structidx];
                        char *methodname = dot + 1;
                        int exists = 0;
                        for (int j = 0; j < s->fc; j++) {
                            if (strcmp(s->fnames[j], methodname) == 0) {
                                exists = 1;
                                break;
                            }
                        }
                        if (!exists) {
                            s->fnames = realloc(s->fnames, (s->fc + 1) * sizeof(char*));
                            s->ftypes = realloc(s->ftypes, (s->fc + 1) * sizeof(char*));
                            s->fnames[s->fc] = strdup(methodname);
                            
                            char sig[512] = "fn(";
                            for (int p = 0; p < n->fn.pc; p++) {
                                if (n->fn.ptypes && n->fn.ptypes[p]) {
                                    strcat(sig, n->fn.ptypes[p]);
                                } else {
                                    strcat(sig, "any");
                                }
                                if (p + 1 < n->fn.pc) strcat(sig, ",");
                            }
                            strcat(sig, ")->");
                            if (n->fn.ret) {
                                strcat(sig, n->fn.ret);
                            } else {
                                strcat(sig, "any");
                            }
                            s->ftypes[s->fc] = strdup(sig);
                            s->fc++;
                        }
                    }
                }
            }
            if (n->fn.tpc > 0) {
                if (c->templatecount >= 128) {
                    Error(c, n->line, "Too many templates registered");
                    break;
                }
                TemplateDecl *t = &c->templates[c->templatecount++];
                strncpy(t->name, n->fn.name, 127);
                t->ast = NodeCopy(n);
                break;
            }
            VerifyFnContracts(c, n);
            FnProto *parent=c->current;
            /* Apply namespace mangling */
            char *mangled = MangleName(c, n->fn.name);
            const char *fn_reg_name = mangled ? mangled : n->fn.name;
            /* Determine effective visibility for registration */
            int fn_vis = n->fn.visibility;
            if (fn_vis == VIS_DEFAULT) {
                fn_vis = (c->current_namespace[0] != '\0') ? VIS_PRIVATE : VIS_PUB;
            }
            RegisterSymbol(c, fn_reg_name, fn_vis, c->currentfilepath, c->current_namespace);
            FnProto *fn = NULL;
            for (int i = 0; i < c->fncount; i++) {
                if (c->fns[i] && strcmp(c->fns[i]->name, fn_reg_name) == 0) {
                    fn = c->fns[i];
                    break;
                }
            }
            if(!fn) {
                fn=NewFn(c, fn_reg_name);
            }
            if (mangled) {
                free(mangled);
            }
            if(!fn) {
                break;
            }
            fn->ast = n;
            fn->paramcount=n->fn.pc;
            fn->isasync=n->fn.isasync;
            if (n->fn.ret) strncpy(fn->rettype, n->fn.ret, 63);
            else strcpy(fn->rettype, "any");
            for (int i = 0; i < n->fn.pc; i++) {
                if (n->fn.ptypes[i]) strncpy(fn->paramtypes[i], n->fn.ptypes[i], 63);
                else if (i == 0 && strcmp(n->fn.pnames[i], "self") == 0 && strchr(n->fn.name, '.')) {
                    const char *dot = strchr(n->fn.name, '.');
                    size_t tlen = (size_t)(dot - n->fn.name);
                    if (tlen < 63) {
                        strncpy(fn->paramtypes[0], n->fn.name, tlen);
                        fn->paramtypes[0][tlen] = '\0';
                    } else {
                        strcpy(fn->paramtypes[0], "any");
                    }
                }
                else strcpy(fn->paramtypes[i], "any");
            }
            if (gTypeCheckingEnabled) {
                ChkStaticRecUrsion(c, n->fn.body, n->fn.name, 0);
                ChkEffects(c, n->fn.body, n->fn.effect);
            }
            c->current=fn;
            fn->scopedepth=0;
            // register params as local slots
            for(int i=0;i<n->fn.pc;i++) {
                int slot = AddLocal(c,n->fn.pnames[i],0);
                if (slot >= 0) {
                    strncpy(c->current->locals[slot].typename, fn->paramtypes[i], 63);
                }
            }
            for (int i = 0; i < n->fn.pc; i++) {
                if (n->fn.pdefaults && n->fn.pdefaults[i]) {
                    int slot = ResolveLocal(c, n->fn.pnames[i]);
                    if (slot >= 0) {
                        ChunkWrite(&fn->chunk, OPLOAD, n->line);
                        ChunkWriteU16(&fn->chunk, (uint16_t)slot, n->line);
                        ChunkWrite(&fn->chunk, OPPUSHNULL, n->line);
                        ChunkWrite(&fn->chunk, OPEQ, n->line);
                        
                        int jumpoffset = ChunkEmitJump(&fn->chunk, OPJUMPIFFALSE, n->line);
                        EmitExpr(c, n->fn.pdefaults[i]);
                        ChunkWrite(&fn->chunk, OPSTORE, n->line);
                        ChunkWriteU16(&fn->chunk, (uint16_t)slot, n->line);
                        
                        ChunkPatchJump(&fn->chunk, jumpoffset);
                    }
                }
            }
            if (n->fn.req_count > 0) {
                for (int i = 0; i < n->fn.req_count; i++) {
                    char err_msg[512];
                    snprintf(err_msg, sizeof(err_msg), "PreconditionViolated: %s failed", n->fn.name ? n->fn.name : "fn");
                    CompileContractCheck(c, n->fn.requires[i], err_msg, n->line);
                }
            }
            if (n->fn.ens_count > 0) {
                AddLocal(c, "result", 0);
            }
            if (n->fn.isvariadic) {
                ChunkWrite(&fn->chunk, OPRESTARGS, n->line);
                ChunkWrite(&fn->chunk, (uint8_t)(fn->paramcount - 1), n->line);
            }
            for (int i = 0; i < fn->paramcount; i++) {
                if (strchr(fn->paramtypes[i], '{')) {
                    ChunkWrite(&fn->chunk, OPLOAD, n->line);
                    ChunkWriteU16(&fn->chunk, (uint16_t)i, n->line);
                    EmitRefCheck(c, fn->paramtypes[i], n->line, NULL);
                    ChunkWrite(&fn->chunk, OPPOP, n->line);
                }
            }
            EmitNode(c,n->fn.body);
            if (n->fn.ens_count > 0) {
                int res_slot = ResolveLocal(c, "result");
                if (res_slot >= 0) {
                    ChunkWrite(&fn->chunk, OPPUSHNULL, n->line);
                    ChunkWrite(&fn->chunk, OPSTORE, n->line);
                    ChunkWriteU16(&fn->chunk, (uint16_t)res_slot, n->line);
                }
                for (int i = 0; i < n->fn.ens_count; i++) {
                    char err_msg[512];
                    snprintf(err_msg, sizeof(err_msg), "PostconditionViolated: %s failed", n->fn.name ? n->fn.name : "fn");
                    CompileContractCheck(c, n->fn.ensures[i], err_msg, n->line);
                }
            }
            // make sure we always return
            ChunkWrite(&fn->chunk,OPRETURN,n->line);
            c->current=parent;
            break;
        }
        case NTYPEALIAS: {
            int found = 0;
            for (int i = 0; i < c->aliascount; i++) {
                if (strcmp(c->aliases[i].name, n->typealias.name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (c->aliascount >= 128) {
                    Error(c, n->line, "Too many type aliases registered");
                    break;
                }
                CompilerTypeAlias *a = &c->aliases[c->aliascount++];
                strncpy(a->name, n->typealias.name, 127);
                strncpy(a->target, n->typealias.target, 255);
                a->isopaque = n->typealias.isopaque;
                a->tpc = n->typealias.tpc;
                if (n->typealias.tpc > 0) {
                    a->tparams = malloc(n->typealias.tpc * sizeof(char *));
                    for (int i = 0; i < n->typealias.tpc; i++) {
                        a->tparams[i] = strdup(n->typealias.tparams[i]);
                    }
                } else {
                    a->tparams = NULL;
                }
            }
            break;
        }
        case NSTRUCTDECL: {
            if (n->strct.tpc > 0) {
                int found = 0;
                for (int i = 0; i < c->templatecount; i++) {
                    if (strcmp(c->templates[i].name, n->strct.name) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (c->templatecount >= 128) {
                        Error(c, n->line, "Too many struct templates registered");
                        break;
                    }
                    TemplateDecl *t = &c->templates[c->templatecount++];
                    strncpy(t->name, n->strct.name, 127);
                    t->ast = NodeCopy(n);
                }
            } else {
                int structidx = -1;
                for (int i = 0; i < c->structcount; i++) {
                    if (strcmp(c->structs[i].name, n->strct.name) == 0) {
                        structidx = i;
                        break;
                    }
                }
                CompilerStruct *s;
                if (structidx < 0) {
                    if (c->structcount >= 512) {
                        Error(c, n->line, "Too many structs registered");
                        break;
                    }
                    s = &c->structs[c->structcount++];
                    strncpy(s->name, n->strct.name, 127);
                    s->fc = n->strct.fc;
                    s->fnames = malloc(s->fc * sizeof(char*));
                    s->ftypes = malloc(s->fc * sizeof(char*));
                    for (int i = 0; i < s->fc; i++) {
                        s->fnames[i] = strdup(n->strct.fnames[i]);
                        s->ftypes[i] = strdup(n->strct.ftypes[i]);
                    }
                    s->ic = n->strct.ic;
                    if (s->ic > 0) {
                        s->interfaces = malloc(s->ic * sizeof(char*));
                        for (int i = 0; i < s->ic; i++) {
                            s->interfaces[i] = strdup(n->strct.interfaces[i]);
                        }
                    } else {
                        s->interfaces = NULL;
                    }
                } else {
                    s = &c->structs[structidx];
                }
                if (s->ic > 0) {
                    for (int i = 0; i < s->ic; i++) {
                        int intfidx = -1;
                        for (int k = 0; k < c->interfacecount; k++) {
                            if (strcmp(c->interfaces[k].name, s->interfaces[i]) == 0) {
                                intfidx = k; break;
                            }
                        }
                        if (intfidx >= 0) {
                            CompilerInterface *inf = &c->interfaces[intfidx];
                            for (int j = 0; j < inf->count; j++) {
                                int foundinstruct = 0;
                                for (int f = 0; f < s->fc; f++) {
                                    if (strcmp(s->fnames[f], inf->fnames[j]) == 0) {
                                        foundinstruct = 1; break;
                                    }
                                }
                                if (!foundinstruct && inf->hasdefault[j] && inf->defaultbodies[j]) {
                                    char defaultfnname[256];
                                    snprintf(defaultfnname, sizeof(defaultfnname), "%s.%s", s->name, inf->fnames[j]);
                                    FnProto *fn = NewFn(c, defaultfnname);
                                    if (fn) {
                                        fn->paramcount = inf->paramcounts[j];
                                        fn->isasync = 0;
                                        strncpy(fn->rettype, inf->rettypes[j], 63);
                                        for (int k = 0; k < fn->paramcount; k++) {
                                            strncpy(fn->paramtypes[k], inf->paramtypes[j][k], 63);
                                        }
                                        FnProto *parent = c->current;
                                        c->current = fn;
                                        fn->scopedepth = 0;
                                        for (int k = 0; k < fn->paramcount; k++) {
                                            int slot = AddLocal(c, inf->paramnames[j][k], 0);
                                            if (slot >= 0) {
                                                strncpy(c->current->locals[slot].typename, fn->paramtypes[k], 63);
                                            }
                                        }
                                        EmitNode(c, inf->defaultbodies[j]);
                                        ChunkWrite(&fn->chunk, OPRETURN, n->line);
                                        c->current = parent;
                                    }
                                    
                                    s->fnames = realloc(s->fnames, (s->fc + 1) * sizeof(char*));
                                    s->ftypes = realloc(s->ftypes, (s->fc + 1) * sizeof(char*));
                                    s->fnames[s->fc] = strdup(inf->fnames[j]);
                                    
                                    char sig[512] = "fn(";
                                    for (int k = 0; k < inf->paramcounts[j]; k++) {
                                        strcat(sig, inf->paramtypes[j][k]);
                                        if (k + 1 < inf->paramcounts[j]) strcat(sig, ",");
                                    }
                                    strcat(sig, ")->");
                                    strcat(sig, inf->rettypes[j]);
                                    s->ftypes[s->fc] = strdup(sig);
                                    s->fc++;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case NACTORDECL: {
            int structidx = -1;
            for (int i = 0; i < c->structcount; i++) {
                if (strcmp(c->structs[i].name, n->actordecl.name) == 0) {
                    structidx = i;
                    break;
                }
            }
            CompilerStruct *s;
            if (structidx < 0) {
                if (c->structcount < 512) {
                    s = &c->structs[c->structcount++];
                    strncpy(s->name, n->actordecl.name, 127);
                    s->fc = n->actordecl.fc;
                    s->fnames = malloc(s->fc * sizeof(char*));
                    s->ftypes = malloc(s->fc * sizeof(char*));
                    for (int i = 0; i < s->fc; i++) {
                        s->fnames[i] = strdup(n->actordecl.fnames[i]);
                        s->ftypes[i] = strdup(n->actordecl.ftypes[i] ? n->actordecl.ftypes[i] : "any");
                    }
                    s->ic = 0;
                    s->interfaces = NULL;
                }
            }
            for (int i = 0; i < n->actordecl.mc; i++) {
                EmitNode(c, n->actordecl.methods[i]);
            }
            FnProto *fn = NewFn(c, n->actordecl.name);
            fn->paramcount = n->actordecl.fc;
            for (int i = 0; i < n->actordecl.fc; i++) {
                if (n->actordecl.ftypes && n->actordecl.ftypes[i]) strncpy(fn->paramtypes[i], n->actordecl.ftypes[i], 63);
                else strcpy(fn->paramtypes[i], "any");
            }
            FnProto *parent = c->current;
            c->current = fn;
            fn->scopedepth = 0;
            for (int i = 0; i < n->actordecl.fc; i++) {
                int slot = AddLocal(c, n->actordecl.fnames[i], 0);
                if (slot >= 0) {
                    strncpy(c->current->locals[slot].typename, fn->paramtypes[i], 63);
                }
            }

            ChunkWrite(&fn->chunk, OPNEWSTRUCT, n->line);
            int tid = AddStr(c, n->actordecl.name);
            ChunkWriteU16(&fn->chunk, (uint16_t)tid, n->line);
            for (int i = 0; i < n->actordecl.fc; i++) {
                ChunkWrite(&fn->chunk, OPLOAD, n->line);
                ChunkWriteU16(&fn->chunk, (uint16_t)i, n->line);
                ChunkWrite(&fn->chunk, OPSETFIELD, n->line);
                int fid = AddStr(c, n->actordecl.fnames[i]);
                ChunkWriteU16(&fn->chunk, (uint16_t)fid, n->line);
            }
            int fn_hmap_slot = AddLocal(c, "fn_hmap", 0);
            int obj_tid = AddStr(c, "Object");
            ChunkWrite(&fn->chunk, OPNEWSTRUCT, n->line);
            ChunkWriteU16(&fn->chunk, (uint16_t)obj_tid, n->line);
            ChunkWrite(&fn->chunk, OPSTORE, n->line);
            ChunkWriteU16(&fn->chunk, (uint16_t)fn_hmap_slot, n->line);
            for (int i = 0; i < n->actordecl.mc; i++) {
                Node *m = n->actordecl.methods[i];
                if (m && m->type == NFNDECL) {
                    char *mname = m->fn.name;
                    char *dot = strchr(mname, '.');
                    const char *shortname = dot ? (dot + 1) : mname;
                    int m_fnid = ResolveGlobal(c, mname, 0);
                    if (m_fnid >= 0) {
                        ChunkWrite(&fn->chunk, OPPUSHFN, n->line);
                        ChunkWriteU16(&fn->chunk, (uint16_t)m_fnid, n->line);
                        ChunkWrite(&fn->chunk, OPLOAD, n->line);
                        ChunkWriteU16(&fn->chunk, (uint16_t)fn_hmap_slot, n->line);
                        int fid = AddStr(c, shortname);
                        ChunkWrite(&fn->chunk, OPSETFIELD, n->line);
                        ChunkWriteU16(&fn->chunk, (uint16_t)fid, n->line);
                        ChunkWrite(&fn->chunk, OPPOP, n->line);
                    }
                }
            }
            ChunkWrite(&fn->chunk, OPLOAD, n->line);
            ChunkWriteU16(&fn->chunk, (uint16_t)fn_hmap_slot, n->line);

            int spawn_fn = ResolveGlobal(c, "ActorSpawn", 0);
            if (spawn_fn >= 0) {
                ChunkWrite(&fn->chunk, OPCALL, n->line);
                ChunkWriteU16(&fn->chunk, (uint16_t)spawn_fn, n->line);
                ChunkWrite(&fn->chunk, 2, n->line);
            }
            ChunkWrite(&fn->chunk, OPRETURNVAL, n->line);

            /* Also generate Actor.Spawn(initial_state) wrapper */
            char spawn_name[256];
            snprintf(spawn_name, sizeof(spawn_name), "%s.Spawn", n->actordecl.name);
            FnProto *sfn = NewFn(c, spawn_name);
            sfn->paramcount = 1;
            strcpy(sfn->paramtypes[0], "any");
            c->current = sfn;
            sfn->scopedepth = 0;
            AddLocal(c, "state", 0);
            ChunkWrite(&sfn->chunk, OPLOAD, n->line);
            ChunkWriteU16(&sfn->chunk, 0, n->line);

            int sfn_hmap_slot = AddLocal(c, "sfn_hmap", 0);
            ChunkWrite(&sfn->chunk, OPNEWSTRUCT, n->line);
            ChunkWriteU16(&sfn->chunk, (uint16_t)obj_tid, n->line);
            ChunkWrite(&sfn->chunk, OPSTORE, n->line);
            ChunkWriteU16(&sfn->chunk, (uint16_t)sfn_hmap_slot, n->line);
            for (int i = 0; i < n->actordecl.mc; i++) {
                Node *m = n->actordecl.methods[i];
                if (m && m->type == NFNDECL) {
                    char *mname = m->fn.name;
                    char *dot = strchr(mname, '.');
                    const char *shortname = dot ? (dot + 1) : mname;
                    int m_fnid = ResolveGlobal(c, mname, 0);
                    if (m_fnid >= 0) {
                        ChunkWrite(&sfn->chunk, OPPUSHFN, n->line);
                        ChunkWriteU16(&sfn->chunk, (uint16_t)m_fnid, n->line);
                        ChunkWrite(&sfn->chunk, OPLOAD, n->line);
                        ChunkWriteU16(&sfn->chunk, (uint16_t)sfn_hmap_slot, n->line);
                        int fid = AddStr(c, shortname);
                        ChunkWrite(&sfn->chunk, OPSETFIELD, n->line);
                        ChunkWriteU16(&sfn->chunk, (uint16_t)fid, n->line);
                        ChunkWrite(&sfn->chunk, OPPOP, n->line);
                    }
                }
            }
            ChunkWrite(&sfn->chunk, OPLOAD, n->line);
            ChunkWriteU16(&sfn->chunk, (uint16_t)sfn_hmap_slot, n->line);

            if (spawn_fn >= 0) {
                ChunkWrite(&sfn->chunk, OPCALL, n->line);
                ChunkWriteU16(&sfn->chunk, (uint16_t)spawn_fn, n->line);
                ChunkWrite(&sfn->chunk, 2, n->line);
            }
            ChunkWrite(&sfn->chunk, OPRETURNVAL, n->line);

            c->current = parent;
            break;
        }
        case NINTERFACEDECL: {
            int found = 0;
            for (int i = 0; i < c->interfacecount; i++) {
                if (strcmp(c->interfaces[i].name, n->interfacedecl.name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (c->interfacecount >= 128) {
                    Error(c, n->line, "Too many interfaces registered");
                    break;
                }
                CompilerInterface *inf = &c->interfaces[c->interfacecount++];
                strncpy(inf->name, n->interfacedecl.name, 127);
                inf->count = n->interfacedecl.count;
                inf->fnames = malloc(inf->count * sizeof(char*));
                inf->rettypes = malloc(inf->count * sizeof(char*));
                inf->paramtypes = malloc(inf->count * sizeof(char**));
                inf->paramnames = malloc(inf->count * sizeof(char**));
                inf->paramcounts = malloc(inf->count * sizeof(int));
                inf->hasdefault = malloc(inf->count * sizeof(int));
                inf->defaultbodies = malloc(inf->count * sizeof(Node*));
                for (int i = 0; i < inf->count; i++) {
                    inf->fnames[i] = strdup(n->interfacedecl.fnames[i]);
                    inf->rettypes[i] = strdup(n->interfacedecl.rettypes[i]);
                    inf->paramcounts[i] = n->interfacedecl.paramcounts[i];
                    inf->hasdefault[i] = n->interfacedecl.hasdefault ? n->interfacedecl.hasdefault[i] : 0;
                    inf->defaultbodies[i] = (n->interfacedecl.hasdefault && n->interfacedecl.hasdefault[i]) ? NodeCopy(n->interfacedecl.defaultbodies[i]) : NULL;
                    int pc = inf->paramcounts[i];
                    if (pc > 0) {
                        inf->paramtypes[i] = malloc(pc * sizeof(char*));
                        inf->paramnames[i] = malloc(pc * sizeof(char*));
                        for (int j = 0; j < pc; j++) {
                            inf->paramtypes[i][j] = strdup(n->interfacedecl.paramtypes[i][j]);
                            inf->paramnames[i][j] = strdup(n->interfacedecl.paramnames[i][j]);
                        }
                    } else {
                        inf->paramtypes[i] = NULL;
                        inf->paramnames[i] = NULL;
                    }
                }
                inf->assoccount = n->interfacedecl.assoccount;
                if (inf->assoccount > 0) {
                    inf->assoctypes = malloc(inf->assoccount * sizeof(char*));
                    for (int i = 0; i < inf->assoccount; i++) {
                        inf->assoctypes[i] = strdup(n->interfacedecl.assoctypes[i]);
                    }
                } else {
                    inf->assoctypes = NULL;
                }
            }
            break;
        }
        case NENUMDECL: {
            int enumidx = -1;
            for (int i = 0; i < c->enumcount; i++) {
                if (strcmp(c->enums[i].name, n->enumdecl.name) == 0) {
                    enumidx = i;
                    break;
                }
            }
            CompilerEnum *en;
            if (enumidx < 0) {
                if (c->enumcount >= 128) {
                    Error(c, n->line, "Too many enums registered");
                    break;
                }
                en = &c->enums[c->enumcount++];
                strncpy(en->name, n->enumdecl.name, 127);
                en->count = n->enumdecl.count;
                en->isadt = n->enumdecl.isadt;
                en->members = malloc(en->count * sizeof(char*));
                en->values = malloc(en->count * sizeof(int64_t));
                for (int i = 0; i < en->count; i++) {
                    en->members[i] = strdup(n->enumdecl.members[i]);
                    en->values[i] = n->enumdecl.values[i];
                }
            } else {
                en = &c->enums[enumidx];
            }
            
            if (en->isadt) {
                // generate ADT constructor helper
                for (int i = 0; i < en->count; i++) {
                    char constructorname[256];
                    snprintf(constructorname, sizeof(constructorname), "%s.%s", en->name, en->members[i]);
                    
                    int paramcount = n->enumdecl.membertypecounts[i];
                    FnProto *fn = NewFn(c, constructorname);
                    if (!fn) continue;
                    fn->paramcount = paramcount;
                    fn->localcount = paramcount + 1; // arg slots 0..N-1, slot N is struct
                    strncpy(fn->rettype, en->name, 63);
                    for (int p = 0; p < paramcount; p++) {
                        if (n->enumdecl.membertypes[i][p]) {
                            strncpy(fn->paramtypes[p], n->enumdecl.membertypes[i][p], 63);
                        } else {
                            strcpy(fn->paramtypes[p], "any");
                        }
                    }
                    
                    Chunk *ch = &fn->chunk;
                    int line = n->line;
                    
                    // create variant struct
                    int variantnameid = AddStr(c, constructorname);
                    ChunkWrite(ch, OPNEWSTRUCT, line);
                    ChunkWriteU16(ch, (uint16_t)variantnameid, line);
                    
                    // store struct in local slot
                    ChunkWrite(ch, OPSTORE, line);
                    ChunkWriteU16(ch, (uint16_t)paramcount, line);
                    
                    // set struct tag
                    int tagid = AddStr(c, "$tag");
                    ChunkWrite(ch, OPPUSHSTR, line);
                    ChunkWriteU16(ch, (uint16_t)variantnameid, line);
                    ChunkWrite(ch, OPLOAD, line);
                    ChunkWriteU16(ch, (uint16_t)paramcount, line);
                    ChunkWrite(ch, OPSETFIELD, line);
                    ChunkWriteU16(ch, (uint16_t)tagid, line);
                    ChunkWrite(ch, OPPOP, line); // pop setfield value
                    
                    // populate fields
                    for (int p = 0; p < paramcount; p++) {
                        char fieldname[32];
                        snprintf(fieldname, sizeof(fieldname), "field%d", p);
                        int fid = AddStr(c, fieldname);
                        
                        ChunkWrite(ch, OPLOAD, line);
                        ChunkWriteU16(ch, (uint16_t)p, line); // push parameter value
                        ChunkWrite(ch, OPLOAD, line);
                        ChunkWriteU16(ch, (uint16_t)paramcount, line); // push struct
                        ChunkWrite(ch, OPSETFIELD, line);
                        ChunkWriteU16(ch, (uint16_t)fid, line);
                        ChunkWrite(ch, OPPOP, line); // pop setfield value
                    }
                    
                    // load and return
                    ChunkWrite(ch, OPLOAD, line);
                    ChunkWriteU16(ch, (uint16_t)paramcount, line);
                    ChunkWrite(ch, OPRETURNVAL, line);
                }
                
                // generate toString method
                char tostrname[256];
                snprintf(tostrname, sizeof(tostrname), "%s.ToString", en->name);
                FnProto *fntostr = NewFn(c, tostrname);
                if (fntostr) {
                    fntostr->paramcount = 1;
                    fntostr->localcount = 1;
                    strncpy(fntostr->rettype, "string", 63);
                    strcpy(fntostr->paramtypes[0], en->name);
                    
                    Chunk *chtostr = &fntostr->chunk;
                    int line = n->line;
                    int tagid = AddStr(c, "$tag");
                    int strfnid = ResolveGlobal(c, "String", 0);
                    
                    for (int i = 0; i < en->count; i++) {
                        char constructorname[256];
                        snprintf(constructorname, sizeof(constructorname), "%s.%s", en->name, en->members[i]);
                        int variantnameid = AddStr(c, constructorname);
                        
                        ChunkWrite(chtostr, OPLOAD, line);
                        ChunkWriteU16(chtostr, 0, line);
                        ChunkWrite(chtostr, OPGETFIELD, line);
                        ChunkWriteU16(chtostr, (uint16_t)tagid, line);
                        
                        ChunkWrite(chtostr, OPPUSHSTR, line);
                        ChunkWriteU16(chtostr, (uint16_t)variantnameid, line);
                        
                        ChunkWrite(chtostr, OPEQ, line);
                        int jumpiffalse = ChunkEmitJump(chtostr, OPJUMPIFFALSE, line);
                        
                        int membernameid = AddStr(c, en->members[i]);
                        ChunkWrite(chtostr, OPPUSHSTR, line);
                        ChunkWriteU16(chtostr, (uint16_t)membernameid, line);
                        
                        int paramcount = n->enumdecl.membertypecounts[i];
                        if (paramcount > 0) {
                            int openparenid = AddStr(c, "(");
                            ChunkWrite(chtostr, OPPUSHSTR, line);
                            ChunkWriteU16(chtostr, (uint16_t)openparenid, line);
                            ChunkWrite(chtostr, OPADD, line);
                            
                            for (int p = 0; p < paramcount; p++) {
                                char fieldname[32];
                                snprintf(fieldname, sizeof(fieldname), "field%d", p);
                                int fid = AddStr(c, fieldname);
                                
                                ChunkWrite(chtostr, OPLOAD, line);
                                ChunkWriteU16(chtostr, 0, line);
                                ChunkWrite(chtostr, OPGETFIELD, line);
                                ChunkWriteU16(chtostr, (uint16_t)fid, line);
                                
                                if (strfnid >= 0) {
                                    ChunkWrite(chtostr, OPCALL, line);
                                    ChunkWriteU16(chtostr, (uint16_t)strfnid, line);
                                    ChunkWrite(chtostr, 1, line);
                                }
                                
                                ChunkWrite(chtostr, OPADD, line);
                                
                                if (p + 1 < paramcount) {
                                    int commaid = AddStr(c, ", ");
                                    ChunkWrite(chtostr, OPPUSHSTR, line);
                                    ChunkWriteU16(chtostr, (uint16_t)commaid, line);
                                    ChunkWrite(chtostr, OPADD, line);
                                }
                            }
                            
                            int closeparenid = AddStr(c, ")");
                            ChunkWrite(chtostr, OPPUSHSTR, line);
                            ChunkWriteU16(chtostr, (uint16_t)closeparenid, line);
                            ChunkWrite(chtostr, OPADD, line);
                        }
                        
                        ChunkWrite(chtostr, OPRETURNVAL, line);
                        ChunkPatchJump(chtostr, jumpiffalse);
                    }
                    
                    int unknownid = AddStr(c, "Unknown ADT");
                    ChunkWrite(chtostr, OPPUSHSTR, line);
                    ChunkWriteU16(chtostr, (uint16_t)unknownid, line);
                    ChunkWrite(chtostr, OPRETURNVAL, line);
                }
                
                // generate eq method
                char eqname[256];
                snprintf(eqname, sizeof(eqname), "%s.Eq", en->name);
                FnProto *fneq = NewFn(c, eqname);
                if (fneq) {
                    fneq->paramcount = 2;
                    fneq->localcount = 2;
                    strncpy(fneq->rettype, "bool", 63);
                    strcpy(fneq->paramtypes[0], en->name);
                    strcpy(fneq->paramtypes[1], en->name);
                    
                    Chunk *cheq = &fneq->chunk;
                    int line = n->line;
                    int tagid = AddStr(c, "$tag");
                    
                    ChunkWrite(cheq, OPLOAD, line);
                    ChunkWriteU16(cheq, 0, line);
                    ChunkWrite(cheq, OPGETFIELD, line);
                    ChunkWriteU16(cheq, (uint16_t)tagid, line);
                    
                    ChunkWrite(cheq, OPLOAD, line);
                    ChunkWriteU16(cheq, 1, line);
                    ChunkWrite(cheq, OPGETFIELD, line);
                    ChunkWriteU16(cheq, (uint16_t)tagid, line);
                    
                    ChunkWrite(cheq, OPEQ, line);
                    int sametagjump = ChunkEmitJump(cheq, OPJUMPIFFALSE, line);
                    
                    for (int i = 0; i < en->count; i++) {
                        char constructorname[256];
                        snprintf(constructorname, sizeof(constructorname), "%s.%s", en->name, en->members[i]);
                        int variantnameid = AddStr(c, constructorname);
                        
                        ChunkWrite(cheq, OPLOAD, line);
                        ChunkWriteU16(cheq, 0, line);
                        ChunkWrite(cheq, OPGETFIELD, line);
                        ChunkWriteU16(cheq, (uint16_t)tagid, line);
                        
                        ChunkWrite(cheq, OPPUSHSTR, line);
                        ChunkWriteU16(cheq, (uint16_t)variantnameid, line);
                        
                        ChunkWrite(cheq, OPEQ, line);
                        int variantmismatchjump = ChunkEmitJump(cheq, OPJUMPIFFALSE, line);
                        
                        int paramcount = n->enumdecl.membertypecounts[i];
                        int fieldmismatchjumps[128];
                        int mismatchcount = 0;
                        
                        for (int p = 0; p < paramcount; p++) {
                            char fieldname[32];
                            snprintf(fieldname, sizeof(fieldname), "field%d", p);
                            int fid = AddStr(c, fieldname);
                            
                            ChunkWrite(cheq, OPLOAD, line);
                            ChunkWriteU16(cheq, 0, line);
                            ChunkWrite(cheq, OPGETFIELD, line);
                            ChunkWriteU16(cheq, (uint16_t)fid, line);
                            
                            ChunkWrite(cheq, OPLOAD, line);
                            ChunkWriteU16(cheq, 1, line);
                            ChunkWrite(cheq, OPGETFIELD, line);
                            ChunkWriteU16(cheq, (uint16_t)fid, line);
                            
                            ChunkWrite(cheq, OPEQ, line);
                            fieldmismatchjumps[mismatchcount++] = ChunkEmitJump(cheq, OPJUMPIFFALSE, line);
                        }
                        
                        ChunkWrite(cheq, OPPUSHBOOL, line);
                        ChunkWrite(cheq, 1, line);
                        ChunkWrite(cheq, OPRETURNVAL, line);
                        
                        for (int m = 0; m < mismatchcount; m++) {
                            ChunkPatchJump(cheq, fieldmismatchjumps[m]);
                        }
                        
                        ChunkPatchJump(cheq, variantmismatchjump);
                    }
                    
                    ChunkWrite(cheq, OPPUSHBOOL, line);
                    ChunkWrite(cheq, 0, line);
                    ChunkWrite(cheq, OPRETURNVAL, line);
                    
                    ChunkPatchJump(cheq, sametagjump);
                    ChunkWrite(cheq, OPPUSHBOOL, line);
                    ChunkWrite(cheq, 0, line);
                    ChunkWrite(cheq, OPRETURNVAL, line);
                }
            }
            break;
        }
        case NMODULE: {
            /* Save current namespace, build new nested namespace */
            char saved_ns[512];
            strncpy(saved_ns, c->current_namespace, 511);
            saved_ns[511] = '\0';
            if (c->current_namespace[0] != '\0') {
                /* nested: parent::child */
                char new_ns[512];
                snprintf(new_ns, sizeof(new_ns), "%s::%s", c->current_namespace, n->single.var);
                strncpy(c->current_namespace, new_ns, 511);
            } else {
                strncpy(c->current_namespace, n->single.var, 511);
            }
            c->current_namespace[511] = '\0';
            /* Compile the module body with the new namespace active */
            if (n->single.expr) EmitNode(c, n->single.expr);
            /* Restore namespace */
            strncpy(c->current_namespace, saved_ns, 511);
            c->current_namespace[511] = '\0';
            break;
        }
        case NUSE: {
            /* [pub] use path::item  OR  use path::* */
            const char *target = n->decl.tname;
            if (!target) break;
            int vis = n->decl.visibility;
            /* Wildcard: use math::*  -  import all pub symbols from that namespace */
            size_t tlen = strlen(target);
            if (tlen >= 3 && strcmp(target + tlen - 3, "::*") == 0) {
                /* Extract namespace prefix */
                char ns_prefix[256];
                strncpy(ns_prefix, target, tlen - 3);
                ns_prefix[tlen - 3] = '\0';
                /* Scan all known functions and register aliases for public ones in that ns */
                size_t pfx_len = strlen(ns_prefix);
                for (int fi = 0; fi < c->fncount; fi++) {
                    const char *fname = c->fns[fi]->name;
                    if (strncmp(fname, ns_prefix, pfx_len) == 0 && fname[pfx_len] == ':' && fname[pfx_len+1] == ':') {
                        const char *basename = fname + pfx_len + 2;
                        /* Check visibility in symbol table */
                        int sym_vis = VIS_PUB;
                        for (int si = 0; si < c->mod_symbol_count; si++) {
                            if (strcmp(c->mod_symbols[si].name, fname) == 0) {
                                sym_vis = c->mod_symbols[si].visibility;
                                break;
                            }
                        }
                        if (sym_vis == VIS_PUB || sym_vis == VIS_DEFAULT) {
                            RegisterAliasHelper(c, basename, fname, vis, c->currentfilepath);
                        }
                    }
                }
            } else {
                /* Specific: use math::add  -> alias "add" -> "math::add" */
                const char *last_sep = strrchr(target, ':');
                if (last_sep && last_sep > target && *(last_sep-1) == ':') {
                    const char *basename = last_sep + 1;
                    RegisterAliasHelper(c, basename, target, vis, c->currentfilepath);
                    /* Also register full path as itself (for use math::add; then math::add(...) ) */
                    RegisterAliasHelper(c, target, target, vis, c->currentfilepath);
                } else {
                    /* No separator  -  plain "use foo;" just register as itself */
                    RegisterAliasHelper(c, target, target, vis, c->currentfilepath);
                }
            }
            break;
        }
        case NIMPORT: {
            /* Lazy imports are compiled on first use via load_lazy_module */
            if (n->decl.isconstflag) break;
            const char *path = n->single.var;
            char *resolved = PkgResolveImport(path, c->currentfilepath);
            /* Make a stable copy of the final path */
            char finalpath_buf[512];
            strncpy(finalpath_buf, resolved ? resolved : path, 511);
            finalpath_buf[511] = '\0';
            if (resolved) { free(resolved); resolved = NULL; }
            const char *finalpath = finalpath_buf;

            /* Circular import detection */
            int is_circular = 0;
            for (int ci = 0; ci < c->import_depth; ci++) {
                if (strcmp(c->active_imports[ci], finalpath) == 0) {
                    is_circular = 1;
                    char chain[2048] = "";
                    for (int ci2 = 0; ci2 < c->import_depth; ci2++) {
                        if (chain[0]) strncat(chain, " -> ", sizeof(chain)-strlen(chain)-1);
                        strncat(chain, c->active_imports[ci2], sizeof(chain)-strlen(chain)-1);
                    }
                    strncat(chain, " -> ", sizeof(chain)-strlen(chain)-1);
                    strncat(chain, finalpath, sizeof(chain)-strlen(chain)-1);
                    strncat(chain, " (cycle!)", sizeof(chain)-strlen(chain)-1);
                    char errbuf2[2200];
                    snprintf(errbuf2, sizeof(errbuf2),
                             "CircularImportError: circular import detected:\n  %s", chain);
                    Error(c, n->line, errbuf2);
                    break;
                }
            }
            if (is_circular) break;

            /* Import deduplication: skip if already compiled */
            int already_done = 0;
            for (int di = 0; di < c->compiled_file_count; di++) {
                if (strcmp(c->compiled_files[di], finalpath) == 0) {
                    already_done = 1;
                    break;
                }
            }
            if (already_done) break;

            char *src = CompReadFile(finalpath);
            if (!src) {
                char errbuf3[600];
                snprintf(errbuf3, sizeof(errbuf3),
                         "Could not open imported module file: %s", finalpath);
                Error(c, n->line, errbuf3);
                break;
            }
            int parseerr = 0;
            Node *importedast = Parse(src, &parseerr);
            free(src);
            if (parseerr || !importedast) {
                char errbuf4[600];
                snprintf(errbuf4, sizeof(errbuf4),
                         "Failed to parse imported module: %s", finalpath);
                Error(c, n->line, errbuf4);
                if (importedast) NodeFree(importedast);
                break;
            }
            /* Push active import stack */
            if (c->import_depth < 64) {
                strncpy(c->active_imports[c->import_depth], finalpath, 511);
                c->active_imports[c->import_depth][511] = '\0';
                c->import_depth++;
            }
            char savedpath[512];
            strncpy(savedpath, c->currentfilepath, sizeof(savedpath) - 1);
            savedpath[sizeof(savedpath) - 1] = '\0';
            strncpy(c->currentfilepath, finalpath, sizeof(c->currentfilepath) - 1);
            c->currentfilepath[sizeof(c->currentfilepath) - 1] = '\0';
            EmitNode(c, importedast);
            strncpy(c->currentfilepath, savedpath, sizeof(c->currentfilepath) - 1);
            c->currentfilepath[sizeof(c->currentfilepath) - 1] = '\0';
            /* Pop active import stack */
            if (c->import_depth > 0) c->import_depth--;
            /* Mark file as compiled */
            if (c->compiled_file_count < 256) {
                strncpy(c->compiled_files[c->compiled_file_count], finalpath, 511);
                c->compiled_files[c->compiled_file_count][511] = '\0';
                c->compiled_file_count++;
            }
            if (c->imported_ast_count < 1024) {
                c->imported_asts[c->imported_ast_count++] = importedast;
            } else {
                NodeFree(importedast);
            }
            break;
        }
        case NFORIN: {
            if (n->forin.isasync) {
                char itervar[64];
                char itemvar[64];
                snprintf(itervar, sizeof(itervar), "__iter_tmp_%d", c->current->localcount);
                snprintf(itemvar, sizeof(itemvar), "%s", n->forin.var);

                Node *iterdecl = NodeNew(NVARDECL, n->line);
                iterdecl->decl.name = strdup(itervar);
                iterdecl->decl.tname = strdup("any");
                iterdecl->decl.isconstflag = 0;
                iterdecl->decl.init = NodeCopy(n->forin.iter);

                Node *cond = NodeNew(NBOOL, n->line);
                cond->sval = strdup("true");

                Node *whilebody = NodeNew(NBLOCK, n->line);
                whilebody->block.count = 3;
                whilebody->block.stmts = malloc(3 * sizeof(Node*));

                Node *itemdecl = NodeNew(NVARDECL, n->line);
                itemdecl->decl.name = strdup(itemvar);
                itemdecl->decl.tname = strdup("any");
                itemdecl->decl.isconstflag = 0;
                itemdecl->decl.init = NodeNew(NNULL, n->line);
                
                Node *typecall = NodeNew(NCALL, n->line);
                typecall->call.callee = NodeNew(NIDENT, n->line);
                typecall->call.callee->sval = strdup("type");
                typecall->call.argc = 1;
                typecall->call.args = malloc(sizeof(Node*));
                typecall->call.args[0] = NodeNew(NIDENT, n->line);
                typecall->call.args[0]->sval = strdup(itervar);
                typecall->call.isspread = calloc(1, sizeof(int));
                
                Node *chanstr = NodeNew(NSTR, n->line);
                chanstr->sval = strdup("channel");
                
                Node *ischanop = NodeNew(NBINOP, n->line);
                ischanop->binop.op = strdup("==");
                ischanop->binop.left = typecall;
                ischanop->binop.right = chanstr;

                Node *thenblock = NodeNew(NBLOCK, n->line);
                thenblock->block.count = 1;
                thenblock->block.stmts = malloc(sizeof(Node*));
                
                Node *recvcall = NodeNew(NCALL, n->line);
                recvcall->call.callee = NodeNew(NIDENT, n->line);
                recvcall->call.callee->sval = strdup("Receive");
                recvcall->call.argc = 1;
                recvcall->call.args = malloc(sizeof(Node*));
                recvcall->call.args[0] = NodeNew(NIDENT, n->line);
                recvcall->call.args[0]->sval = strdup(itervar);
                recvcall->call.isspread = calloc(1, sizeof(int));

                Node *thenassign = NodeNew(NASSIGN, n->line);
                thenassign->assign.target = NodeNew(NIDENT, n->line);
                thenassign->assign.target->sval = strdup(itemvar);
                thenassign->assign.value = recvcall;

                Node *thenstmt = NodeNew(NEXPRSTMT, n->line);
                thenstmt->single.expr = thenassign;
                thenblock->block.stmts[0] = thenstmt;

                Node *elseblock = NodeNew(NBLOCK, n->line);
                elseblock->block.count = 1;
                elseblock->block.stmts = malloc(sizeof(Node*));

                Node *nextmember = NodeNew(NMEMBER, n->line);
                nextmember->member.obj = NodeNew(NIDENT, n->line);
                nextmember->member.obj->sval = strdup(itervar);
                nextmember->member.field = strdup("next");

                Node *nextcall = NodeNew(NCALL, n->line);
                nextcall->call.callee = nextmember;
                nextcall->call.argc = 0;
                nextcall->call.args = NULL;
                nextcall->call.isspread = NULL;

                Node *awaitexpr = NodeNew(NAWAIT, n->line);
                awaitexpr->awaitn.expr = nextcall;
                awaitexpr->awaitn.timeout = NULL;

                Node *elseassign = NodeNew(NASSIGN, n->line);
                elseassign->assign.target = NodeNew(NIDENT, n->line);
                elseassign->assign.target->sval = strdup(itemvar);
                elseassign->assign.value = awaitexpr;

                Node *elsestmt = NodeNew(NEXPRSTMT, n->line);
                elsestmt->single.expr = elseassign;
                elseblock->block.stmts[0] = elsestmt;

                Node *ifstmt = NodeNew(NIF, n->line);
                ifstmt->ifn.cond = ischanop;
                ifstmt->ifn.then = thenblock;
                ifstmt->ifn.els = elseblock;

                Node *nullval = NodeNew(NNULL, n->line);
                Node *isnullop = NodeNew(NBINOP, n->line);
                isnullop->binop.op = strdup("==");
                isnullop->binop.left = NodeNew(NIDENT, n->line);
                isnullop->binop.left->sval = strdup(itemvar);
                isnullop->binop.right = nullval;

                Node *breakthen = NodeNew(NBLOCK, n->line);
                breakthen->block.count = 1;
                breakthen->block.stmts = malloc(sizeof(Node*));
                breakthen->block.stmts[0] = NodeNew(NBREAK, n->line);

                Node *breakif = NodeNew(NIF, n->line);
                breakif->ifn.cond = isnullop;
                breakif->ifn.then = breakthen;
                breakif->ifn.els = NULL;

                Node *userbody = NodeCopy(n->forin.body);

                Node *initblock = NodeNew(NBLOCK, n->line);
                initblock->block.count = 2;
                initblock->block.stmts = malloc(2 * sizeof(Node*));
                initblock->block.stmts[0] = itemdecl;
                initblock->block.stmts[1] = ifstmt;

                whilebody->block.stmts[0] = initblock;
                whilebody->block.stmts[1] = breakif;
                whilebody->block.stmts[2] = userbody;

                Node *whilestmt = NodeNew(NWHILE, n->line);
                whilestmt->whilen.cond = cond;
                whilestmt->whilen.body = whilebody;

                Node *outerblock = NodeNew(NBLOCK, n->line);
                outerblock->block.count = 2;
                outerblock->block.stmts = malloc(2 * sizeof(Node*));
                outerblock->block.stmts[0] = iterdecl;
                outerblock->block.stmts[1] = whilestmt;

                EmitNode(c, outerblock);
                NodeFree(outerblock);
                break;
            }
            char itervar[64];
            char ivar[64];
            snprintf(itervar, sizeof(itervar), "__iter_tmp_%d", c->current->localcount);
            snprintf(ivar, sizeof(ivar), "__i_tmp_%d", c->current->localcount + 1);
            
            Node *iterdecl = NodeNew(NVARDECL, n->line);
            iterdecl->decl.name = strdup(itervar);
            iterdecl->decl.tname = strdup("any");
            iterdecl->decl.isconstflag = 0;
            iterdecl->decl.init = NodeCopy(n->forin.iter);
            
            Node *idecl = NodeNew(NVARDECL, n->line);
            idecl->decl.name = strdup(ivar);
            idecl->decl.tname = strdup("int");
            idecl->decl.isconstflag = 0;
            idecl->decl.init = NodeNew(NINT, n->line);
            idecl->decl.init->ival = 0;
            
            Node *cond = NodeNew(NBINOP, n->line);
            cond->binop.op = strdup("<");
            cond->binop.left = NodeNew(NIDENT, n->line);
            cond->binop.left->sval = strdup(ivar);
            
            Node *lencall = NodeNew(NCALL, n->line);
            lencall->call.callee = NodeNew(NIDENT, n->line);
            lencall->call.callee->sval = strdup("Length");
            lencall->call.argc = 1;
            lencall->call.args = malloc(sizeof(Node*));
            lencall->call.args[0] = NodeNew(NIDENT, n->line);
            lencall->call.args[0]->sval = strdup(itervar);
            lencall->call.isspread = calloc(1, sizeof(int));
            lencall->call.tc = 0;
            lencall->call.targs = NULL;
            cond->binop.right = lencall;
            
            Node *whilebody = NodeNew(NBLOCK, n->line);
            whilebody->block.count = 3;
            whilebody->block.stmts = malloc(3 * sizeof(Node*));
            
            Node *vardecl = NodeNew(NVARDECL, n->line);
            vardecl->decl.name = strdup(n->forin.var);
            vardecl->decl.tname = strdup("any");
            vardecl->decl.isconstflag = 0;
            Node *idxexpr = NodeNew(NINDEX, n->line);
            idxexpr->index.arr = NodeNew(NIDENT, n->line);
            idxexpr->index.arr->sval = strdup(itervar);
            idxexpr->index.idx = NodeNew(NIDENT, n->line);
            idxexpr->index.idx->sval = strdup(ivar);
            vardecl->decl.init = idxexpr;
            whilebody->block.stmts[0] = vardecl;
            
            whilebody->block.stmts[1] = NodeCopy(n->forin.body);
            
            Node *incassign = NodeNew(NASSIGN, n->line);
            incassign->assign.target = NodeNew(NIDENT, n->line);
            incassign->assign.target->sval = strdup(ivar);
            Node *addop = NodeNew(NBINOP, n->line);
            addop->binop.op = strdup("+");
            addop->binop.left = NodeNew(NIDENT, n->line);
            addop->binop.left->sval = strdup(ivar);
            addop->binop.right = NodeNew(NINT, n->line);
            addop->binop.right->ival = 1;
            incassign->assign.value = addop;
            
            Node *incstmt = NodeNew(NEXPRSTMT, n->line);
            incstmt->single.expr = incassign;
            incstmt->single.var = NULL;
            whilebody->block.stmts[2] = incstmt;
            
            Node *whilestmt = NodeNew(NWHILE, n->line);
            whilestmt->whilen.cond = cond;
            whilestmt->whilen.body = whilebody;
            
            Node *outerblock = NodeNew(NBLOCK, n->line);
            outerblock->block.count = 3;
            outerblock->block.stmts = malloc(3 * sizeof(Node*));
            outerblock->block.stmts[0] = iterdecl;
            outerblock->block.stmts[1] = idecl;
            outerblock->block.stmts[2] = whilestmt;
            
            EmitNode(c, outerblock);
            NodeFree(outerblock);
            break;
        }
        case NTRYCATCH: {
            c->trydepth++;
            int trypushoffset = ChunkEmitJump(ch, OPTRYPUSH, n->line);
            EmitNode(c, n->trycatch.tryblock);
            c->trydepth--;
            ChunkWrite(ch, OPTRYPOP, n->line);
            int skipcatchjump = ChunkEmitJump(ch, OPJUMP, n->line);
            ChunkPatchJump(ch, trypushoffset);
            c->current->scopedepth++;
            int errslot = AddLocal(c, n->trycatch.errvar, 0);
            ChunkWrite(ch, OPSTORE, n->line);
            ChunkWriteU16(ch, (uint16_t)errslot, n->line);
            EmitNode(c, n->trycatch.catchblock);
            c->current->scopedepth--;
            ChunkPatchJump(ch, skipcatchjump);
            break;
        }
        case NTHROW: {
            EmitExpr(c, n->throwstmt.expr);
            ChunkWrite(ch, OPTHROW, n->line);
            break;
        }
        case NPROGRAM: {
            /* Pass 0: Recursively register all use-aliases (including pub use
             * re-exports inside module blocks) so they are available when
             * compiling function bodies in Pass 2. */
            for (int i = 0; i < n->block.count; i++) {
                PreregisterAllUses(c, n->block.stmts[i]);
            }
            /* Pass 1: Emit all type, struct, actor, interface, enum, and macro declarations first. */
            for (int i = 0; i < n->block.count; i++) {
                if (!n->block.stmts[i]) continue;
                NType t = n->block.stmts[i]->type;
                if (t == NSTRUCTDECL || t == NENUMDECL || t == NTYPEALIAS || t == NMACRODECL || t == NACTORDECL || t == NINTERFACEDECL) {
                    EmitNode(c, n->block.stmts[i]);
                }
            }
            /* Pass 2: Emit all function declarations so they are fully compiled
             * and available for compile-time function execution (CTFE). */
            for (int i = 0; i < n->block.count; i++) {
                if (n->block.stmts[i] && n->block.stmts[i]->type == NFNDECL) {
                    EmitNode(c, n->block.stmts[i]);
                }
            }
            /* Pass 3: Emit all other statements (main execution code) */
            int last_non_fn_idx = -1;
            for (int i = 0; i < n->block.count; i++) {
                if (!n->block.stmts[i]) continue;
                NType t = n->block.stmts[i]->type;
                if (t != NSTRUCTDECL && t != NENUMDECL && t != NTYPEALIAS && t != NMACRODECL && t != NACTORDECL && t != NINTERFACEDECL && t != NFNDECL) {
                    last_non_fn_idx = i;
                }
            }
            for (int i = 0; i < n->block.count; i++) {
                if (!n->block.stmts[i]) continue;
                NType t = n->block.stmts[i]->type;
                if (t == NSTRUCTDECL || t == NENUMDECL || t == NTYPEALIAS || t == NMACRODECL || t == NACTORDECL || t == NINTERFACEDECL || t == NFNDECL) continue;

                if (i == last_non_fn_idx && c->iseval) {
                    if (n->block.stmts[i]->type == NEXPRSTMT) {
                        EmitExpr(c, n->block.stmts[i]->single.expr);
                    } else if (n->block.stmts[i]->type == NRETURN) {
                        Node *retstmt = n->block.stmts[i];
                        if (retstmt->single.expr) {
                            EmitExpr(c, retstmt->single.expr);
                        } else {
                            ChunkWrite(ch, OPPUSHNULL, n->line);
                        }
                    } else {
                        EmitNode(c, n->block.stmts[i]);
                        ChunkWrite(ch, OPPUSHNULL, n->line);
                    }
                } else {
                    EmitNode(c, n->block.stmts[i]);
                }
            }
            if (c->iseval && last_non_fn_idx == -1) {
                ChunkWrite(ch, OPPUSHNULL, n->line);
            }
            break;
        }
        case NASM: {
            int strid = AddStr(c, n->asmstmt.code ? n->asmstmt.code : "");
            ChunkWrite(ch, OPASM, n->line);
            ChunkWriteU16(ch, (uint16_t)strid, n->line);
            ChunkWrite(ch, OPPOP, n->line);
            break;
        }
        case NEFFECTDECL: {
            int sid = AddStr(c, n->effectdecl.name);
            ChunkWrite(ch, OPNEWSTRUCT, n->line);
            ChunkWriteU16(ch, (uint16_t)sid, n->line);
            ChunkWrite(ch, OPSTOREGLOBAL, n->line);
            ChunkWriteU16(ch, (uint16_t)sid, n->line);
            for (int i = 0; i < n->effectdecl.opcount; i++) {
                int fsid = AddStr(c, n->effectdecl.opnames[i]);
                int full_op_len = (int)strlen(n->effectdecl.name) + 1 + (int)strlen(n->effectdecl.opnames[i]) + 1;
                char *full_op = malloc(full_op_len);
                snprintf(full_op, full_op_len, "%s.%s", n->effectdecl.name, n->effectdecl.opnames[i]);
                int val_sid = AddStr(c, full_op);
                free(full_op);
                ChunkWrite(ch, OPPUSHSTR, n->line);
                ChunkWriteU16(ch, (uint16_t)val_sid, n->line);
                ChunkWrite(ch, OPLOADGLOBAL, n->line);
                ChunkWriteU16(ch, (uint16_t)sid, n->line);
                ChunkWrite(ch, OPSETFIELD, n->line);
                ChunkWriteU16(ch, (uint16_t)fsid, n->line);
                ChunkWrite(ch, OPPOP, n->line);
            }
            break;
        }
        case NCONST_EVAL: {
            if (n->single.expr) {
                EmitNode(c, n->single.expr);
            }
            break;
        }
        case NATOMIC: {
            EmitExpr(c, n);
            ChunkWrite(ch, OPPOP, n->line);
            break;
        }
        default:
            // expression statement
            EmitExpr(c,n);
            ChunkWrite(ch,OPPOP,n->line);
            break;
    }
}

// public APIs
char g_current_file[512] = "";

Compiler *CompNew(void) {
    Compiler *c=calloc(1,sizeof(Compiler));
    strncpy(c->currentfilepath, g_current_file, sizeof(c->currentfilepath) - 1);
    c->currentfilepath[sizeof(c->currentfilepath) - 1] = '\0';
    c->strcap=16;
    c->strpool=malloc(c->strcap*sizeof(char*));
    c->isrepl=0;
    c->iseval=0;
    // always create the top-level "main" function
    FnProto *mainfn=NewFn(c,"__main__");
    c->current=mainfn;

    // register built-in standard library functions
    NewFn(c, "Length");
    NewFn(c, "clock");
    NewFn(c, "SquareRoot");
    NewFn(c, "panic");
    NewFn(c, "SetPanicHook");
    NewFn(c, "cause");
    NewFn(c, "Absolute");
    NewFn(c, "String");
    NewFn(c, "append");
    NewFn(c, "Power");
    NewFn(c, "sin");
    NewFn(c, "cos");
    NewFn(c, "tan");
    NewFn(c, "log");
    NewFn(c, "Ceiling");
    NewFn(c, "floor");
    NewFn(c, "Minimum");
    NewFn(c, "Maximum");
    NewFn(c, "pop");
    NewFn(c, "insert");
    NewFn(c, "remove");
    NewFn(c, "slice");
    NewFn(c, "Ordinal");
    NewFn(c, "Character");
    NewFn(c, "Substring");
    NewFn(c, "type");
    NewFn(c, "WriteFile");
    NewFn(c, "ReadFile");
    NewFn(c, "WriteFileAsync");
    NewFn(c, "ReadFileAsync");
    NewFn(c, "SocketListen");
    NewFn(c, "SocketAccept");
    NewFn(c, "SocketReceive");
    NewFn(c, "SocketSend");
    NewFn(c, "SocketClose");
    NewFn(c, "SocketConnect");
    NewFn(c, "UdpSocket");
    NewFn(c, "UdpSendTo");
    NewFn(c, "UdpReceiveFrom");
    NewFn(c, "Execute");
    NewFn(c, "exit");
    NewFn(c, "Random");
    NewFn(c, "round");

    // advanced Libraries - String
    NewFn(c, "split");
    NewFn(c, "replace");
    NewFn(c, "trim");
    NewFn(c, "lower");
    NewFn(c, "upper");
    NewFn(c, "find");
    NewFn(c, "starts");
    NewFn(c, "ends");

    // advanced Libraries - Array
    NewFn(c, "sort");
    NewFn(c, "Concatenate");
    NewFn(c, "contains");

    // advanced Libraries - Struct Reflection
    NewFn(c, "keys");
    NewFn(c, "values");

    // advanced Libraries - Math
    NewFn(c, "asin");
    NewFn(c, "acos");
    NewFn(c, "atan");
    NewFn(c, "atan2");
    NewFn(c, "sinh");
    NewFn(c, "cosh");
    NewFn(c, "tanh");
    NewFn(c, "Integer");
    NewFn(c, "float");

    // comprehensive Math Library Extensions
    NewFn(c, "asinh");
    NewFn(c, "acosh");
    NewFn(c, "atanh");
    NewFn(c, "exp");
    NewFn(c, "log10");
    NewFn(c, "log2");
    NewFn(c, "cbrt");
    NewFn(c, "trunc");
    NewFn(c, "Hypotenuse");
    NewFn(c, "erf");
    NewFn(c, "erfc");
    NewFn(c, "tgamma");
    NewFn(c, "lgamma");
    NewFn(c, "GreatestCommonDivisor");
    NewFn(c, "LeastCommonMultiple");
    NewFn(c, "Factorial");
    NewFn(c, "nPr");
    NewFn(c, "nCr");
    NewFn(c, "IsPrime");
    NewFn(c, "ToRadians");
    NewFn(c, "ToDegrees");
    NewFn(c, "mean");
    NewFn(c, "median");
    NewFn(c, "StandardDeviation");
    NewFn(c, "variance");

    // advanced Libraries - System
    NewFn(c, "time");
    NewFn(c, "sleep");
    NewFn(c, "exists");
    NewFn(c, "RemoveFile");
    NewFn(c, "MakeDirectory");
    NewFn(c, "GetEnv");
    NewFn(c, "SetEnv");
    NewFn(c, "GetCap");
    NewFn(c, "html");
    NewFn(c, "shell");
    NewFn(c, "path");
    NewFn(c, "SecureString");
    NewFn(c, "CryptCompare");
    NewFn(c, "ConstantTimeCompare");
    NewFn(c, "ShmOpen");
    NewFn(c, "ShmWrite");
    NewFn(c, "ShmRead");
    NewFn(c, "ShmClose");
    NewFn(c, "breakpoint");
    NewFn(c, "ClipboardSet");
    NewFn(c, "ClipboardGet");
    NewFn(c, "notify");
    NewFn(c, "TrayAdd");
    NewFn(c, "TrayRemove");
    NewFn(c, "win");
    NewFn(c, "Button");
    NewFn(c, "MessageBox");
    NewFn(c, "loop");
    NewFn(c, "SdlInit");
    NewFn(c, "SdlWin");
    NewFn(c, "SdlClear");
    NewFn(c, "SdlRect");
    NewFn(c, "SdlCircle");
    NewFn(c, "SdlEvent");
    NewFn(c, "GuiDrag");
    NewFn(c, "GuiDrop");
    NewFn(c, "DomGet");
    NewFn(c, "DomSet");

    NewFn(c, "JsonParse");
    NewFn(c, "JsonStringify");
    NewFn(c, "JsonParseStream");
    NewFn(c, "typeof");
    NewFn(c, "CborEncode");
    NewFn(c, "CborDecode");
    NewFn(c, "MessagePackEncode");
    NewFn(c, "MessagePackDecode");
    NewFn(c, "CsvParse");
    NewFn(c, "CsvStringify");
    NewFn(c, "TomlParse");
    NewFn(c, "TomlStringify");
    NewFn(c, "XmlParse");
    NewFn(c, "XmlStringify");
    NewFn(c, "ProtobufEncodeObj");
    NewFn(c, "ProtobufDecodeObj");
    NewFn(c, "Utf8Len");
    NewFn(c, "Utf8CharAt");
    NewFn(c, "Utf8CodepointAt");
    NewFn(c, "Utf8ToChars");
    NewFn(c, "Utf8FromCodepoints");
    NewFn(c, "RegexMatch");
    NewFn(c, "RegexSearch");
    NewFn(c, "RegexReplace");
    NewFn(c, "UnicodeNormalize");
    NewFn(c, "LocaleCompare");

    // cryptography and Encodings
    NewFn(c, "MD5");
    NewFn(c, "SHA256");
    NewFn(c, "SHA512");
    NewFn(c, "SHA3256");
    NewFn(c, "SHA3512");
    NewFn(c, "AES256GCMEncrypt");
    NewFn(c, "AES256GCMDecrypt");
    NewFn(c, "Chacha20Poly1305Encrypt");
    NewFn(c, "Chacha20Poly1305Decrypt");
    NewFn(c, "HMACSHA256");
    NewFn(c, "HMACSHA512");
    NewFn(c, "PBKDF2");
    NewFn(c, "Scrypt");
    NewFn(c, "Argon2");
    NewFn(c, "RSAKeygen");
    NewFn(c, "RSASign");
    NewFn(c, "RSAVerify");
    NewFn(c, "ECDSAKeygen");
    NewFn(c, "ECDSASign");
    NewFn(c, "ECDSAVerify");
    NewFn(c, "Ed25519Keygen");
    NewFn(c, "Ed25519Sign");
    NewFn(c, "Ed25519Verify");
    NewFn(c, "X25519Keygen");
    NewFn(c, "X25519DH");
    NewFn(c, "SecureRandom");
    NewFn(c, "HexEncode");
    NewFn(c, "HexDecode");
    NewFn(c, "B64Enc");
    NewFn(c, "B64Dec");

    // Url encoding
    NewFn(c, "UrlEnc");
    NewFn(c, "UrlDec");

    // date/Time Formatting
    NewFn(c, "date");

    // system Information
    NewFn(c, "platform");
    NewFn(c, "CurrentWorkingDirectory");

    // FFI support
    NewFn(c, "FFILoad");
    NewFn(c, "FFIFree");
    NewFn(c, "FFICall");
    NewFn(c, "COMCall");
    NewFn(c, "free");

    // SIMD support
    NewFn(c, "SimdAddRaw");
    NewFn(c, "SimdMulRaw");
    NewFn(c, "SimdSubRaw");
    NewFn(c, "SimdDivRaw");

    // concurrency Support
    NewFn(c, "Channel");
    NewFn(c, "send");
    NewFn(c, "Send");
    NewFn(c, "receive");
    NewFn(c, "Receive");
    NewFn(c, "close");
    NewFn(c, "Close");
    NewFn(c, "ChannelSend");
    NewFn(c, "ChannelReceive");
    NewFn(c, "ChannelClose");
    NewFn(c, "mutex");
    NewFn(c, "lock");
    NewFn(c, "unlock");
    NewFn(c, "Semaphore");
    NewFn(c, "wait");
    NewFn(c, "post");
    NewFn(c, "AtomAdd");
    NewFn(c, "AtomSub");
    NewFn(c, "AtomGet");
    NewFn(c, "AtomSet");
    NewFn(c, "AtomCompareAndSwap");
    NewFn(c, "ParallelMapRaw");
    NewFn(c, "ParallelReduceRaw");
    NewFn(c, "actor");
    NewFn(c, "ActorSend");
    NewFn(c, "cancel");
    NewFn(c, "IsCancelled");
    NewFn(c, "CurrentTask");

    // metaprogramming and Reflection Builtins
    NewFn(c, "fields");
    NewFn(c, "has");
    NewFn(c, "get");
    NewFn(c, "set");
    NewFn(c, "GetFunction");
    NewFn(c, "FunctionName");
    NewFn(c, "globals");
    NewFn(c, "Evaluate");
    NewFn(c, "CheckIntersect");

    // Missing Networking and Huffman Builtins
    NewFn(c, "HuffmanDecode");
    NewFn(c, "DnsResolve");
    NewFn(c, "TlsHandshake");
    NewFn(c, "TlsSend");
    NewFn(c, "TlsReceive");
    NewFn(c, "TlsClose");

    // Math, SIMD, fft, bigint extensions
    NewFn(c, "SimdOp");
    NewFn(c, "AssertSnapshot");
    NewFn(c, "SimdFma");
    NewFn(c, "MatrixMulRaw");
    NewFn(c, "MatrixTransposeRaw");
    NewFn(c, "MatrixInverseRaw");
    NewFn(c, "MatrixLuRaw");
    NewFn(c, "MatrixCholeskyRaw");
    NewFn(c, "FftRaw");
    NewFn(c, "BigIntAdd");
    NewFn(c, "BigIntSub");
    NewFn(c, "BigIntMul");
    NewFn(c, "BigIntDiv");
    NewFn(c, "BigIntCmp");

    // Arena Allocator
    NewFn(c, "MakeArena");
    NewFn(c, "ArenaAllocArray");
    NewFn(c, "ArenaAllocStruct");
    NewFn(c, "ArenaFree");

    // Next-Gen Runtime Builtins
    NewFn(c, "ReinterpretCast");
    NewFn(c, "spawn_remote");
    NewFn(c, "MatMul");
    NewFn(c, "NurseryNew");
    NewFn(c, "RegionNew");

    // Systems Built-in Functions
    NewFn(c, "MMapOpen");
    NewFn(c, "MMapRead");
    NewFn(c, "MMapWrite");
    NewFn(c, "MMapFlush");
    NewFn(c, "MMapClose");

    NewFn(c, "AtomicI64New");
    NewFn(c, "AtomicI64Free");
    NewFn(c, "AtomicI64Load");
    NewFn(c, "AtomicI64Store");
    NewFn(c, "AtomicI64Add");
    NewFn(c, "AtomicI64Sub");
    NewFn(c, "AtomicI64Cas");
    NewFn(c, "AtomicI64Exchange");
    NewFn(c, "AtomicFence");

    NewFn(c, "ProcessSpawn");
    NewFn(c, "ProcessWriteStdin");
    NewFn(c, "ProcessReadStdout");
    NewFn(c, "ProcessReadStderr");
    NewFn(c, "ProcessWait");
    NewFn(c, "ProcessKill");
    NewFn(c, "ProcessExitCode");
    NewFn(c, "ProcessClose");

    NewFn(c, "SystemCpuCount");
    NewFn(c, "SystemMemoryTotal");
    NewFn(c, "SystemMemoryAvailable");
    NewFn(c, "SystemPid");

    NewFn(c, "NativeLibraryLoad");
    NewFn(c, "NativeLibraryGet");
    NewFn(c, "NativeCallInt0");
    NewFn(c, "NativeCallInt1");
    NewFn(c, "NativeCallInt2");
    NewFn(c, "NativeLibraryFree");

    NewFn(c, "TimerNowNanos");
    NewFn(c, "TimerNowMicros");
    NewFn(c, "TimerNowMillis");

    NewFn(c, "SocketListen");
    NewFn(c, "SocketAccept");
    NewFn(c, "SocketConnect");
    NewFn(c, "SocketSend");
    NewFn(c, "SocketRecv");
    NewFn(c, "SocketClose");

    NewFn(c, "SecureRandomBytes");
    NewFn(c, "SecureRandomI64");
    NewFn(c, "SecureRandomUuid");

    NewFn(c, "FileWatcherNew");
    NewFn(c, "FileWatcherPoll");
    NewFn(c, "FileWatcherClose");

    NewFn(c, "SimdVec8fNew");
    NewFn(c, "SimdVec8fAdd");
    NewFn(c, "SimdVec8fMul");
    NewFn(c, "SimdVec8fFmadd");
    NewFn(c, "SimdVec8fSum");
    NewFn(c, "SimdVec8fToArray");
    NewFn(c, "SimdVec8fFree");

    NewFn(c, "UdpSocketBind");
    NewFn(c, "UdpSocketNew");
    NewFn(c, "UdpSocketSendTo");
    NewFn(c, "UdpSocketRecvFrom");
    NewFn(c, "UdpSocketClose");

    NewFn(c, "ArenaNew");
    NewFn(c, "ArenaAlloc");
    NewFn(c, "ArenaReset");
    NewFn(c, "ArenaUsed");
    NewFn(c, "ArenaFree");

    NewFn(c, "LockFreeQueueNew");
    NewFn(c, "LockFreeQueuePush");
    NewFn(c, "LockFreeQueuePop");
    NewFn(c, "LockFreeQueueFree");

    NewFn(c, "CryptoSha256");
    NewFn(c, "CryptoSha512");
    NewFn(c, "CryptoHmacSha256");

    NewFn(c, "CompressDeflate");
    NewFn(c, "CompressInflate");

    NewFn(c, "ThreadLocalNew");
    NewFn(c, "ThreadLocalSet");
    NewFn(c, "ThreadLocalGet");
    NewFn(c, "ThreadLocalFree");

    NewFn(c, "MemoryPoolNew");
    NewFn(c, "MemoryPoolAlloc");
    NewFn(c, "MemoryPoolFree");
    NewFn(c, "MemoryPoolAvailable");
    NewFn(c, "MemoryPoolDestroy");

    NewFn(c, "IpcQueueCreate");
    NewFn(c, "IpcQueueOpen");
    NewFn(c, "IpcQueuePush");
    NewFn(c, "IpcQueuePop");
    NewFn(c, "IpcQueueClose");

    NewFn(c, "EnvGet");
    NewFn(c, "EnvSet");
    NewFn(c, "EnvCwd");
    NewFn(c, "EnvSetCwd");

    NewFn(c, "BitPopCount");
    NewFn(c, "BitClz");
    NewFn(c, "BitCtz");
    NewFn(c, "BitRotl");
    NewFn(c, "BitRotr");
    NewFn(c, "BitByteSwap");

    NewFn(c, "FileSystemSize");
    NewFn(c, "FileSystemIsFile");
    NewFn(c, "FileSystemIsDir");
    NewFn(c, "FileSystemModifiedTime");
    NewFn(c, "FileSystemCopy");
    NewFn(c, "FileSystemWriteText");
    NewFn(c, "FileSystemReadText");
    NewFn(c, "FileSystemDelete");

    NewFn(c, "AesGcmEncrypt");
    NewFn(c, "AesGcmDecrypt");

    NewFn(c, "MemoryProtectSet");

    NewFn(c, "ProcessWorkingSet");
    NewFn(c, "ProcessPeakWorkingSet");
    NewFn(c, "ProcessCpuTime");

    NewFn(c, "OsSignalIgnoreBreak");
    NewFn(c, "OsSignalGenerateInterrupt");

    NewFn(c, "UuidGenerate");
    NewFn(c, "UuidIsValid");

    NewFn(c, "CpuPhysicalCores");
    NewFn(c, "CpuLogicalCores");
    NewFn(c, "CpuPageSize");

    NewFn(c, "NamedPipeCreateServer");
    NewFn(c, "NamedPipeCall");
    NewFn(c, "NamedPipeClose");

    NewFn(c, "FastMathSqrtVec8f");
    NewFn(c, "FastMathRsqrtVec8f");
    NewFn(c, "FastMathMinVec8f");
    NewFn(c, "FastMathMaxVec8f");

    NewFn(c, "RegistryGetString");
    NewFn(c, "RegistrySetString");
    NewFn(c, "RegistryGetDword");
    NewFn(c, "RegistrySetDword");
    NewFn(c, "RegistryDeleteValue");

    NewFn(c, "ConsoleSetColor");
    NewFn(c, "ConsoleResetColor");
    NewFn(c, "ConsoleSetCursorPos");
    NewFn(c, "ConsoleGetSize");

    NewFn(c, "DiskVolumeFreeSpace");
    NewFn(c, "DiskVolumeTotalSpace");
    NewFn(c, "DiskVolumeVolumeName");
    NewFn(c, "DiskVolumeFileSystemName");

    NewFn(c, "ThreadSyncCreateMutex");
    NewFn(c, "ThreadSyncLock");
    NewFn(c, "ThreadSyncUnlock");
    NewFn(c, "ThreadSyncDestroy");

    NewFn(c, "MemoryStreamNew");
    NewFn(c, "MemoryStreamWriteByte");
    NewFn(c, "MemoryStreamWriteInt32");
    NewFn(c, "MemoryStreamWriteInt64");
    NewFn(c, "MemoryStreamWriteString");
    NewFn(c, "MemoryStreamReadByte");
    NewFn(c, "MemoryStreamReadInt32");
    NewFn(c, "MemoryStreamReadInt64");
    NewFn(c, "MemoryStreamReadString");
    NewFn(c, "MemoryStreamSeek");
    NewFn(c, "MemoryStreamPosition");
    NewFn(c, "MemoryStreamLength");
    NewFn(c, "MemoryStreamToHex");
    NewFn(c, "MemoryStreamDestroy");

    NewFn(c, "SystemPowerAcStatus");
    NewFn(c, "SystemPowerBatteryPercent");
    NewFn(c, "SystemPowerBatteryLifeTime");
    NewFn(c, "SystemPowerIsCharging");

    NewFn(c, "NetworkInterfacePrimaryIp");
    NewFn(c, "NetworkInterfacePrimaryMac");

    NewFn(c, "FastMatrixIdentity");
    NewFn(c, "FastMatrixMultiply");
    NewFn(c, "FastMatrixTransformVec4");
    NewFn(c, "FastMatrixTranspose");

    NewFn(c, "OsSemaphoreCreate");
    NewFn(c, "OsSemaphoreAcquire");
    NewFn(c, "OsSemaphoreRelease");
    NewFn(c, "OsSemaphoreDestroy");

    NewFn(c, "ProcessWorkingSetCurrent");
    NewFn(c, "ProcessWorkingSetPeak");
    NewFn(c, "ProcessWorkingSetPageFaults");
    NewFn(c, "ProcessWorkingSetTrim");

    NewFn(c, "HardwareTimestampRdtsc");
    NewFn(c, "HardwareTimestampRdtscp");
    NewFn(c, "HardwareTimestampTicksPerSecond");
    NewFn(c, "HardwareTimestampNanoseconds");

    NewFn(c, "FileLockAcquireShared");
    NewFn(c, "FileLockAcquireExclusive");
    NewFn(c, "FileLockRelease");
    NewFn(c, "FileLockIsLocked");

    NewFn(c, "FastChecksumCrc32c");
    NewFn(c, "FastChecksumCrc32cUpdate");
    NewFn(c, "FastChecksumAdler32");
    NewFn(c, "FastChecksumFnv1a64");

    NewFn(c, "OsEventCreate");
    NewFn(c, "OsEventSet");
    NewFn(c, "OsEventReset");
    NewFn(c, "OsEventWait");
    NewFn(c, "OsEventDestroy");

    NewFn(c, "EntropyPoolHardwareSeed");
    NewFn(c, "EntropyPoolHardwareRandomInt64");
    NewFn(c, "EntropyPoolHardwareEntropyAvailable");
    NewFn(c, "EntropyPoolSystemEntropyBytes");

    NewFn(c, "ThreadSchedulerSetAffinity");
    NewFn(c, "ThreadSchedulerGetProcessAffinity");
    NewFn(c, "ThreadSchedulerSetPriority");
    NewFn(c, "ThreadSchedulerGetPriority");

    NewFn(c, "SystemLocaleCurrentLocaleName");
    NewFn(c, "SystemLocaleTimezoneName");
    NewFn(c, "SystemLocaleTimezoneBiasMinutes");
    NewFn(c, "SystemLocaleIsDaylightSaving");

    NewFn(c, "WaitableTimerCreate");
    NewFn(c, "WaitableTimerSet");
    NewFn(c, "WaitableTimerWait");
    NewFn(c, "WaitableTimerCancel");
    NewFn(c, "WaitableTimerDestroy");

    NewFn(c, "FastGeometryDot3");
    NewFn(c, "FastGeometryCross3");
    NewFn(c, "FastGeometryNormalize3");
    NewFn(c, "FastGeometryDistance3");

    NewFn(c, "ProcessTelemetryCommandLine");
    NewFn(c, "ProcessTelemetryExecutablePath");
    NewFn(c, "ProcessTelemetryProcessId");
    NewFn(c, "ProcessTelemetryThreadId");
    NewFn(c, "ProcessTelemetryKernelUserCpuTimes");

    NewFn(c, "ReadWriteLockNew");
    NewFn(c, "ReadWriteLockLockShared");
    NewFn(c, "ReadWriteLockUnlockShared");
    NewFn(c, "ReadWriteLockLockExclusive");
    NewFn(c, "ReadWriteLockUnlockExclusive");
    NewFn(c, "ReadWriteLockTryLockShared");
    NewFn(c, "ReadWriteLockTryLockExclusive");
    NewFn(c, "ReadWriteLockDestroy");

    NewFn(c, "AlignedBufferAlloc");
    NewFn(c, "AlignedBufferWriteBytes");
    NewFn(c, "AlignedBufferReadBytes");
    NewFn(c, "AlignedBufferIsAligned");
    NewFn(c, "AlignedBufferFree");

    NewFn(c, "SimdVec8iAdd");
    NewFn(c, "SimdVec8iSub");
    NewFn(c, "SimdVec8iMul");
    NewFn(c, "SimdVec8iBitwiseAnd");
    NewFn(c, "SimdVec8iBitwiseOr");
    NewFn(c, "SimdVec8iBitwiseXor");

    NewFn(c, "CryptoKdfPbkdf2Sha256");
    NewFn(c, "CryptoKdfPbkdf2Sha512");
    NewFn(c, "CryptoKdfVerifyPassword");

    NewFn(c, "SystemDisplayPrimaryResolution");
    NewFn(c, "SystemDisplayVirtualScreenBounds");
    NewFn(c, "SystemDisplayWorkArea");
    NewFn(c, "SystemDisplayMonitorCount");

    NewFn(c, "SharedSpinlockOpenOrCreate");
    NewFn(c, "SharedSpinlockAcquire");
    NewFn(c, "SharedSpinlockTryAcquire");
    NewFn(c, "SharedSpinlockRelease");
    NewFn(c, "SharedSpinlockDestroy");

    NewFn(c, "ThreadBarrierNew");
    NewFn(c, "ThreadBarrierWait");
    NewFn(c, "ThreadBarrierDestroy");

    NewFn(c, "JobObjectCreate");
    NewFn(c, "JobObjectAssignProcess");
    NewFn(c, "JobObjectSetMemoryLimit");
    NewFn(c, "JobObjectTerminateAll");
    NewFn(c, "JobObjectDestroy");

    NewFn(c, "SharedRingBufferCreate");
    NewFn(c, "SharedRingBufferWrite");
    NewFn(c, "SharedRingBufferRead");
    NewFn(c, "SharedRingBufferAvailableRead");
    NewFn(c, "SharedRingBufferAvailableWrite");
    NewFn(c, "SharedRingBufferDestroy");

    NewFn(c, "CryptoHmacSha256");
    NewFn(c, "CryptoHmacSha512");
    NewFn(c, "CryptoHmacVerify");

    NewFn(c, "SimdVec4dAdd");
    NewFn(c, "SimdVec4dSub");
    NewFn(c, "SimdVec4dMul");
    NewFn(c, "SimdVec4dDiv");
    NewFn(c, "SimdVec4dSqrt");
    NewFn(c, "SimdVec4dDot");

    NewFn(c, "SystemIdentityUserName");
    NewFn(c, "SystemIdentityIsAdmin");
    NewFn(c, "SystemIdentityProcessIntegrityLevel");

    NewFn(c, "ConditionVariableNew");
    NewFn(c, "ConditionVariableWaitSRW");
    NewFn(c, "ConditionVariableNotifyOne");
    NewFn(c, "ConditionVariableNotifyAll");
    NewFn(c, "ConditionVariableDestroy");

    NewFn(c, "ProcessPrioritySetClass");
    NewFn(c, "ProcessPriorityGetClass");
    NewFn(c, "ProcessPrioritySetEcoMode");

    NewFn(c, "FastMemSearchFindByte");
    NewFn(c, "FastMemSearchFindSequence");
    NewFn(c, "FastMemSearchCountByte");

    NewFn(c, "CryptoChaCha20Encrypt");
    NewFn(c, "CryptoChaCha20Decrypt");

    NewFn(c, "SimdVec16sAdd");
    NewFn(c, "SimdVec16sSub");
    NewFn(c, "SimdVec16sMul");
    NewFn(c, "SimdVec16sAddSaturated");
    NewFn(c, "SimdVec16sSubSaturated");

    NewFn(c, "VirtualMemoryMapQueryAddress");
    NewFn(c, "VirtualMemoryMapPageSize");
    NewFn(c, "VirtualMemoryMapAllocationGranularity");

    NewFn(c, "ThreadPoolWorkCreate");
    NewFn(c, "ThreadPoolWorkSubmit");
    NewFn(c, "ThreadPoolWorkWait");
    NewFn(c, "ThreadPoolWorkClose");

    NewFn(c, "SymLinkCreateFile");
    NewFn(c, "SymLinkCreateDirectory");
    NewFn(c, "SymLinkResolve");
    NewFn(c, "SymLinkIsSymLink");

    NewFn(c, "StopwatchFrequency");
    NewFn(c, "StopwatchCounter");

    NewFn(c, "SimdVec32u8AddSaturated");
    NewFn(c, "SimdVec32u8SubSaturated");
    NewFn(c, "SimdVec32u8Average");
    NewFn(c, "SimdVec32u8Min");
    NewFn(c, "SimdVec32u8Max");

    NewFn(c, "ProcessMemInfoQuery");

    NewFn(c, "FilePrefetchVirtualRange");
    NewFn(c, "FilePrefetchSetAdvice");

    NewFn(c, "ThreadPoolTimerCreate");
    NewFn(c, "ThreadPoolTimerSet");
    NewFn(c, "ThreadPoolTimerWait");
    NewFn(c, "ThreadPoolTimerClose");

    NewFn(c, "DirectIoOpen");
    NewFn(c, "DirectIoRead");
    NewFn(c, "DirectIoWrite");
    NewFn(c, "DirectIoClose");

    NewFn(c, "CryptoSha3Sha256");
    NewFn(c, "CryptoSha3Sha512");
    NewFn(c, "CryptoSha3Keccak256");

    NewFn(c, "SimdVec8fSum");
    NewFn(c, "SimdVec8fMinElement");
    NewFn(c, "SimdVec8fMaxElement");
    NewFn(c, "SimdVec8fNorm");

    NewFn(c, "ConsoleAnsiGetSize");
    NewFn(c, "ConsoleAnsiGetCursorPosition");
    NewFn(c, "ConsoleAnsiSetCursorPosition");
    NewFn(c, "ConsoleAnsiClear");
    NewFn(c, "ConsoleAnsiEnableVirtualTerminal");

    NewFn(c, "WaitOnAddressWait");
    NewFn(c, "WaitOnAddressWakeOne");
    NewFn(c, "WaitOnAddressWakeAll");

    NewFn(c, "CryptoBlake2Blake2b");
    NewFn(c, "CryptoBlake2Blake2s");

    NewFn(c, "ProcessAffinityGet");
    NewFn(c, "ProcessAffinitySet");
    NewFn(c, "ProcessAffinitySetCurrentThread");

    NewFn(c, "VirtualMemoryLockLock");
    NewFn(c, "VirtualMemoryLockUnlock");

    NewFn(c, "FastBmi2Pext");
    NewFn(c, "FastBmi2Pdep");
    NewFn(c, "FastBmi2Bzhi");

    NewFn(c, "HardLinkCreate");
    NewFn(c, "HardLinkGetCount");

    NewFn(c, "SparseFileSetSparse");
    NewFn(c, "SparseFileAllocateSparse");

    NewFn(c, "FileMemoryCommitFlush");

    NewFn(c, "CryptoPoly1305Auth");

    NewFn(c, "SystemThreadTelemetryGetTimes");
    NewFn(c, "SystemThreadTelemetryGetCycles");

    NewFn(c, "MemoryPressureWatcherQuery");

    NewFn(c, "SimdStringMatchFind");

    NewFn(c, "FileIoRingSubmitCopy");

    NewFn(c, "CryptoArgon2Hash");

    NewFn(c, "SimdVec4qNew");
    NewFn(c, "SimdVec4qAdd");
    NewFn(c, "SimdVec4qSub");
    NewFn(c, "SimdVec4qBitAnd");
    NewFn(c, "SimdVec4qBitOr");
    NewFn(c, "SimdVec4qBitXor");
    NewFn(c, "SimdVec4qGet");

    NewFn(c, "ProcessMemoryProtectProtect");

    NewFn(c, "HardwareCacheInfoGetHierarchy");

    NewFn(c, "FastBitPermuteBswap64");
    NewFn(c, "FastBitPermuteBswap32");
    NewFn(c, "FastBitPermuteBswap16");
    NewFn(c, "FastBitPermuteRbit64");

    NewFn(c, "SimdDotProductFmaDot8f");
    NewFn(c, "SimdDotProductFmaFma");

    NewFn(c, "SimdVec16u8Adds");
    NewFn(c, "SimdVec16u8Subs");
    NewFn(c, "SimdVec16u8Avg");

    NewFn(c, "SimdMatrix4x4Mul");
    NewFn(c, "SimdMatrix4x4Det");

    NewFn(c, "OsSharedMemoryMutexCreate");
    NewFn(c, "OsSharedMemoryMutexAcquire");
    NewFn(c, "OsSharedMemoryMutexRelease");
    NewFn(c, "OsSharedMemoryMutexClose");

    NewFn(c, "OsSystemTimerResolutionGetCaps");
    NewFn(c, "OsSystemTimerResolutionBegin");
    NewFn(c, "OsSystemTimerResolutionEnd");

    NewFn(c, "CryptoSipHash64Hash");
    NewFn(c, "CryptoSipHash128Hash");

    NewFn(c, "CryptoXChaCha20Poly1305EncryptC");
    NewFn(c, "CryptoXChaCha20Poly1305DecryptC");
    NewFn(c, "CryptoSha512_256HashC");
    NewFn(c, "CryptoHkdfExtractC");
    NewFn(c, "CryptoHkdfExpandC");
    NewFn(c, "CryptoAes128CmacAuthenticateC");
    NewFn(c, "BitSet256NewC");
    NewFn(c, "BitSet256SetC");
    NewFn(c, "BitSet256TestC");
    NewFn(c, "BitSet256CountOnesC");
    NewFn(c, "BitSet256BitAndC");
    NewFn(c, "BitSet256BitOrC");
    NewFn(c, "BitSet256BitXorC");
    NewFn(c, "FastBloomFilterCreateC");
    NewFn(c, "FastBloomFilterAddC");
    NewFn(c, "FastBloomFilterContainsC");

    NewFn(c, "RawDnsResolveC");
    NewFn(c, "WebSocketFrameEncodeC");
    NewFn(c, "WebSocketFrameDecodeC");
    NewFn(c, "IcmpPingEchoC");
    NewFn(c, "Http11ParseRequestC");
    NewFn(c, "Http11ParseResponseC");
    NewFn(c, "CircularByteBufferNewC");
    NewFn(c, "CircularByteBufferWriteC");
    NewFn(c, "CircularByteBufferReadC");
    NewFn(c, "RadixTreeNewC");
    NewFn(c, "RadixTreeInsertC");
    NewFn(c, "RadixTreeLookupC");
    NewFn(c, "RadixTreePrefixSearchC");
    NewFn(c, "HAMTGetC");
    NewFn(c, "HAMTInsertC");
    NewFn(c, "HAMTNewC");
    NewFn(c, "HazardPointerAcquireC");
    NewFn(c, "HazardPointerNewC");
    NewFn(c, "HazardPointerReleaseC");
    NewFn(c, "StealingDequeNewC");
    NewFn(c, "StealingDequePopC");
    NewFn(c, "StealingDequePushC");
    NewFn(c, "StealingDequeStealC");
    NewFn(c, "TarExtractListC");
    NewFn(c, "WaveletNewC");
    NewFn(c, "WaveletRankC");
    NewFn(c, "WinmmAudioPlayC");

    NewFn(c, "AtomicNew");
    NewFn(c, "Base32DecodeC");
    NewFn(c, "Base32EncodeC");
    NewFn(c, "Base85DecodeC");
    NewFn(c, "Base85EncodeC");
    NewFn(c, "BmpFileParseC");
    NewFn(c, "CSVParseC");
    NewFn(c, "Complex128AbsC");
    NewFn(c, "Complex128AddC");
    NewFn(c, "Complex128ArgC");
    NewFn(c, "Complex128ConjC");
    NewFn(c, "Complex128DivC");
    NewFn(c, "Complex128ExpC");
    NewFn(c, "Complex128LogC");
    NewFn(c, "Complex128MulC");
    NewFn(c, "Complex128NewC");
    NewFn(c, "Complex128PowC");
    NewFn(c, "Complex128SqrtC");
    NewFn(c, "Complex128SubC");
    NewFn(c, "CryptoAesCbcEncryptC");
    NewFn(c, "CryptoAesCbcDecryptC");
    NewFn(c, "CryptoCrc32C");
    NewFn(c, "CryptoPbkdf2C");
    NewFn(c, "CryptoTotpC");
    NewFn(c, "CryptoX25519C");
    NewFn(c, "CryptoXxHashC");
    NewFn(c, "CuckooFilterContainsC");
    NewFn(c, "CuckooFilterInsertC");
    NewFn(c, "CuckooFilterNewC");
    NewFn(c, "DSUFindC");
    NewFn(c, "DSUNewC");
    NewFn(c, "DSUSameC");
    NewFn(c, "DSUUnionC");
    NewFn(c, "EtwEmitC");
    NewFn(c, "FenwickTreeAddC");
    NewFn(c, "FenwickTreeNewC");
    NewFn(c, "FenwickTreeQueryC");
    NewFn(c, "GaussQuadC");
    NewFn(c, "GdiBitmapCreateC");
    NewFn(c, "GdiBitmapSaveC");
    NewFn(c, "GdiBitmapSetPixelC");
    NewFn(c, "IniParseC");
    NewFn(c, "JSONParseC");
    NewFn(c, "LagrangeInterpC");
    NewFn(c, "LinearSolveC");
    NewFn(c, "LockFreeStackNewC");
    NewFn(c, "LockFreeStackPopC");
    NewFn(c, "LockFreeStackPushC");
    NewFn(c, "MTNewC");
    NewFn(c, "MTNextFloatC");
    NewFn(c, "MTNextU64C");
    NewFn(c, "MillerRabinC");
    NewFn(c, "MillerRabinNextPrimeC");
    NewFn(c, "MinMaxHeapMaxC");
    NewFn(c, "MinMaxHeapMinC");
    NewFn(c, "MinMaxHeapNewC");
    NewFn(c, "MinMaxHeapPushC");
    NewFn(c, "MinMaxHeapSizeC");
    NewFn(c, "MpscQueueNewC");
    NewFn(c, "MpscQueuePopC");
    NewFn(c, "MpscQueuePushC");
    NewFn(c, "MsgPackEncodeC");
    NewFn(c, "NtfsAdsReadC");
    NewFn(c, "NtfsAdsWriteC");
    NewFn(c, "NtpQueryC");
    NewFn(c, "OsClipboardClearC");
    NewFn(c, "OsClipboardGetC");
    NewFn(c, "OsClipboardSetC");
    NewFn(c, "OsConsoleRawModeC");
    NewFn(c, "OsFiberCreateC");
    NewFn(c, "OsHeapWalkC");
    NewFn(c, "OsIocpCreateC");
    NewFn(c, "OsIocpGetC");
    NewFn(c, "OsIocpPostC");
    NewFn(c, "OsModuleEnumC");
    NewFn(c, "OsNamedEventCloseC");
    NewFn(c, "OsNamedEventCreateC");
    NewFn(c, "OsNamedEventResetC");
    NewFn(c, "OsNamedEventSetC");
    NewFn(c, "OsNamedEventWaitC");
    NewFn(c, "OsProcessSnapshotC");
    NewFn(c, "OsSymlinkCreateC");
    NewFn(c, "OsSymlinkReadC");
    NewFn(c, "OsTokenPrivilegesC");
    NewFn(c, "OsVirtualProtectC");
    NewFn(c, "OsVirtualQueryC");
    NewFn(c, "OsWindowEnumC");
    NewFn(c, "OsWow64QueryC");
    NewFn(c, "ParallelMap");
    NewFn(c, "PdbSymbolResolveC");
    NewFn(c, "PdhQueryC");
    NewFn(c, "RationalAddC");
    NewFn(c, "RationalDivC");
    NewFn(c, "RationalEqC");
    NewFn(c, "RationalMulC");
    NewFn(c, "RationalNewC");
    NewFn(c, "RationalSubC");
    NewFn(c, "RationalToFloatC");
    NewFn(c, "RegexMatchC");
    NewFn(c, "RegexReplaceC");
    NewFn(c, "RegexSplitC");
    NewFn(c, "RunningStatsAddC");
    NewFn(c, "RunningStatsMeanC");
    NewFn(c, "RunningStatsNewC");
    NewFn(c, "RunningStatsStdC");
    NewFn(c, "RunningStatsVarC");
    NewFn(c, "SegTreeNewC");
    NewFn(c, "SegTreeQueryC");
    NewFn(c, "SegTreeUpdateC");
    NewFn(c, "SimdBroadcastC");
    NewFn(c, "SimdConvertIntToFloatC");
    NewFn(c, "SimdCrc32cC");
    NewFn(c, "SimdDot");
    NewFn(c, "SimdSum");
    NewFn(c, "SkipListContainsC");
    NewFn(c, "SkipListInsertC");
    NewFn(c, "SkipListLenC");
    NewFn(c, "SkipListNewC");
    NewFn(c, "SmtpSendC");
    NewFn(c, "SocketAcceptSync");
    NewFn(c, "SocketConnectSync");
    NewFn(c, "SocketListenSync");
    NewFn(c, "SocketReceiveSync");
    NewFn(c, "SocketRecvSync");
    NewFn(c, "SocketSendSync");
    NewFn(c, "Socks5ConnectC");
    NewFn(c, "StackUnwinderC");
    NewFn(c, "StatKurtosisC");
    NewFn(c, "StatMaxC");
    NewFn(c, "StatMeanC");
    NewFn(c, "StatMinC");
    NewFn(c, "StatPercentileC");
    NewFn(c, "StatSkewnessC");
    NewFn(c, "StatStddevC");
    NewFn(c, "StatVarianceC");
    NewFn(c, "SuffixArrayBuildC");
    NewFn(c, "SuffixArraySearchC");
    NewFn(c, "TOMLParseC");
    NewFn(c, "TreapContainsC");
    NewFn(c, "TreapInsertC");
    NewFn(c, "TreapMaxC");
    NewFn(c, "TreapMinC");
    NewFn(c, "TreapNewC");
    NewFn(c, "UdpMulticastJoinC");
    NewFn(c, "WavFileParseC");
    NewFn(c, "WinHttpGetC");
    NewFn(c, "WinHttpPostC");
    NewFn(c, "ShmRingNewC");
    NewFn(c, "ShmRingPushC");
    NewFn(c, "ShmRingPopC");
    NewFn(c, "ShmRingAvailableC");
    NewFn(c, "ShmRingCloseC");
    NewFn(c, "ZipArchiveCreateC");
    NewFn(c, "ZipArchiveExtractC");
    NewFn(c, "ZipArchiveListC");

    return c;
}

void CompFree(Compiler *c) {
    for(int i=0;i<c->fncount;i++){
        free(c->fns[i]->chunk.code);
        free(c->fns[i]->chunk.lines);
        if (c->fns[i]->chunk.globalcache) free(c->fns[i]->chunk.globalcache);
        if (c->fns[i]->chunk.fieldcache) free(c->fns[i]->chunk.fieldcache);
        if (c->fns[i]->chunk.loopcounters) free(c->fns[i]->chunk.loopcounters);
        if (c->fns[i]->chunk.executed) free(c->fns[i]->chunk.executed);
        if (c->fns[i]->regcode) {
            RegInst *rinsts = (RegInst*)c->fns[i]->regcode;
            for (int r = 0; r < c->fns[i]->regcount; r++) {
                ValFree(&rinsts[r].constval);
                if (rinsts[r].fieldname) free(rinsts[r].fieldname);
                if (rinsts[r].cachedtypename) free(rinsts[r].cachedtypename);
            }
            free(c->fns[i]->regcode);
        }
        if (c->fns[i]->offsettomap) free(c->fns[i]->offsettomap);
        free(c->fns[i]);
    }
    for(int i=0;i<c->strcount;i++) free(c->strpool[i]);
    free(c->strpool);
    for (int i = 0; i < STRPOOL_BUCKETS; i++) {
        StrPoolEntry *e = c->str_hash[i];
        while (e) {
            StrPoolEntry *next = e->next;
            free(e);
            e = next;
        }
        c->str_hash[i] = NULL;
    }
    
    // free macros
    for (int i = 0; i < c->macrocount; i++) {
        NodeFree(c->macros[i].body);
        for (int p = 0; p < c->macros[i].pc; p++) {
            free(c->macros[i].pnames[p]);
        }
        free(c->macros[i].pnames);
    }
    // free templates
    for (int i = 0; i < c->templatecount; i++) {
        NodeFree(c->templates[i].ast);
    }
    
    // free structs
    for (int i = 0; i < c->structcount; i++) {
        for (int j = 0; j < c->structs[i].fc; j++) {
            free(c->structs[i].fnames[j]);
            free(c->structs[i].ftypes[j]);
        }
        free(c->structs[i].fnames);
        free(c->structs[i].ftypes);
        for (int j = 0; j < c->structs[i].ic; j++) {
            free(c->structs[i].interfaces[j]);
        }
        if (c->structs[i].ic > 0 && c->structs[i].interfaces) free(c->structs[i].interfaces);
    }
    
    // free interfaces
    for (int i = 0; i < c->interfacecount; i++) {
        for (int j = 0; j < c->interfaces[i].count; j++) {
            free(c->interfaces[i].fnames[j]);
            free(c->interfaces[i].rettypes[j]);
            for (int k = 0; k < c->interfaces[i].paramcounts[j]; k++) {
                free(c->interfaces[i].paramtypes[j][k]);
                free(c->interfaces[i].paramnames[j][k]);
            }
            if (c->interfaces[i].paramcounts[j] > 0) {
                free(c->interfaces[i].paramtypes[j]);
                free(c->interfaces[i].paramnames[j]);
            }
        }
        free(c->interfaces[i].fnames);
        free(c->interfaces[i].rettypes);
        free(c->interfaces[i].paramtypes);
        free(c->interfaces[i].paramnames);
        free(c->interfaces[i].paramcounts);
    }
    
    // free enums
    for (int i = 0; i < c->enumcount; i++) {
        for (int j = 0; j < c->enums[i].count; j++) {
            free(c->enums[i].members[j]);
        }
        free(c->enums[i].members);
        free(c->enums[i].values);
    }
    
    // free aliases
    for (int i = 0; i < c->aliascount; i++) {
        if (c->aliases[i].tpc > 0 && c->aliases[i].tparams) {
            for (int j = 0; j < c->aliases[i].tpc; j++) {
                free(c->aliases[i].tparams[j]);
            }
            free(c->aliases[i].tparams);
        }
    }
    
    // IMPORTANT: modules[i].ast pointers are child nodes of the AST trees
    // in imported_asts[]. They will be freed recursively when we free the
    // parent imported AST tree below. Null them out first to prevent
    // double-free (which was causing the exit-139 crash in ModuleHelper tests).
    for (int i = 0; i < c->module_count; i++) {
        c->modules[i].ast = NULL;
    }
    // Similarly, lazy_mods that have their AST registered in imported_asts[]
    // will have it freed below. Null them out to prevent double-free.
    for (int i = 0; i < c->lazy_mod_count; i++) {
        if (c->lazy_mods[i].ast) {
            // Check if this AST was also stored in imported_asts (it would be
            // if the lazy mod fell back to eager at the limit). If not found
            // in imported_asts, free it here (it's standalone).
            int found_in_imported = 0;
            for (int j = 0; j < c->imported_ast_count; j++) {
                if (c->imported_asts[j] == c->lazy_mods[i].ast) {
                    found_in_imported = 1;
                    break;
                }
            }
            if (!found_in_imported) {
                NodeFree(c->lazy_mods[i].ast);
            }
            c->lazy_mods[i].ast = NULL;
        }
    }
    // free imported ASTs (this recursively frees all child nodes including
    // the module body nodes that modules[].ast was pointing to)
    for (int i = 0; i < c->imported_ast_count; i++) {
        if (c->imported_asts[i]) {
            NodeFree(c->imported_asts[i]);
        }
    }

    StypeFreeAll(c);
    free(c);
}

static Module *CompilerFindModule(Compiler *c, const char *name) {
    if (!c || !name) return NULL;
    for (int i = 0; i < c->module_count; i++) {
        if (strcmp(c->modules[i].name, name) == 0) return &c->modules[i];
    }
    return NULL;
}

static Module *CompilerAddModule(Compiler *c, const char *name, const char *path, Node *ast) {
    if (!c || !name) return NULL;
    Module *m = CompilerFindModule(c, name);
    if (m) return m;
    if (c->module_count >= 64) return NULL;
    m = &c->modules[c->module_count++];
    memset(m, 0, sizeof(Module));
    strncpy(m->name, name, 255);
    if (path) strncpy(m->path, path, 511);
    m->ast = ast;
    m->is_loaded = 1;
    return m;
}

static void PreregisterFns(Compiler *c, Node *n) {
    if (!n) return;
    if (n->type == NPROGRAM || n->type == NBLOCK) {
        for (int i = 0; i < n->block.count; i++) {
            PreregisterFns(c, n->block.stmts[i]);
        }
    } else if (n->type == NMODULE) {
        CompilerAddModule(c, n->single.var, c->currentfilepath, n->single.expr);
        char saved_ns[512];
        strncpy(saved_ns, c->current_namespace, 511);
        saved_ns[511] = '\0';
        if (c->current_namespace[0] != '\0') {
            char new_ns[512];
            snprintf(new_ns, sizeof(new_ns), "%s::%s", c->current_namespace, n->single.var);
            strncpy(c->current_namespace, new_ns, 511);
        } else {
            strncpy(c->current_namespace, n->single.var, 511);
        }
        c->current_namespace[511] = '\0';
        if (n->single.expr) PreregisterFns(c, n->single.expr);
        strncpy(c->current_namespace, saved_ns, 511);
        c->current_namespace[511] = '\0';
    } else if (n->type == NFNDECL) {
        int found = -1;
        char *mangled = MangleName(c, n->fn.name);
        const char *fn_reg_name = mangled ? mangled : n->fn.name;
        for (int i = 0; i < c->fncount; i++) {
            if (strcmp(c->fns[i]->name, fn_reg_name) == 0) {
                found = i;
                break;
            }
        }
        FnProto *fn = NULL;
        if (found < 0) {
            fn = NewFn(c, fn_reg_name);
        } else {
            fn = c->fns[found];
        }
        if (mangled) free(mangled);
        if (fn) {
            fn->ast = n;
            fn->paramcount = n->fn.pc;
            fn->isasync = n->fn.isasync;
            if (n->fn.ret) strncpy(fn->rettype, n->fn.ret, 63);
            else strcpy(fn->rettype, "any");
            for (int i = 0; i < n->fn.pc; i++) {
                if (n->fn.ptypes[i]) strncpy(fn->paramtypes[i], n->fn.ptypes[i], 63);
                else strcpy(fn->paramtypes[i], "any");
            }
            char *dot = strchr(n->fn.name, '.');
            if (dot) {
                int structnamelen = (int)(dot - n->fn.name);
                char structname[128];
                if (structnamelen < 127) {
                    strncpy(structname, n->fn.name, structnamelen);
                    structname[structnamelen] = '\0';
                    int structidx = -1;
                    for (int i = 0; i < c->structcount; i++) {
                        if (strcmp(c->structs[i].name, structname) == 0) {
                            structidx = i;
                            break;
                        }
                    }
                    if (structidx >= 0) {
                        CompilerStruct *s = &c->structs[structidx];
                        char *methodname = dot + 1;
                        int exists = 0;
                        for (int j = 0; j < s->fc; j++) {
                            if (strcmp(s->fnames[j], methodname) == 0) {
                                exists = 1;
                                break;
                            }
                        }
                        if (!exists) {
                            s->fnames = realloc(s->fnames, (s->fc + 1) * sizeof(char*));
                            s->ftypes = realloc(s->ftypes, (s->fc + 1) * sizeof(char*));
                            s->fnames[s->fc] = strdup(methodname);
                            char sig[512] = "fn(";
                            for (int p = 0; p < n->fn.pc; p++) {
                                if (n->fn.ptypes && n->fn.ptypes[p]) {
                                    strcat(sig, n->fn.ptypes[p]);
                                } else {
                                    strcat(sig, "any");
                                }
                                if (p + 1 < n->fn.pc) strcat(sig, ",");
                            }
                            strcat(sig, ")->");
                            if (n->fn.ret) {
                                strcat(sig, n->fn.ret);
                            } else {
                                strcat(sig, "any");
                            }
                            s->ftypes[s->fc] = strdup(sig);
                            s->fc++;
                        }
                    }
                }
            }
        }
    } else if (n->type == NIMPORT) {
        int islazy = n->decl.isconstflag;  /* set by 'lazy import' parser */
        const char *path = n->single.var;
        char *resolved = PkgResolveImport(path, c->currentfilepath);
        char finalpath_buf[512];
        strncpy(finalpath_buf, resolved ? resolved : path, 511);
        finalpath_buf[511] = '\0';
        if (resolved) { free(resolved); resolved = NULL; }
        const char *finalpath = finalpath_buf;
        char *src = CompReadFile(finalpath);
        if (src) {
            int parseerr = 0;
            Node *importedast = Parse(src, &parseerr);
            free(src);
            if (!parseerr && importedast) {
                char savedpath[512];
                strncpy(savedpath, c->currentfilepath, sizeof(savedpath) - 1);
                savedpath[sizeof(savedpath) - 1] = '\0';
                strncpy(c->currentfilepath, finalpath, sizeof(c->currentfilepath) - 1);
                c->currentfilepath[sizeof(c->currentfilepath) - 1] = '\0';
                PreregisterFns(c, importedast);
                strncpy(c->currentfilepath, savedpath, sizeof(c->currentfilepath) - 1);
                c->currentfilepath[sizeof(c->currentfilepath) - 1] = '\0';
                if (islazy) {
                    /* Lazy: store AST for on-demand compilation later */
                    if (c->lazy_mod_count < 64) {
                        int li = c->lazy_mod_count++;
                        strncpy(c->lazy_mods[li].path, finalpath, 511);
                        c->lazy_mods[li].path[511] = '\0';
                        strncpy(c->lazy_mods[li].savedpath, savedpath, 511);
                        c->lazy_mods[li].savedpath[511] = '\0';
                        c->lazy_mods[li].ast = importedast;
                        c->lazy_mods[li].loaded = 0;
                        /* Mark all stubs from this file as lazy */
                        for (int fi = 0; fi < c->fncount; fi++) {
                            if (strcmp(c->fns[fi]->file_name, finalpath) == 0) {
                                c->fns[fi]->is_lazy_stub = 1;
                            }
                        }
                    } else {
                        /* Too many lazy mods: fall back to eager */
                        if (c->imported_ast_count < 1024)
                            c->imported_asts[c->imported_ast_count++] = importedast;
                        else NodeFree(importedast);
                    }
                } else {
                    /* Eager: keep AST alive */
                    if (c->imported_ast_count < 1024) {
                        c->imported_asts[c->imported_ast_count++] = importedast;
                    } else {
                        NodeFree(importedast);
                    }
                }
            }
        }
    }
}

static int GetInstructionSize(uint8_t *code, int count, int ip) {
    if (ip >= count) return 0;
    uint8_t op = code[ip];
    switch (op) {
        case OPHALT:
        case OPPUSHNULL:
        case OPDUP:
        case OPPOP:
        case OPPRINTLN:
        case OPADD:
        case OPSUB:
        case OPMUL:
        case OPDIV:
        case OPMOD:
        case OPEQ:
        case OPNEQ:
        case OPLT:
        case OPGT:
        case OPLTE:
        case OPGTE:
        case OPAND:
        case OPOR:
        case OPNOT:
        case OPNEG:
        case OPRETURN:
        case OPRETURNVAL:
        case OPARRGET:
        case OPARRSET:
        case OPINC:
        case OPDEC:
        case OPTRYPOP:
        case OPTHROW:
        case OPRANGE:
        case OPPRINTREPL:
        case OPTYPEOF:
            return 1;

        case OPPUSHBOOL:
            return 2;

        case OPPUSHSTR:
        case OPLOAD:
        case OPLOADGLOBAL:
        case OPSTORE:
        case OPSTOREGLOBAL:
        case OPINPUTSTR:
        case OPNEWARR:
        case OPNEWSTRUCT:
        case OPGETFIELD:
        case OPSETFIELD:
        case OPPUSHFN:
            return 3;

        case OPCALL:
        case OPTAILCALL:
            return 4;

        case OPJUMP:
        case OPJUMPIFFALSE:
        case OPJUMPIFNULL:
        case OPTRYPUSH:
        case OPCOALESCE:
            return 5;

        case OPPUSHINT:
        case OPPUSHFLOAT:
            return 9;

        case OPCALLDYNAMIC:
            return 2;

        case OPSPAWN:
            return 4;

        default:
            return 1;
    }
}

static void AdjustJumps(Chunk *ch, int start, int deletecount) {
    int i = 0;
    while (i < ch->count) {
        uint8_t op = ch->code[i];
        int size = GetInstructionSize(ch->code, ch->count, i);
        if (op == OPJUMP || op == OPJUMPIFFALSE || op == OPJUMPIFNULL || 
            op == OPTRYPUSH || op == OPCOALESCE) {
            int32_t offset = (int32_t)((ch->code[i+1] << 24) | (ch->code[i+2] << 16) | (ch->code[i+3] << 8) | ch->code[i+4]);
            int target = i + 5 + offset;
            
            if (i < start && target >= start) {
                offset -= deletecount;
            } else if (i >= start && target < start) {
                offset += deletecount;
            }
            
            ch->code[i+1] = (uint8_t)((offset >> 24) & 0xFF);
            ch->code[i+2] = (uint8_t)((offset >> 16) & 0xFF);
            ch->code[i+3] = (uint8_t)((offset >> 8) & 0xFF);
            ch->code[i+4] = (uint8_t)(offset & 0xFF);
        }
        i += size;
    }
}

static void CompActChunk(Chunk *ch, int start, int deletecount) {
    memmove(ch->code + start, ch->code + start + deletecount, ch->count - start - deletecount);
    if (ch->lines) {
        memmove(ch->lines + start, ch->lines + start + deletecount, (ch->count - start - deletecount) * sizeof(int));
    }
    ch->count -= deletecount;
    AdjustJumps(ch, start, deletecount);
}

static void OptBytecode(Chunk *ch) {
    int i = 0;

    while (i < ch->count) {
        int size = GetInstructionSize(ch->code, ch->count, i);
        
        if (ch->code[i] == OPPUSHINT && i + 9 < ch->count && ch->code[i+9] == OPADD) {
            int64_t val = 0;
            for (int k = 1; k <= 8; k++) {
                val = (val << 8) | ch->code[i+k];
            }
            if (val == 1) {
                ch->code[i] = OPINC;
                CompActChunk(ch, i + 1, 9);
                continue;
            }
        }
        
        if (ch->code[i] == OPPUSHINT && i + 9 < ch->count && ch->code[i+9] == OPSUB) {
            int64_t val = 0;
            for (int k = 1; k <= 8; k++) {
                val = (val << 8) | ch->code[i+k];
            }
            if (val == 1) {
                ch->code[i] = OPDEC;
                CompActChunk(ch, i + 1, 9);
                continue;
            }
        }

        i += size;
    }
}

static Node *SpecializeCall(Compiler *c, Node *program, Node *callnode) {
    if (callnode->call.callee->type != NIDENT) return callnode;
    const char *name = callnode->call.callee->sval;
    Node *fndecl = NULL;
    if (program->type == NPROGRAM || program->type == NBLOCK) {
        for (int i = 0; i < program->block.count; i++) {
            Node *stmt = program->block.stmts[i];
            if (stmt && stmt->type == NFNDECL && strcmp(stmt->fn.name, name) == 0) {
                fndecl = stmt;
                break;
            }
        }
    }
    if (!fndecl) return callnode;
    if (fndecl->fn.isvariadic || fndecl->fn.req_count > 0 || fndecl->fn.ens_count > 0) return callnode;
    int hasconstant = 0;
    for (int i = 0; i < callnode->call.argc; i++) {
        NType t = callnode->call.args[i]->type;
        if (t == NINT || t == NFLOAT || t == NBOOL || t == NSTR || t == NNULL) {
            hasconstant = 1;
            break;
        }
    }
    if (!hasconstant) return callnode;
    char mangled[512];
    int len = snprintf(mangled, sizeof(mangled), "%sspec", name);
    for (int i = 0; i < callnode->call.argc; i++) {
        Node *arg = callnode->call.args[i];
        if (arg->type == NINT) {
            len += snprintf(mangled + len, sizeof(mangled) - len, "int%lld", (long long)arg->ival);
        } else if (arg->type == NFLOAT) {
            char fltbuf[64];
            snprintf(fltbuf, sizeof(fltbuf), "%g", arg->fval);
            char *p = fltbuf;
            while (*p) {
                if (*p == '.') *p = 'p';
                if (*p == '-') *p = 'n';
                p++;
            }
            len += snprintf(mangled + len, sizeof(mangled) - len, "flt%s", fltbuf);
        } else if (arg->type == NBOOL) {
            len += snprintf(mangled + len, sizeof(mangled) - len, "bool%s", arg->ival ? "t" : "f");
        } else if (arg->type == NSTR) {
            char safe[128];
            char *d = safe;
            const char *s = arg->sval;
            unsigned long hash = 5381;
            while (*s) {
                hash = ((hash << 5) + hash) + (unsigned char)*s;
                if ((d - safe) < 10 && isalnum((unsigned char)*s)) {
                    *d++ = *s;
                }
                s++;
            }
            *d = '\0';
            len += snprintf(mangled + len, sizeof(mangled) - len, "str%s%lx", safe, hash);
        } else if (arg->type == NNULL) {
            len += snprintf(mangled + len, sizeof(mangled) - len, "null");
        } else {
            len += snprintf(mangled + len, sizeof(mangled) - len, "any");
        }
    }

    Node *specfn = NULL;
    if (program->type == NPROGRAM || program->type == NBLOCK) {
        for (int i = 0; i < program->block.count; i++) {
            Node *stmt = program->block.stmts[i];
            if (stmt && stmt->type == NFNDECL && strcmp(stmt->fn.name, mangled) == 0) {
                specfn = stmt;
                break;
            }
        }
    }
    if (!specfn) {
        Node *NewFn = NodeCopy(fndecl);
        free(NewFn->fn.name);
        NewFn->fn.name = strdup(mangled);
        int newpc = 0;
        for (int i = 0; i < fndecl->fn.pc; i++) {
            if (i < callnode->call.argc) {
                Node *arg = callnode->call.args[i];
                NType t = arg->type;
                if (t != NINT && t != NFLOAT && t != NBOOL && t != NSTR && t != NNULL) {
                    newpc++;
                }
            } else {
                newpc++;
            }
        }
        char **newpnames = newpc > 0 ? malloc(newpc * sizeof(char*)) : NULL;
        char **newptypes = newpc > 0 ? malloc(newpc * sizeof(char*)) : NULL;
        Node **newpdefaults = newpc > 0 ? malloc(newpc * sizeof(Node*)) : NULL;
        int constcount = fndecl->fn.pc - newpc;
        Node **conststmts = malloc(constcount * sizeof(Node*));
        int constidx = 0;
        int pidx = 0;
        for (int i = 0; i < fndecl->fn.pc; i++) {
            Node *arg = (i < callnode->call.argc) ? callnode->call.args[i] : NULL;
            if (arg && (arg->type == NINT || arg->type == NFLOAT || arg->type == NBOOL || arg->type == NSTR || arg->type == NNULL)) {
                Node *decl = NodeNew(NCONSTDECL, fndecl->line);
                decl->decl.name = strdup(fndecl->fn.pnames[i]);
                decl->decl.tname = strdup("");
                decl->decl.init = NodeCopy(arg);
                decl->decl.isconstflag = 1;
                conststmts[constidx++] = decl;
            } else {
                if (i < fndecl->fn.pc) {
                    newpnames[pidx] = strdup(fndecl->fn.pnames[i]);
                    newptypes[pidx] = strdup(fndecl->fn.ptypes[i]);
                    newpdefaults[pidx] = (fndecl->fn.pdefaults && fndecl->fn.pdefaults[i]) ? NodeCopy(fndecl->fn.pdefaults[i]) : NULL;
                    pidx++;
                }
            }
        }
        for (int i = 0; i < fndecl->fn.pc; i++) {
            free(NewFn->fn.pnames[i]);
            free(NewFn->fn.ptypes[i]);
            if (NewFn->fn.pdefaults && NewFn->fn.pdefaults[i]) NodeFree(NewFn->fn.pdefaults[i]);
        }
        free(NewFn->fn.pnames);
        free(NewFn->fn.ptypes);
        free(NewFn->fn.pdefaults);
        NewFn->fn.pnames = newpnames;
        NewFn->fn.ptypes = newptypes;
        NewFn->fn.pdefaults = newpdefaults;
        NewFn->fn.pc = newpc;
        Node *oldbody = NewFn->fn.body;
        Node *newbody = NodeNew(NBLOCK, oldbody->line);
        newbody->block.count = constcount + oldbody->block.count;
        newbody->block.stmts = malloc(newbody->block.count * sizeof(Node*));
        for (int i = 0; i < constcount; i++) {
            newbody->block.stmts[i] = conststmts[i];
        }
        for (int i = 0; i < oldbody->block.count; i++) {
            newbody->block.stmts[constcount + i] = NodeCopy(oldbody->block.stmts[i]);
        }
        NodeFree(oldbody);
        NewFn->fn.body = newbody;
        free(conststmts);
        int oldcount = program->block.count;
        program->block.stmts = realloc(program->block.stmts, (oldcount + 1) * sizeof(Node*));
        program->block.stmts[oldcount] = NewFn;
        program->block.count = oldcount + 1;
        specfn = NewFn;
    }
    free(callnode->call.callee->sval);
    callnode->call.callee->sval = strdup(mangled);
    int newargc = 0;
    for (int i = 0; i < callnode->call.argc; i++) {
        Node *arg = callnode->call.args[i];
        NType t = arg->type;
        if (t != NINT && t != NFLOAT && t != NBOOL && t != NSTR && t != NNULL) {
            newargc++;
        }
    }
    Node **newargs = newargc > 0 ? malloc(newargc * sizeof(Node*)) : NULL;
    int aidx = 0;
    for (int i = 0; i < callnode->call.argc; i++) {
        Node *arg = callnode->call.args[i];
        NType t = arg->type;
        if (t != NINT && t != NFLOAT && t != NBOOL && t != NSTR && t != NNULL) {
            newargs[aidx++] = arg;
        } else {
            NodeFree(arg);
        }
    }
    free(callnode->call.args);
    callnode->call.args = newargs;
    callnode->call.argc = newargc;
    return callnode;
}

static void ASTSpecialize(Compiler *c, Node *program, Node *n) {
    if (!n) return;
    switch (n->type) {
        case NPROGRAM:
        case NBLOCK:
            for (int i = 0; i < n->block.count; i++) {
                ASTSpecialize(c, program, n->block.stmts[i]);
            }
            break;
        case NFNDECL:
            ASTSpecialize(c, program, n->fn.body);
            break;
        case NVARDECL:
        case NCONSTDECL:
            ASTSpecialize(c, program, n->decl.init);
            break;
        case NIF:
            ASTSpecialize(c, program, n->ifn.cond);
            ASTSpecialize(c, program, n->ifn.then);
            if (n->ifn.els) ASTSpecialize(c, program, n->ifn.els);
            break;
        case NTERNARY:
            ASTSpecialize(c, program, n->ternary.cond);
            ASTSpecialize(c, program, n->ternary.tval);
            ASTSpecialize(c, program, n->ternary.fval);
            break;
        case NWHILE:
            ASTSpecialize(c, program, n->whilen.cond);
            ASTSpecialize(c, program, n->whilen.body);
            break;
        case NFOR:
            if (n->forn.init) ASTSpecialize(c, program, n->forn.init);
            if (n->forn.cond) ASTSpecialize(c, program, n->forn.cond);
            if (n->forn.post) ASTSpecialize(c, program, n->forn.post);
            ASTSpecialize(c, program, n->forn.body);
            break;
        case NFORIN:
            ASTSpecialize(c, program, n->forin.iter);
            ASTSpecialize(c, program, n->forin.body);
            break;
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
        case NTHROW:
        case NYIELD:
        case NAWAIT:
        case NSPAWN:
            if (n->single.expr) ASTSpecialize(c, program, n->single.expr);
            break;
        case NASSIGN:
            ASTSpecialize(c, program, n->assign.target);
            ASTSpecialize(c, program, n->assign.value);
            break;
        case NCALL:
            ASTSpecialize(c, program, n->call.callee);
            for (int i = 0; i < n->call.argc; i++) {
                ASTSpecialize(c, program, n->call.args[i]);
            }
            SpecializeCall(c, program, n);
            break;
        case NINDEX:
            ASTSpecialize(c, program, n->index.arr);
            ASTSpecialize(c, program, n->index.idx);
            break;
        case NMEMBER:
            ASTSpecialize(c, program, n->member.obj);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                ASTSpecialize(c, program, n->arrlit.elems[i]);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++) {
                ASTSpecialize(c, program, n->maplit.vals[i]);
            }
            break;
        case NBINOP:
            ASTSpecialize(c, program, n->binop.left);
            ASTSpecialize(c, program, n->binop.right);
            break;
        case NUNOP:
            ASTSpecialize(c, program, n->binop.left);
            break;
        case NINC:
        case NDEC:
            ASTSpecialize(c, program, n->incdec.expr);
            break;
        case NTRYCATCH:
            ASTSpecialize(c, program, n->trycatch.tryblock);
            if (n->trycatch.catchblock) ASTSpecialize(c, program, n->trycatch.catchblock);
            if (n->trycatch.finallyblock) ASTSpecialize(c, program, n->trycatch.finallyblock);
            break;
        case NCOALESCE:
            ASTSpecialize(c, program, n->coalesce.left);
            ASTSpecialize(c, program, n->coalesce.right);
            break;
        case NOPTCHAIN:
            if (n->optchain.obj) ASTSpecialize(c, program, n->optchain.obj);
            if (n->optchain.idx) ASTSpecialize(c, program, n->optchain.idx);
            break;
        case NRANGE:
            ASTSpecialize(c, program, n->range.start);
            ASTSpecialize(c, program, n->range.end);
            break;
        case NSPREAD:
            ASTSpecialize(c, program, n->spread.expr);
            break;
        case NSWITCH:
            ASTSpecialize(c, program, n->sw.expr);
            for (int i = 0; i < n->sw.casecount; i++) {
                if (n->sw.casevals[i]) ASTSpecialize(c, program, n->sw.casevals[i]);
                ASTSpecialize(c, program, n->sw.casebodies[i]);
            }
            if (n->sw.defaultbody) ASTSpecialize(c, program, n->sw.defaultbody);
            break;
        default:
            break;
    }
}

static int FindLastUse(Node *n, const char *varname) {
    if (!n) return 0;
    int maxline = 0;
    if (n->type == NIDENT && strcmp(n->sval, varname) == 0) {
        maxline = n->line;
    }
    switch (n->type) {
        case NBINOP: {
            int l = FindLastUse(n->binop.left, varname);
            int r = FindLastUse(n->binop.right, varname);
            if (l > maxline) maxline = l;
            if (r > maxline) maxline = r;
            break;
        }
        case NUNOP: {
            int l = FindLastUse(n->binop.left, varname);
            if (l > maxline) maxline = l;
            break;
        }
        case NVARDECL:
        case NCONSTDECL: {
            if (n->decl.init) {
                int l = FindLastUse(n->decl.init, varname);
                if (l > maxline) maxline = l;
            }
            break;
        }
        case NFNDECL: {
            int l = FindLastUse(n->fn.body, varname);
            if (l > maxline) maxline = l;
            break;
        }
        case NBLOCK:
        case NPROGRAM: {
            for (int i = 0; i < n->block.count; i++) {
                int l = FindLastUse(n->block.stmts[i], varname);
                if (l > maxline) maxline = l;
            }
            break;
        }
        case NIF: {
            int c = FindLastUse(n->ifn.cond, varname);
            int t = FindLastUse(n->ifn.then, varname);
            int e = FindLastUse(n->ifn.els, varname);
            if (c > maxline) maxline = c;
            if (t > maxline) maxline = t;
            if (e > maxline) maxline = e;
            break;
        }
        case NTERNARY: {
            int c = FindLastUse(n->ternary.cond, varname);
            int t = FindLastUse(n->ternary.tval, varname);
            int e = FindLastUse(n->ternary.fval, varname);
            if (c > maxline) maxline = c;
            if (t > maxline) maxline = t;
            if (e > maxline) maxline = e;
            break;
        }
        case NWHILE: {
            int c = FindLastUse(n->whilen.cond, varname);
            int b = FindLastUse(n->whilen.body, varname);
            if (c > maxline) maxline = c;
            if (b > maxline) maxline = b;
            break;
        }
        case NFOR: {
            int i = FindLastUse(n->forn.init, varname);
            int c = FindLastUse(n->forn.cond, varname);
            int p = FindLastUse(n->forn.post, varname);
            int b = FindLastUse(n->forn.body, varname);
            if (i > maxline) maxline = i;
            if (c > maxline) maxline = c;
            if (p > maxline) maxline = p;
            if (b > maxline) maxline = b;
            break;
        }
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
        case NTHROW: {
            if (n->single.expr) {
                int l = FindLastUse(n->single.expr, varname);
                if (l > maxline) maxline = l;
            }
            break;
        }
        case NASSIGN: {
            int t = FindLastUse(n->assign.target, varname);
            int v = FindLastUse(n->assign.value, varname);
            if (t > maxline) maxline = t;
            if (v > maxline) maxline = v;
            break;
        }
        case NCALL: {
            int c = FindLastUse(n->call.callee, varname);
            if (c > maxline) maxline = c;
            for (int i = 0; i < n->call.argc; i++) {
                int a = FindLastUse(n->call.args[i], varname);
                if (a > maxline) maxline = a;
            }
            break;
        }
        case NINDEX: {
            int a = FindLastUse(n->index.arr, varname);
            int idx = FindLastUse(n->index.idx, varname);
            if (a > maxline) maxline = a;
            if (idx > maxline) maxline = idx;
            break;
        }
        case NMEMBER: {
            int o = FindLastUse(n->member.obj, varname);
            if (o > maxline) maxline = o;
            break;
        }
        case NARRLIT: {
            for (int i = 0; i < n->arrlit.count; i++) {
                int e = FindLastUse(n->arrlit.elems[i], varname);
                if (e > maxline) maxline = e;
            }
            break;
        }
        case NMAPLIT: {
            for (int i = 0; i < n->maplit.count; i++) {
                int v = FindLastUse(n->maplit.vals[i], varname);
                if (v > maxline) maxline = v;
            }
            break;
        }
        default: break;
    }
    return maxline;
}

static BorrowTracker g_borrows[256];
static int g_borrow_count = 0;

TypeInfo InferReferenceType(const char *type_name) {
    TypeInfo ti;
    memset(&ti, 0, sizeof(ti));
    if (type_name) strncpy(ti.name, type_name, 127);
    if (type_name && type_name[0] == '&') {
        ti.is_reference = 1;
        if (strncmp(type_name, "&mut", 4) == 0) {
            ti.is_mutable_ref = 1;
            strncpy(ti.element_type, type_name + 4, 127);
        } else {
            ti.is_mutable_ref = 0;
            strncpy(ti.element_type, type_name + 1, 127);
        }
    }
    return ti;
}

int CanBorrowMutable(Compiler *c, int local_id, int depth) {
    if (local_id < 0 || local_id >= c->current->localcount) return 0;
    const char *varname = c->current->locals[local_id].name;
    for (int i = 0; i < g_borrow_count; i++) {
        if (strcmp(g_borrows[i].target, varname) == 0) {
            return 0;
        }
    }
    return 1;
}

int CanBorrowImmutable(Compiler *c, int local_id, int depth) {
    if (local_id < 0 || local_id >= c->current->localcount) return 0;
    const char *varname = c->current->locals[local_id].name;
    for (int i = 0; i < g_borrow_count; i++) {
        if (strcmp(g_borrows[i].target, varname) == 0 && g_borrows[i].is_mutable) {
            return 0;
        }
    }
    return 1;
}

void RecordBorrow(Compiler *c, const char *borrower, const char *target, int depth, int is_mut, int line) {
    if (g_borrow_count >= 256) return;
    strncpy(g_borrows[g_borrow_count].borrower, borrower ? borrower : "", 127);
    strncpy(g_borrows[g_borrow_count].target, target ? target : "", 127);
    g_borrows[g_borrow_count].scope_depth = depth;
    g_borrows[g_borrow_count].is_mutable = is_mut;
    g_borrows[g_borrow_count].line = line;
    g_borrow_count++;
}

void ExitScopeBorrows(Compiler *c, int depth) {
    int write = 0;
    for (int read = 0; read < g_borrow_count; read++) {
        if (g_borrows[read].scope_depth < depth) {
            g_borrows[write++] = g_borrows[read];
        }
    }
    g_borrow_count = write;
}

static void CheckConflicts(Compiler *c, Node *n, const char *borrowedvar, int startline, int endline, int ismutborrow, const char *borrowvar) {
    if (!n) return;
    if (n->line >= startline && n->line <= endline) {
        if (n->type == NASSIGN) {
            Node *target = n->assign.target;
            while (target->type == NMEMBER) target = target->member.obj;
            while (target->type == NINDEX) target = target->index.arr;
            if (target->type == NIDENT && strcmp(target->sval, borrowedvar) == 0) {
                char buf[512];
                snprintf(buf, sizeof(buf),
                    "error[E0596]: Cannot mutate `%s` while it is immutably borrowed\n"
                    "  --> line %d\n"
                    "   |\n"
                    " %d | let %s = &%s;\n"
                    "   | -- immutable borrow occurs here\n"
                    " %d | %s = ...;\n"
                    "   | ^^^^^^ assignment to borrowed variable occurs here\n"
                    "   |\n"
                    "note: Consider releasing borrow of `%s` before mutating it.",
                    borrowedvar, n->line, startline, borrowvar, borrowedvar, n->line, borrowedvar, borrowedvar);
                Error(c, n->line, buf);
            }
        }
        if (n->type == NVARDECL || n->type == NCONSTDECL) {
            Node *init = n->decl.init;
            if (init && (init->type == NREF || init->type == NMUTREF || (init->type == NUNOP && (!strcmp(init->binop.op, "&") || !strcmp(init->binop.op, "&mut"))))) {
                Node *operand = (init->type == NREF || init->type == NMUTREF) ? init->refnode.expr : init->binop.left;
                while (operand && operand->type == NMEMBER) operand = operand->member.obj;
                while (operand && operand->type == NINDEX) operand = operand->index.arr;
                if (operand && operand->type == NIDENT && strcmp(operand->sval, borrowedvar) == 0) {
                    int otherismut = (init->type == NMUTREF) || (init->type == NUNOP && !strcmp(init->binop.op, "&mut"));
                    if ((ismutborrow || otherismut) && strcmp(n->decl.name, borrowvar) != 0) {
                        char buf[1024];
                        const char *code_code = otherismut ? "E0499" : "E0502";
                        snprintf(buf, sizeof(buf),
                            "error[%s]: Cannot borrow `%s` mutably while it's immutably borrowed\n"
                            "  --> line %d\n"
                            "   |\n"
                            " %d | let %s = %s%s;\n"
                            "   | -- %s borrow occurs here\n"
                            " %d | let %s = %s%s;\n"
                            "   | ^^^^^^ %s borrow occurs here\n"
                            "   |\n"
                            "note: Consider one of these solutions:\n"
                            " 1. Stop using %s before line %d: %s goes out of scope\n"
                            " 2. Change to immutable borrow: &%s instead of &mut %s\n"
                            " 3. Restructure code to use borrows in separate scopes\n"
                            "suggestion: Move the print statement inside a scope block",
                            code_code, borrowedvar, n->line,
                            startline, borrowvar, ismutborrow ? "&mut " : "&", borrowedvar,
                            ismutborrow ? "mutable" : "immutable",
                            n->line, n->decl.name, otherismut ? "&mut " : "&", borrowedvar,
                            otherismut ? "mutable" : "immutable",
                            borrowvar, n->line, borrowvar,
                            borrowedvar, borrowedvar);
                        Error(c, n->line, buf);
                    }
                }
            }
        }
    }
    switch (n->type) {
        case NBINOP:
            CheckConflicts(c, n->binop.left, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->binop.right, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NUNOP:
            CheckConflicts(c, n->binop.left, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NVARDECL:
        case NCONSTDECL:
            if (n->decl.init) CheckConflicts(c, n->decl.init, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NFNDECL:
            CheckConflicts(c, n->fn.body, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) {
                CheckConflicts(c, n->block.stmts[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
            }
            break;
        case NIF:
            CheckConflicts(c, n->ifn.cond, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->ifn.then, borrowedvar, startline, endline, ismutborrow, borrowvar);
            if (n->ifn.els) CheckConflicts(c, n->ifn.els, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NTERNARY:
            CheckConflicts(c, n->ternary.cond, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->ternary.tval, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->ternary.fval, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NWHILE:
            CheckConflicts(c, n->whilen.cond, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->whilen.body, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NFOR:
            if (n->forn.init) CheckConflicts(c, n->forn.init, borrowedvar, startline, endline, ismutborrow, borrowvar);
            if (n->forn.cond) CheckConflicts(c, n->forn.cond, borrowedvar, startline, endline, ismutborrow, borrowvar);
            if (n->forn.post) CheckConflicts(c, n->forn.post, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->forn.body, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
        case NTHROW:
            if (n->single.expr) CheckConflicts(c, n->single.expr, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NASSIGN:
            CheckConflicts(c, n->assign.target, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->assign.value, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NCALL:
            CheckConflicts(c, n->call.callee, borrowedvar, startline, endline, ismutborrow, borrowvar);
            for (int i = 0; i < n->call.argc; i++) {
                CheckConflicts(c, n->call.args[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
            }
            break;
        case NINDEX:
            CheckConflicts(c, n->index.arr, borrowedvar, startline, endline, ismutborrow, borrowvar);
            CheckConflicts(c, n->index.idx, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NMEMBER:
            CheckConflicts(c, n->member.obj, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                CheckConflicts(c, n->arrlit.elems[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++) {
                CheckConflicts(c, n->maplit.vals[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
            }
            break;
        case NMATCH:
            CheckConflicts(c, n->match.expr, borrowedvar, startline, endline, ismutborrow, borrowvar);
            for (int i = 0; i < n->match.count; i++) {
                CheckConflicts(c, n->match.patterns[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
                CheckConflicts(c, n->match.bodies[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
                if (n->match.guards && n->match.guards[i]) CheckConflicts(c, n->match.guards[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
            }
            break;
        case NAWAIT:
            CheckConflicts(c, n->awaitn.expr, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NSPAWN:
            CheckConflicts(c, n->spawnn.expr, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NYIELD:
            CheckConflicts(c, n->yieldstmt.expr, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NTRYPROPAGATE:
            CheckConflicts(c, n->single.expr, borrowedvar, startline, endline, ismutborrow, borrowvar);
            break;
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) {
                CheckConflicts(c, n->tuple.elems[i], borrowedvar, startline, endline, ismutborrow, borrowvar);
            }
            break;

        default: break;
    }
}

static const char *GetDeclaredType(Node *n, const char *varname) {
    if (!n) return NULL;
    if ((n->type == NVARDECL || n->type == NCONSTDECL) && strcmp(n->decl.name, varname) == 0) {
        if (n->decl.tname && strlen(n->decl.tname) > 0) return n->decl.tname;
        if (n->decl.init) {
            if (n->decl.init->type == NINT) return "int";
            if (n->decl.init->type == NFLOAT) return "float";
            if (n->decl.init->type == NBOOL) return "bool";
            if (n->decl.init->type == NSTR) return "string";
            if (n->decl.init->type == NNEW && n->decl.init->newobj.name) return n->decl.init->newobj.name;
            if (n->decl.init->type == NSTRUCTINIT && n->decl.init->structinit.typename) return n->decl.init->structinit.typename;
        }
    }
    if (n->type == NBLOCK || n->type == NPROGRAM) {
        for (int i = 0; i < n->block.count; i++) {
            const char *res = GetDeclaredType(n->block.stmts[i], varname);
            if (res) return res;
        }
    }
    return NULL;
}

static int IsCopyTypeName(const char *typename) {
    if (!typename) return 0;
    if (!strcmp(typename, "int") || !strcmp(typename, "float") || !strcmp(typename, "bool")) return 1;
    return 0;
}

static int IsStructType(Compiler *c, const char *typename) {
    if (!typename) return 0;
    for (int i = 0; i < c->structcount; i++) {
        if (strcmp(c->structs[i].name, typename) == 0) return 1;
    }
    return 0;
}

static int IsPinNedType(Compiler *c, const char *typename) {
    if (!typename) return 0;
    if (strstr(typename, "Pin") != NULL) return 1;
    for (int i = 0; i < c->structcount; i++) {
        if (strcmp(c->structs[i].name, typename) == 0) {
            CompilerStruct *s = &c->structs[i];
            for (int j = 0; j < s->fc; j++) {
                if (s->ftypes[j] && strstr(s->ftypes[j], typename) != NULL) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int IsDeclaredLocal(Node *n, const char *varname) {
    if (!n) return 0;
    if (n->type == NVARDECL || n->type == NCONSTDECL) {
        if (n->decl.name && strcmp(n->decl.name, varname) == 0) return 1;
    }
    if (n->type == NBLOCK || n->type == NPROGRAM) {
        for (int i = 0; i < n->block.count; i++) {
            if (IsDeclaredLocal(n->block.stmts[i], varname)) return 1;
        }
    }
    return 0;
}

static char g_moved_vars[128][128];
static int g_moved_count = 0;

static void RecordMove(const char *varname) {
    if (!varname) return;
    for (int i = 0; i < g_moved_count; i++) {
        if (!strcmp(g_moved_vars[i], varname)) return;
    }
    if (g_moved_count < 128) {
        strncpy(g_moved_vars[g_moved_count++], varname, 127);
    }
}

static void RemoveMove(const char *varname) {
    if (!varname) return;
    int write_idx = 0;
    for (int i = 0; i < g_moved_count; i++) {
        if (strcmp(g_moved_vars[i], varname) != 0) {
            if (write_idx != i) {
                strncpy(g_moved_vars[write_idx], g_moved_vars[i], 127);
            }
            write_idx++;
        }
    }
    g_moved_count = write_idx;
}

static int IsMoved(const char *varname) {
    if (!varname) return 0;
    for (int i = 0; i < g_moved_count; i++) {
        if (!strcmp(g_moved_vars[i], varname)) return 1;
    }
    return 0;
}

static void CheckBorrows(Compiler *c, Node *n, Node *root) {
    if (!n) return;
    if (n->type == NIDENT) {
        if (IsMoved(n->sval)) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                "error[E0382]: Use of moved value `%s`\n"
                "  --> line %d\n"
                "   |\n"
                " %d | let y = %s;\n"
                "   | - value moved here\n"
                " %d | print(%s);\n"
                "   | ^^^^^^ value used here after move\n"
                "   |\n"
                "note: `%s` was moved because it does not implement the `Copy` trait.",
                n->sval, n->line, n->line - 1, n->sval, n->line, n->sval, n->sval);
            Error(c, n->line, buf);
        }
    }
    if (n->type == NVARDECL || n->type == NCONSTDECL) {
        Node *init = n->decl.init;
        if (init && (init->type == NREF || init->type == NMUTREF || (init->type == NUNOP && (!strcmp(init->binop.op, "&") || !strcmp(init->binop.op, "&mut"))))) {
            Node *operand = (init->type == NREF || init->type == NMUTREF) ? init->refnode.expr : init->binop.left;
            while (operand && operand->type == NMEMBER) operand = operand->member.obj;
            while (operand && operand->type == NINDEX) operand = operand->index.arr;
            if (operand && operand->type == NIDENT) {
                const char *borrowedvar = operand->sval;
                const char *borrowvar = n->decl.name;
                int ismutborrow = (init->type == NMUTREF) || (init->type == NUNOP && !strcmp(init->binop.op, "&mut"));
                RecordBorrow(c, borrowvar, borrowedvar, c->current ? c->current->scopedepth : 0, ismutborrow, n->line);
                int lastuse = FindLastUse(root, borrowvar);
                if (lastuse < n->line) lastuse = n->line;
                CheckConflicts(c, root, borrowedvar, n->line, lastuse, ismutborrow, borrowvar);
            }
        }
    }
    if (n->type == NVARDECL || n->type == NCONSTDECL) {
        Node *init = n->decl.init;
        if (init && init->type == NIDENT) {
            const char *srcname = GetDeclaredType(root, init->sval);
            if (!srcname && c->current) {
                int slot = ResolveLocal(c, init->sval);
                if (slot >= 0) srcname = c->current->locals[slot].typename;
            }
            if (srcname && IsPinNedType(c, srcname)) {
                char buf[256];
                snprintf(buf, sizeof(buf), "PinError: cannot move pinned value or self-referential structure '%s'", init->sval);
                Error(c, n->line, buf);
            }
        }
    }
    if (n->type == NASSIGN) {
        Node *val = n->assign.value;
        if (val && val->type == NIDENT) {
            const char *srcname = GetDeclaredType(root, val->sval);
            if (!srcname && c->current) {
                int slot = ResolveLocal(c, val->sval);
                if (slot >= 0) srcname = c->current->locals[slot].typename;
            }
            if (srcname && IsPinNedType(c, srcname)) {
                char buf[256];
                snprintf(buf, sizeof(buf), "PinError: cannot move pinned value or self-referential structure '%s'", val->sval);
                Error(c, n->line, buf);
            }
            /* AKS assignment is copy semantics - do NOT record as moved */
        }
    }
    if (n->type == NCALL) {
        for (int i = 0; i < n->call.argc; i++) {
            Node *arg = n->call.args[i];
            if (arg->type == NIDENT) {
                const char *srcname = GetDeclaredType(root, arg->sval);
                if (!srcname && c->current) {
                    int slot = ResolveLocal(c, arg->sval);
                    if (slot >= 0) srcname = c->current->locals[slot].typename;
                }
                if (srcname && IsPinNedType(c, srcname)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "PinError: cannot move pinned value or self-referential structure '%s'", arg->sval);
                    Error(c, n->line, buf);
                }
            }
        }
    }

    if (n->type == NRETURN) {
        Node *expr = n->single.expr;
        if (expr) {
            if (expr->type == NIDENT) {
                int slot = ResolveLocal(c, expr->sval);
                if (slot >= 0) {
                    const char *typename = c->current->locals[slot].typename;
                    if (strchr(typename, '&') != NULL) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "LifetimeError: cannot return reference to local variable '%s'", expr->sval);
                        Error(c, n->line, buf);
                    }
                }
            } else if (expr->type == NREF || expr->type == NMUTREF || (expr->type == NUNOP && (!strcmp(expr->binop.op, "&") || !strcmp(expr->binop.op, "&mut")))) {
                Node *operand = (expr->type == NREF || expr->type == NMUTREF) ? expr->refnode.expr : expr->binop.left;
                if (operand && operand->type == NIDENT) {
                    if (IsDeclaredLocal(root, operand->sval) || ResolveLocal(c, operand->sval) >= 0) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "LifetimeError: cannot return reference to local variable '%s'", operand->sval);
                        Error(c, n->line, buf);
                    }
                }
            }
        }
    }
    if (n->type == NRETURN) {
        Node *expr = n->single.expr;
        if (expr && expr->type == NIDENT) {
            int slot = ResolveLocal(c, expr->sval);
            if (slot >= 0) {
                const char *srcname = c->current->locals[slot].typename;
                if (IsPinNedType(c, srcname)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "PinError: cannot move pinned value or self-referential structure '%s'", expr->sval);
                    Error(c, n->line, buf);
                }
            }
        }
    }
    switch (n->type) {
        case NBINOP:
            CheckBorrows(c, n->binop.left, root);
            CheckBorrows(c, n->binop.right, root);
            break;
        case NUNOP:
            CheckBorrows(c, n->binop.left, root);
            break;

        case NVARDECL:
        case NCONSTDECL:
            if (n->decl.init) {
                CheckBorrows(c, n->decl.init, root);
                if (n->decl.init->type == NIDENT) {
                    const char *srctype = GetDeclaredType(root, n->decl.init->sval);
                    int is_fn = ResolveGlobal(c, n->decl.init->sval, 0) >= 0;
                    int is_movable = !is_fn && srctype && (
                        IsStructType(c, srctype) ||
                        IsPinNedType(c, srctype) ||
                        !strcmp(srctype, "string")
                    );
                    if (is_movable) {
                        RecordMove(n->decl.init->sval);
                    }
                }
            }
            break;
        case NFNDECL:
            {
                FnProto *savedcurrent = c->current;
                for (int i = 0; i < c->fncount; i++) {
                    if (strcmp(c->fns[i]->name, n->fn.name) == 0) {
                        c->current = c->fns[i];
                        break;
                    }
                }
                int saved_moved = g_moved_count;
                g_moved_count = 0;
                CheckBorrows(c, n->fn.body, n->fn.body);
                g_moved_count = saved_moved;
                c->current = savedcurrent;
            }
            break;
        case NBLOCK:
        case NPROGRAM:
            {
                int curdepth = c->current ? c->current->scopedepth : 0;
                if (c->current) c->current->scopedepth++;
                for (int i = 0; i < n->block.count; i++) {
                    CheckBorrows(c, n->block.stmts[i], root);
                }
                if (c->current) {
                    ExitScopeBorrows(c, c->current->scopedepth);
                    c->current->scopedepth = curdepth;
                }
            }
            break;
        case NIF:
            {
                CheckBorrows(c, n->ifn.cond, root);
                int count_before = g_moved_count;
                char moved_then[128][128];
                int count_then = 0;
                char moved_els[128][128];
                int count_els = 0;

                CheckBorrows(c, n->ifn.then, root);
                count_then = g_moved_count - count_before;
                for (int i = 0; i < count_then; i++) {
                    strncpy(moved_then[i], g_moved_vars[count_before + i], 127);
                }

                if (n->ifn.els) {
                    g_moved_count = count_before;
                    CheckBorrows(c, n->ifn.els, root);
                    count_els = g_moved_count - count_before;
                    for (int i = 0; i < count_els; i++) {
                        strncpy(moved_els[i], g_moved_vars[count_before + i], 127);
                    }
                    g_moved_count = count_before;
                    for (int i = 0; i < count_then; i++) RecordMove(moved_then[i]);
                    for (int i = 0; i < count_els; i++) RecordMove(moved_els[i]);
                } else {
                    g_moved_count = count_before;
                    for (int i = 0; i < count_then; i++) RecordMove(moved_then[i]);
                }
            }
            break;
        case NTERNARY:
            CheckBorrows(c, n->ternary.cond, root);
            CheckBorrows(c, n->ternary.tval, root);
            CheckBorrows(c, n->ternary.fval, root);
            break;
        case NWHILE:
            {
                CheckBorrows(c, n->whilen.cond, root);
                CheckBorrows(c, n->whilen.body, root);
                CheckBorrows(c, n->whilen.body, root);
            }
            break;
        case NFOR:
            {
                if (n->forn.init) CheckBorrows(c, n->forn.init, root);
                if (n->forn.cond) CheckBorrows(c, n->forn.cond, root);
                CheckBorrows(c, n->forn.body, root);
                if (n->forn.post) CheckBorrows(c, n->forn.post, root);
                CheckBorrows(c, n->forn.body, root);
            }
            break;
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
        case NTHROW:
            if (n->single.expr) CheckBorrows(c, n->single.expr, root);
            break;
        case NASSIGN:
            CheckBorrows(c, n->assign.value, root);
            if (n->assign.target && n->assign.target->type == NIDENT) {
                RemoveMove(n->assign.target->sval);
            } else {
                CheckBorrows(c, n->assign.target, root);
            }
            break;
        case NCALL:
            CheckBorrows(c, n->call.callee, root);
            for (int i = 0; i < n->call.argc; i++) {
                CheckBorrows(c, n->call.args[i], root);
            }
            break;
        case NINDEX:
            CheckBorrows(c, n->index.arr, root);
            CheckBorrows(c, n->index.idx, root);
            break;
        case NMEMBER:
            CheckBorrows(c, n->member.obj, root);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                CheckBorrows(c, n->arrlit.elems[i], root);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++) {
                CheckBorrows(c, n->maplit.vals[i], root);
            }
            break;
        case NMATCH:
            CheckBorrows(c, n->match.expr, root);
            for (int i = 0; i < n->match.count; i++) {
                CheckBorrows(c, n->match.patterns[i], root);
                CheckBorrows(c, n->match.bodies[i], root);
                if (n->match.guards && n->match.guards[i]) CheckBorrows(c, n->match.guards[i], root);
            }
            break;
        case NAWAIT:
            CheckBorrows(c, n->awaitn.expr, root);
            break;
        case NSPAWN:
            CheckBorrows(c, n->spawnn.expr, root);
            break;
        case NYIELD:
            CheckBorrows(c, n->yieldstmt.expr, root);
            break;
        case NTRYPROPAGATE:
            CheckBorrows(c, n->single.expr, root);
            break;
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) {
                CheckBorrows(c, n->tuple.elems[i], root);
            }
            break;

        default: break;
    }
}

static void PreregisterTypes(Compiler *c, Node *n) {
    if (!n) return;
    switch (n->type) {
        case NPROGRAM:
        case NBLOCK: {
            for (int i = 0; i < n->block.count; i++) {
                PreregisterTypes(c, n->block.stmts[i]);
            }
            break;
        }
        case NTYPEALIAS: {
            int found = 0;
            for (int i = 0; i < c->aliascount; i++) {
                if (strcmp(c->aliases[i].name, n->typealias.name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && c->aliascount < 128) {
                CompilerTypeAlias *a = &c->aliases[c->aliascount++];
                strncpy(a->name, n->typealias.name, 127);
                strncpy(a->target, n->typealias.target, 255);
                a->isopaque = n->typealias.isopaque;
                a->tpc = n->typealias.tpc;
                if (n->typealias.tpc > 0) {
                    a->tparams = malloc(n->typealias.tpc * sizeof(char *));
                    for (int i = 0; i < n->typealias.tpc; i++) {
                        a->tparams[i] = strdup(n->typealias.tparams[i]);
                    }
                } else {
                    a->tparams = NULL;
                }
            }
            break;
        }
        case NSTRUCTDECL: {
            char *mangled = MangleName(c, n->strct.name);
            const char *struct_reg_name = mangled ? mangled : n->strct.name;
            if (n->strct.tpc > 0) {
                int found = 0;
                for (int i = 0; i < c->templatecount; i++) {
                    if (strcmp(c->templates[i].name, struct_reg_name) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found && c->templatecount < 128) {
                    TemplateDecl *t = &c->templates[c->templatecount++];
                    strncpy(t->name, struct_reg_name, 127);
                    t->ast = NodeCopy(n);
                }
            } else {
                int found = 0;
                for (int i = 0; i < c->structcount; i++) {
                    if (strcmp(c->structs[i].name, struct_reg_name) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found && c->structcount < 512) {
                    CompilerStruct *s = &c->structs[c->structcount++];
                    strncpy(s->name, struct_reg_name, 127);
                    s->fc = n->strct.fc;
                    s->fnames = malloc(s->fc * sizeof(char*));
                    s->ftypes = malloc(s->fc * sizeof(char*));
                    for (int i = 0; i < s->fc; i++) {
                        s->fnames[i] = strdup(n->strct.fnames[i]);
                        s->ftypes[i] = strdup(n->strct.ftypes[i]);
                    }
                    s->ic = n->strct.ic;
                    if (s->ic > 0) {
                        s->interfaces = malloc(s->ic * sizeof(char*));
                        for (int i = 0; i < s->ic; i++) {
                            s->interfaces[i] = strdup(n->strct.interfaces[i]);
                        }
                    } else {
                        s->interfaces = NULL;
                    }
                }
            }
            if (mangled) free(mangled);
            break;
        }
        case NENUMDECL: {
            int found = 0;
            for (int i = 0; i < c->enumcount; i++) {
                if (strcmp(c->enums[i].name, n->enumdecl.name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && c->enumcount < 128) {
                CompilerEnum *en = &c->enums[c->enumcount++];
                strncpy(en->name, n->enumdecl.name, 127);
                en->count = n->enumdecl.count;
                en->isadt = n->enumdecl.isadt;
                en->members = malloc(en->count * sizeof(char*));
                en->values = malloc(en->count * sizeof(int64_t));
                for (int i = 0; i < en->count; i++) {
                    en->members[i] = strdup(n->enumdecl.members[i]);
                    en->values[i] = n->enumdecl.values[i];
                }
            }
            break;
        }
        case NINTERFACEDECL: {
            int found = 0;
            for (int i = 0; i < c->interfacecount; i++) {
                if (strcmp(c->interfaces[i].name, n->interfacedecl.name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && c->interfacecount < 128) {
                CompilerInterface *inf = &c->interfaces[c->interfacecount++];
                strncpy(inf->name, n->interfacedecl.name, 127);
                inf->count = n->interfacedecl.count;
                inf->fnames = malloc(inf->count * sizeof(char*));
                inf->rettypes = malloc(inf->count * sizeof(char*));
                inf->paramtypes = malloc(inf->count * sizeof(char**));
                inf->paramnames = malloc(inf->count * sizeof(char**));
                inf->paramcounts = malloc(inf->count * sizeof(int));
                inf->hasdefault = malloc(inf->count * sizeof(int));
                inf->defaultbodies = malloc(inf->count * sizeof(Node*));
                for (int i = 0; i < inf->count; i++) {
                    inf->fnames[i] = strdup(n->interfacedecl.fnames[i]);
                    inf->rettypes[i] = strdup(n->interfacedecl.rettypes[i]);
                    inf->paramcounts[i] = n->interfacedecl.paramcounts[i];
                    inf->hasdefault[i] = n->interfacedecl.hasdefault ? n->interfacedecl.hasdefault[i] : 0;
                    inf->defaultbodies[i] = (n->interfacedecl.hasdefault && n->interfacedecl.hasdefault[i]) ? NodeCopy(n->interfacedecl.defaultbodies[i]) : NULL;
                    int pc = inf->paramcounts[i];
                    if (pc > 0) {
                        inf->paramtypes[i] = malloc(pc * sizeof(char*));
                        inf->paramnames[i] = malloc(pc * sizeof(char*));
                        for (int j = 0; j < pc; j++) {
                            inf->paramtypes[i][j] = strdup(n->interfacedecl.paramtypes[i][j]);
                            inf->paramnames[i][j] = strdup(n->interfacedecl.paramnames[i][j]);
                        }
                    } else {
                        inf->paramtypes[i] = NULL;
                        inf->paramnames[i] = NULL;
                    }
                }
                inf->assoccount = n->interfacedecl.assoccount;
                if (inf->assoccount > 0) {
                    inf->assoctypes = malloc(inf->assoccount * sizeof(char*));
                    for (int i = 0; i < inf->assoccount; i++) {
                        inf->assoctypes[i] = strdup(n->interfacedecl.assoctypes[i]);
                    }
                } else {
                    inf->assoctypes = NULL;
                }
            }
            break;
        }
        case NIMPORT: {
            if (n->decl.isconstflag) break;  /* Skip lazy imports  -  pre-registered when loaded */
            const char *path = n->single.var;
            char *resolved = PkgResolveImport(path, c->currentfilepath);
            char finalpath_buf[512];
            strncpy(finalpath_buf, resolved ? resolved : path, 511);
            finalpath_buf[511] = '\0';
            if (resolved) { free(resolved); resolved = NULL; }
            const char *finalpath = finalpath_buf;
            char *src = CompReadFile(finalpath);
            if (src) {
                int parseerr = 0;
                Node *importedast = Parse(src, &parseerr);
                free(src);
                if (!parseerr && importedast) {
                    char savedpath[512];
                    strncpy(savedpath, c->currentfilepath, sizeof(savedpath) - 1);
                    savedpath[sizeof(savedpath) - 1] = '\0';
                    strncpy(c->currentfilepath, finalpath, sizeof(c->currentfilepath) - 1);
                    c->currentfilepath[sizeof(c->currentfilepath) - 1] = '\0';
                    PreregisterTypes(c, importedast);
                    strncpy(c->currentfilepath, savedpath, sizeof(c->currentfilepath) - 1);
                    c->currentfilepath[sizeof(c->currentfilepath) - 1] = '\0';
                    if (c->imported_ast_count < 1024) {
                        c->imported_asts[c->imported_ast_count++] = importedast;
                    } else {
                        NodeFree(importedast);
                    }
                }
            }
            break;
        }
        case NMODULE: {
            char saved_ns[512];
            strncpy(saved_ns, c->current_namespace, 511);
            saved_ns[511] = '\0';
            if (c->current_namespace[0] != '\0') {
                char new_ns[512];
                snprintf(new_ns, sizeof(new_ns), "%s::%s", c->current_namespace, n->single.var);
                strncpy(c->current_namespace, new_ns, 511);
            } else {
                strncpy(c->current_namespace, n->single.var, 511);
            }
            c->current_namespace[511] = '\0';
            if (n->single.expr) PreregisterTypes(c, n->single.expr);
            strncpy(c->current_namespace, saved_ns, 511);
            c->current_namespace[511] = '\0';
            break;
        }
        case NUSE:
            /* Pre-pass: use aliases don't need special pre-registration */
            break;
        default: break;
    }
}
static void RunStaticChecksHelper(Compiler *c, Node *n) {
    if (!n) return;
    if (n->type == NPROGRAM || n->type == NBLOCK) {
        for (int i = 0; i < n->block.count; i++) {
            RunStaticChecksHelper(c, n->block.stmts[i]);
        }
    } else if (n->type == NFNDECL) {
        ChkStaticRecUrsion(c, n->fn.body, n->fn.name, 0);
        ChkEffects(c, n->fn.body, n->fn.effect);
        RunStaticChecksHelper(c, n->fn.body);
    } else if (n->type == NIF) {
        RunStaticChecksHelper(c, n->ifn.then);
        if (n->ifn.els) RunStaticChecksHelper(c, n->ifn.els);
    } else if (n->type == NWHILE) {
        RunStaticChecksHelper(c, n->whilen.body);
    } else if (n->type == NFOR) {
        if (n->forn.init) RunStaticChecksHelper(c, n->forn.init);
        if (n->forn.body) RunStaticChecksHelper(c, n->forn.body);
    } else if (n->type == NTRYCATCH) {
        RunStaticChecksHelper(c, n->trycatch.tryblock);
        RunStaticChecksHelper(c, n->trycatch.catchblock);
        if (n->trycatch.finallyblock) RunStaticChecksHelper(c, n->trycatch.finallyblock);
    }
}

static void RunStaticChecks(Compiler *c, Node *program) {
    RunStaticChecksHelper(c, program);
}

static void MarkStaticSafeNodes(Node *n) {
    if (!n) return;
    n->is_static_safe = 1;
    switch (n->type) {
        case NBINOP:
            MarkStaticSafeNodes(n->binop.left);
            MarkStaticSafeNodes(n->binop.right);
            break;
        case NUNOP:
            MarkStaticSafeNodes(n->binop.left);
            break;
        case NVARDECL:
        case NCONSTDECL:
            if (n->decl.init) MarkStaticSafeNodes(n->decl.init);
            break;
        case NFNDECL:
            MarkStaticSafeNodes(n->fn.body);
            break;
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) {
                MarkStaticSafeNodes(n->block.stmts[i]);
            }
            break;
        case NIF:
            MarkStaticSafeNodes(n->ifn.cond);
            MarkStaticSafeNodes(n->ifn.then);
            if (n->ifn.els) MarkStaticSafeNodes(n->ifn.els);
            break;
        case NWHILE:
            MarkStaticSafeNodes(n->whilen.cond);
            MarkStaticSafeNodes(n->whilen.body);
            break;
        case NFOR:
            if (n->forn.init) MarkStaticSafeNodes(n->forn.init);
            if (n->forn.cond) MarkStaticSafeNodes(n->forn.cond);
            if (n->forn.post) MarkStaticSafeNodes(n->forn.post);
            MarkStaticSafeNodes(n->forn.body);
            break;
        case NEXPRSTMT:
        case NRETURN:
        case NPRINT:
            if (n->single.expr) MarkStaticSafeNodes(n->single.expr);
            break;
        case NASSIGN:
            MarkStaticSafeNodes(n->assign.target);
            MarkStaticSafeNodes(n->assign.value);
            break;
        case NCALL:
            MarkStaticSafeNodes(n->call.callee);
            for (int i = 0; i < n->call.argc; i++) MarkStaticSafeNodes(n->call.args[i]);
            break;
        default:
            break;
    }
}

int CompCompile(Compiler *c, Node *program) {
    ASTSpecialize(c, program, program);
    PreregisterTypes(c, program);
    PreregisterFns(c, program);
    if (gTypeCheckingEnabled) {
        NodeFlowTypeCheck(c, program);
        RunStaticChecks(c, program);
    }
    if (gBorrowCheckerEnabled) {
        g_moved_count = 0;
        CheckBorrows(c, program, program);
        if (!c->haderror) {
            MarkStaticSafeNodes(program);
        }
    }
    program = NodeCseOpt(program);
    program = NodeOpt(program);
    program = NodeInlineOpt(program);
    program = NodeCseOpt(program);
    program = NodeOpt(program);
    program = NodeDceOpt(program);
    NodeEscapeAnalysis(program);
    EmitNode(c,program);
    for (int i = 0; i < c->fncount; i++) {
        OptBytecode(&c->fns[i]->chunk);
    }
    ChunkWrite(&c->fns[0]->chunk, OPHALT, 0);
    return !c->haderror;
}

int CompWrt(Compiler *c, const char *path) {
    FILE *f=fopen(path,"wb");
    if(!f){ perror(path); return 0; }

    // header
    uint32_t magic=AKSMAGIC;
    uint16_t version=AKSVERSION;
    uint16_t flags=0;
    uint32_t fncount=(uint32_t)c->fncount;
    uint32_t strcount=(uint32_t)c->strcount;

    fwrite(&magic,4,1,f);
    fwrite(&version,2,1,f);
    fwrite(&flags,2,1,f);
    fwrite(&fncount,4,1,f);
    fwrite(&strcount,4,1,f);

    // string pool
    for(int i=0;i<c->strcount;i++){
        uint32_t len=(uint32_t)strlen(c->strpool[i]);
        fwrite(&len,4,1,f);
        fwrite(c->strpool[i],1,len,f);
    }

    // functions
    for(int i=0;i<c->fncount;i++){
        FnProto *fn=c->fns[i];
        uint32_t nlen=(uint32_t)strlen(fn->name);
        fwrite(&nlen,4,1,f);
        fwrite(fn->name,1,nlen,f);
        uint32_t pc=(uint32_t)fn->paramcount;
        fwrite(&pc,4,1,f);
        uint32_t Lc=(uint32_t)fn->localcount;
        fwrite(&Lc,4,1,f);
        uint32_t codesize=(uint32_t)fn->chunk.count;
        fwrite(&codesize,4,1,f);
        fwrite(fn->chunk.code,1,fn->chunk.count,f);

        uint8_t isasync = (uint8_t)fn->isasync;
        fwrite(&isasync, 1, 1, f);

        for (int p = 0; p < fn->paramcount; p++) {
            const char *locname = (p < fn->localcount && fn->locals[p].name) ? fn->locals[p].name : "";
            uint32_t plen = (uint32_t)strlen(locname);
            fwrite(&plen, 4, 1, f);
            if (plen > 0) fwrite(locname, 1, plen, f);
        }

        // Write file_name
        const char *fname = fn->file_name ? fn->file_name : "";
        uint32_t flen = (uint32_t)strlen(fname);
        fwrite(&flen, 4, 1, f);
        if (flen > 0) {
            fwrite(fname, 1, flen, f);
        }

        // Write lines array
        if (fn->chunk.lines && codesize > 0) {
            fwrite(fn->chunk.lines, sizeof(int), codesize, f);
        } else {
            int *zeros = calloc(codesize > 0 ? codesize : 1, sizeof(int));
            fwrite(zeros, sizeof(int), codesize, f);
            free(zeros);
        }
    }

    // Write struct count
    uint32_t structcount = (uint32_t)c->structcount;
    fwrite(&structcount, 4, 1, f);
    
    // Write structs
    for (int i = 0; i < c->structcount; i++) {
        CompilerStruct *s = &c->structs[i];
        
        // Write struct name
        const char *sname = s->name ? s->name : "";
        uint32_t nlen = (uint32_t)strlen(sname);
        fwrite(&nlen, 4, 1, f);
        if (nlen > 0) fwrite(sname, 1, nlen, f);
        
        // Write field count
        uint32_t fc = (uint32_t)s->fc;
        fwrite(&fc, 4, 1, f);
        
        // Write fields
        for (int j = 0; j < s->fc; j++) {
            // Field name
            const char *fldname = (s->fnames && s->fnames[j]) ? s->fnames[j] : "";
            uint32_t fnlen = (uint32_t)strlen(fldname);
            fwrite(&fnlen, 4, 1, f);
            if (fnlen > 0) fwrite(fldname, 1, fnlen, f);
            
            // Field type
            const char *fldtype = (s->ftypes && s->ftypes[j]) ? s->ftypes[j] : "any";
            uint32_t ftlen = (uint32_t)strlen(fldtype);
            fwrite(&ftlen, 4, 1, f);
            if (ftlen > 0) fwrite(fldtype, 1, ftlen, f);
        }
        
        // Write interface count
        uint32_t ic = (uint32_t)s->ic;
        fwrite(&ic, 4, 1, f);
        
        // Write interfaces
        for (int j = 0; j < s->ic; j++) {
            const char *intfname = (s->interfaces && s->interfaces[j]) ? s->interfaces[j] : "";
            uint32_t inlen = (uint32_t)strlen(intfname);
            fwrite(&inlen, 4, 1, f);
            if (inlen > 0) fwrite(intfname, 1, inlen, f);
        }
    }

    fclose(f);
    return 1;
}

typedef struct {
    char name[128];
    char typename[128];
    int isconst;
    int ismoved;
} TypeBinding;

typedef struct SymbolEnv {
    TypeBinding *bindings;
    int count;
    int cap;
    struct SymbolEnv *parent;
} SymbolEnv;

typedef struct {
    char *subs[512];
    int nextvar;
} TypeContext;

static const char *GetEnvType(SymbolEnv *env, const char *name) {
    if (!name) return NULL;
    for (SymbolEnv *e = env; e != NULL; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            if (e->bindings[i].name && strcmp(e->bindings[i].name, name) == 0) {
                return e->bindings[i].typename;
            }
        }
    }
    return NULL;
}

static void SetEnvType(SymbolEnv *env, const char *name, const char *type) {
    if (!name) return;
    if (!type) type = "any";
    for (int i = 0; i < env->count; i++) {
        if (env->bindings[i].name && strcmp(env->bindings[i].name, name) == 0) {
            strncpy(env->bindings[i].typename, type, 127);
            env->bindings[i].typename[127] = '\0';
            return;
        }
    }
    if (env->count >= env->cap) {
        env->cap = env->cap == 0 ? 8 : env->cap * 2;
        env->bindings = realloc(env->bindings, env->cap * sizeof(TypeBinding));
    }
    strncpy(env->bindings[env->count].name, name, 127);
    env->bindings[env->count].name[127] = '\0';
    strncpy(env->bindings[env->count].typename, type, 127);
    env->bindings[env->count].typename[127] = '\0';
    env->bindings[env->count].isconst = 0;
    env->bindings[env->count].ismoved = 0;
    env->count++;
}

static int GetEnvConst(SymbolEnv *env, const char *name) {
    if (!name) return 0;
    for (SymbolEnv *e = env; e != NULL; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            if (e->bindings[i].name && strcmp(e->bindings[i].name, name) == 0) {
                return e->bindings[i].isconst;
            }
        }
    }
    return 0;
}

static void SetEnvConst(SymbolEnv *env, const char *name, int isconst) {
    if (!name) return;
    for (int i = 0; i < env->count; i++) {
        if (env->bindings[i].name && strcmp(env->bindings[i].name, name) == 0) {
            env->bindings[i].isconst = isconst;
            return;
        }
    }
    SetEnvType(env, name, "any");
    env->bindings[env->count - 1].isconst = isconst;
}

static int GetEnvMoved(SymbolEnv *env, const char *name) {
    if (!name) return 0;
    for (SymbolEnv *e = env; e != NULL; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            if (e->bindings[i].name && strcmp(e->bindings[i].name, name) == 0) {
                return e->bindings[i].ismoved;
            }
        }
    }
    return 0;
}

static void SetEnvMoved(SymbolEnv *env, const char *name, int ismoved) {
    if (!name) return;
    for (int i = 0; i < env->count; i++) {
        if (env->bindings[i].name && strcmp(env->bindings[i].name, name) == 0) {
            env->bindings[i].ismoved = ismoved;
            return;
        }
    }
    SetEnvType(env, name, "any");
    env->bindings[env->count - 1].ismoved = ismoved;
}

static void CopyEnvType(SymbolEnv *dest, SymbolEnv *src) {
    dest->count = src->count;
    dest->cap = src->count > 0 ? src->count : 8;
    dest->bindings = malloc(dest->cap * sizeof(TypeBinding));
    if (src->count > 0 && src->bindings) {
        memcpy(dest->bindings, src->bindings, src->count * sizeof(TypeBinding));
    }
    dest->parent = src->parent;
}

static void FreeEnv(SymbolEnv *env) {
    if (env && env->bindings) {
        free(env->bindings);
        env->bindings = NULL;
        env->count = 0;
        env->cap = 0;
    }
}

static void MakeUnion(char *dest, const char *t1, const char *t2, size_t destsize) {
    if (!t1) t1 = "any";
    if (!t2) t2 = "any";
    if (strcmp(t1, t2) == 0) {
        snprintf(dest, destsize, "%s", t1);
    } else if (strcmp(t1, "any") == 0 || strcmp(t2, "any") == 0) {
        snprintf(dest, destsize, "any");
    } else {
        snprintf(dest, destsize, "%s|%s", t1, t2);
    }
}

static void MergeEnvType(SymbolEnv *dest, SymbolEnv *e1, SymbolEnv *e2) {
    for (int i = 0; i < e1->count; i++) {
        const char *name = e1->bindings[i].name;
        const char *t1 = e1->bindings[i].typename;
        const char *t2 = GetEnvType(e2, name);
        if (t2) {
            char u[128];
            MakeUnion(u, t1, t2, sizeof(u));
            SetEnvType(dest, name, u);
            int binding_idx = dest->count - 1;
            dest->bindings[binding_idx].isconst = e1->bindings[i].isconst;
            int e2_idx = -1;
            for (int j = 0; j < e2->count; j++) {
                if (strcmp(e2->bindings[j].name, name) == 0) { e2_idx = j; break; }
            }
            if (e2_idx >= 0) {
                dest->bindings[binding_idx].ismoved = e1->bindings[i].ismoved || e2->bindings[e2_idx].ismoved;
            } else {
                dest->bindings[binding_idx].ismoved = e1->bindings[i].ismoved;
            }
        } else {
            SetEnvType(dest, name, t1);
            int binding_idx = dest->count - 1;
            dest->bindings[binding_idx].isconst = e1->bindings[i].isconst;
            dest->bindings[binding_idx].ismoved = e1->bindings[i].ismoved;
        }
    }
    for (int i = 0; i < e2->count; i++) {
        const char *name = e2->bindings[i].name;
        if (!GetEnvType(e1, name)) {
            SetEnvType(dest, name, e2->bindings[i].typename);
            int binding_idx = dest->count - 1;
            dest->bindings[binding_idx].isconst = e2->bindings[i].isconst;
            dest->bindings[binding_idx].ismoved = e2->bindings[i].ismoved;
        }
    }
}

static void NewTypevar(TypeContext *ctx, char *buf, size_t size) {
    snprintf(buf, size, "$T%d", ctx->nextvar++);
}

static const char *Resolve(TypeContext *ctx, const char *type) {
    while (type && type[0] == '$') {
        int id = atoi(type + 2);
        if (id >= 0 && id < ctx->nextvar && ctx->subs[id]) {
            type = ctx->subs[id];
        } else {
            break;
        }
    }
    return type;
}

static int Unify(Compiler *c, TypeContext *ctx, const char *t1, const char *t2) {
    t1 = Resolve(ctx, t1);
    t2 = Resolve(ctx, t2);
    if (strcmp(t1, t2) == 0) return 1;
    
    if (t1[0] == '$') {
        int id = atoi(t1 + 2);
        ctx->subs[id] = strdup(t2);
        return 1;
    }
    if (t2[0] == '$') {
        int id = atoi(t2 + 2);
        ctx->subs[id] = strdup(t1);
        return 1;
    }
    
    if (strcmp(t1, "any") == 0 || strcmp(t2, "any") == 0) return 1;
    if (strcmp(t1, "null") == 0 || strcmp(t2, "null") == 0) return 1;

    if ((!strcmp(t1, "f32x4") || !strcmp(t1, "f64x2") || !strcmp(t1, "i32x8") || !strcmp(t1, "i64x4") || !strcmp(t1, "v128")) &&
        (!strcmp(t2, "array") || t2[0] == '[' || !strncmp(t2, "Array", 5))) return 1;
    if ((!strcmp(t2, "f32x4") || !strcmp(t2, "f64x2") || !strcmp(t2, "i32x8") || !strcmp(t2, "i64x4") || !strcmp(t2, "v128")) &&
        (!strcmp(t1, "array") || t1[0] == '[' || !strncmp(t1, "Array", 5))) return 1;

    if ((t1[0] == '[' || !strncmp(t1, "Array<", 6) || !strncmp(t1, "Array <", 7) || !strcmp(t1, "array")) &&
        (t2[0] == '[' || !strncmp(t2, "Array<", 6) || !strncmp(t2, "Array <", 7) || !strcmp(t2, "array"))) {
        int s1 = -1, s2 = -1;
        const char *c1 = strchr(t1, ','); if (!c1) c1 = strchr(t1, ';');
        if (c1) s1 = atoi(c1 + 1);
        const char *c2 = strchr(t2, ','); if (!c2) c2 = strchr(t2, ';');
        if (c2) s2 = atoi(c2 + 1);
        if (s1 > 0 && s2 > 0 && s1 != s2) return 0;
        return 1;
    }
    
    if (TypesCompatible(c, t1, t2) || TypesCompatible(c, t2, t1)) return 1;
    
    return 0;
}

static void ApplyNarrowing(Compiler *c, Node *cond, SymbolEnv *env, int istrue) {
    if (!cond) return;
    if (cond->type == NBINOP) {
        const char *op = cond->binop.op;
        Node *left = cond->binop.left;
        Node *right = cond->binop.right;
        
        if ((strcmp(op, "!=") == 0 && istrue) || (strcmp(op, "==") == 0 && !istrue)) {
            if (left->type == NIDENT && right->type == NNULL) {
                const char *curr = GetEnvType(env, left->sval);
                if (curr) {
                    char narrow[128];
                    if (strcmp(curr, "any") == 0) {
                        // keep any
                    } else {
                        char *p = strstr(curr, "|null");
                        if (p) {
                            int len = (int)(p - curr);
                            strncpy(narrow, curr, len);
                            narrow[len] = '\0';
                            SetEnvType(env, left->sval, narrow);
                        }
                    }
                }
            }
        }
        if ((strcmp(op, "==") == 0 && istrue) || (strcmp(op, "!=") == 0 && !istrue)) {
            if (left->type == NIDENT && right->type == NNULL) {
                SetEnvType(env, left->sval, "null");
            }
        }
        if (strcmp(op, "==") == 0 && istrue) {
            if (left->type == NCALL && left->call.callee->type == NIDENT &&
                strcmp(left->call.callee->sval, "type") == 0 && left->call.argc == 1 &&
                left->call.args[0]->type == NIDENT && right->type == NSTR) {
                SetEnvType(env, left->call.args[0]->sval, right->sval);
            }
        }
    }
}

static void NodeCheck(Compiler *c, TypeContext *ctx, Node *n, SymbolEnv *env, char *outtype, size_t outsize);

static void CollectReTurnTypes(Compiler *c, TypeContext *ctx, Node *n, SymbolEnv *env, char *outtype, size_t outsize) {
    if (!n) return;
    if (n->type == NRETURN) {
        char ValKind[128] = "void";
        if (n->single.expr) {
            NodeCheck(c, ctx, n->single.expr, env, ValKind, sizeof(ValKind));
        }
        if (outtype[0] == '\0') {
            strncpy(outtype, ValKind, outsize - 1);
            outtype[outsize - 1] = '\0';
        } else {
            char merged[256];
            MakeUnion(merged, outtype, ValKind, sizeof(merged));
            strncpy(outtype, merged, outsize - 1);
            outtype[outsize - 1] = '\0';
        }
        return;
    }
    switch (n->type) {
        case NVARDECL:
        case NCONSTDECL: {
            char ValKind[128] = "any";
            if (n->decl.init) {
                NodeCheck(c, ctx, n->decl.init, env, ValKind, sizeof(ValKind));
            }
            const char *decltype = n->decl.tname;
            if (decltype && decltype[0] != '\0' && strcmp(decltype, "any") != 0) {
                SetEnvType(env, n->decl.name, decltype);
            } else {
                SetEnvType(env, n->decl.name, ValKind);
            }
            break;
        }
        case NPROGRAM:
        case NBLOCK:
            for (int i = 0; i < n->block.count; i++) {
                CollectReTurnTypes(c, ctx, n->block.stmts[i], env, outtype, outsize);
            }
            break;
        case NIF:
            CollectReTurnTypes(c, ctx, n->ifn.then, env, outtype, outsize);
            if (n->ifn.els) CollectReTurnTypes(c, ctx, n->ifn.els, env, outtype, outsize);
            break;
        case NWHILE:
            CollectReTurnTypes(c, ctx, n->whilen.body, env, outtype, outsize);
            break;
        case NFOR:
            CollectReTurnTypes(c, ctx, n->forn.body, env, outtype, outsize);
            break;
        case NTRYCATCH:
            CollectReTurnTypes(c, ctx, n->trycatch.tryblock, env, outtype, outsize);
            if (n->trycatch.catchblock) CollectReTurnTypes(c, ctx, n->trycatch.catchblock, env, outtype, outsize);
            if (n->trycatch.finallyblock) CollectReTurnTypes(c, ctx, n->trycatch.finallyblock, env, outtype, outsize);
            break;
        case NSWITCH:
            for (int i = 0; i < n->sw.casecount; i++) {
                CollectReTurnTypes(c, ctx, n->sw.casebodies[i], env, outtype, outsize);
            }
            if (n->sw.defaultbody) CollectReTurnTypes(c, ctx, n->sw.defaultbody, env, outtype, outsize);
            break;
        default:
            break;
    }
}

static int GetTupleElementTypes(const char *tstr, char elements[32][128]) {
    if (tstr[0] != '(') return 0;
    int len = (int)strlen(tstr);
    if (tstr[len - 1] != ')') return 0;
    
    int count = 0;
    int depth = 0;
    int start = 1;
    for (int i = 1; i < len - 1; i++) {
        if (tstr[i] == '(' || tstr[i] == '{' || tstr[i] == '<') {
            depth++;
        } else if (tstr[i] == ')' || tstr[i] == '}' || tstr[i] == '>') {
            depth--;
        }
        
        if (depth == 0 && tstr[i] == ',') {
            int elen = i - start;
            if (elen > 127) elen = 127;
            if (elen > 0 && count < 32) {
                memcpy(elements[count], tstr + start, elen);
                elements[count][elen] = '\0';
                count++;
            }
            start = i + 1;
        }
    }
    int elen = (len - 1) - start;
    if (elen > 127) elen = 127;
    if (elen > 0 && count < 32) {
        memcpy(elements[count], tstr + start, elen);
        elements[count][elen] = '\0';
        count++;
    }
    return count;
}

static void TypeCheckStmt(Compiler *c, TypeContext *ctx, Node *n, SymbolEnv *env) {
    if (!n) return;
    switch (n->type) {
        case NPROGRAM:
        case NBLOCK: {
            for (int i = 0; i < n->block.count; i++) {
                TypeCheckStmt(c, ctx, n->block.stmts[i], env);
            }
            break;
        }
        case NVARDECL:
        case NCONSTDECL: {
            char ValKind[128] = "any";
            if (n->decl.init) {
                if (gBorrowCheckerEnabled && n->decl.init->type == NIDENT) {
                    if (GetEnvMoved(env, n->decl.init->sval)) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "UseOfMovedValueError: use of moved value '%s'", n->decl.init->sval);
                        Error(c, n->line, buf);
                    }
                }
                NodeCheck(c, ctx, n->decl.init, env, ValKind, sizeof(ValKind));
                if (gBorrowCheckerEnabled && n->decl.init->type == NIDENT) {
                    if (IsNonPrimitiveType(ValKind)) {
                        SetEnvMoved(env, n->decl.init->sval, 1);
                    }
                }
            }
            int isconst = n->decl.isconstflag;
            if (n->type == NVARDECL && gImmutableByDefault) {
                isconst = 1;
            }
            const char *decltype = n->decl.tname;
            if (decltype && decltype[0] != '\0' && strcmp(decltype, "any") != 0) {
                if (!Unify(c, ctx, decltype, ValKind)) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "TypeError: declared type '%s' not compatible with value type '%s'", decltype, ValKind);
                    Error(c, n->line, msg);
                }
                SetEnvType(env, n->decl.name, decltype);
            } else {
                SetEnvType(env, n->decl.name, ValKind);
            }
            SetEnvConst(env, n->decl.name, isconst);
            if (decltype && strchr(decltype, '{')) {
                ChkRefConst(c, decltype, n->line, n->decl.init);
            }
            break;
        }
        case NEXPRSTMT: {
            TypeCheckStmt(c, ctx, n->single.expr, env);
            break;
        }
        case NDESTRUCTARR: {
            char ValKind[128] = "any";
            NodeCheck(c, ctx, n->destructarr.value, env, ValKind, sizeof(ValKind));
            char elements[32][128];
            int elemcount = GetTupleElementTypes(ValKind, elements);
            for (int i = 0; i < n->destructarr.count; i++) {
                char etype[128] = "any";
                if (i < elemcount) {
                    strcpy(etype, elements[i]);
                }
                SetEnvType(env, n->destructarr.names[i], etype);
            }
            break;
        }
        case NDESTRUCTOBJ: {
            char ValKind[128] = "any";
            NodeCheck(c, ctx, n->destructobj.value, env, ValKind, sizeof(ValKind));
            char cleantype[128];
            strncpy(cleantype, ValKind, sizeof(cleantype) - 1);
            cleantype[sizeof(cleantype) - 1] = '\0';
            char *brace = strchr(cleantype, '{');
            if (brace) *brace = '\0';
            
            for (int i = 0; i < n->destructobj.count; i++) {
                char etype[128] = "any";
                for (int s = 0; s < c->structcount; s++) {
                    if (strcmp(c->structs[s].name, cleantype) == 0) {
                        for (int f = 0; f < c->structs[s].fc; f++) {
                            if (strcmp(c->structs[s].fnames[f], n->destructobj.keys[i]) == 0) {
                                strcpy(etype, c->structs[s].ftypes[f]);
                                break;
                            }
                        }
                        break;
                    }
                }
                SetEnvType(env, n->destructobj.aliases[i], etype);
            }
            break;
        }
        case NFNDECL: {
            SymbolEnv fnenv;
            memset(&fnenv, 0, sizeof(fnenv));
            for (int i = 0; i < n->fn.pc; i++) {
                const char *ptype = n->fn.ptypes[i];
                if (!ptype || ptype[0] == '\0') ptype = "any";
                SetEnvType(&fnenv, n->fn.pnames[i], ptype);
            }
            
            FnProto *parent = c->current;
            int fnid = ResolveGlobal(c, n->fn.name, 0);
            if (fnid >= 0 && fnid < c->fncount && c->fns[fnid]) {
                c->current = c->fns[fnid];
            }
            
            char inferredret[128] = "";
            CollectReTurnTypes(c, ctx, n->fn.body, &fnenv, inferredret, sizeof(inferredret));
            if (inferredret[0] == '\0') {
                strcpy(inferredret, "void");
            }
            
            if (fnid >= 0 && fnid < c->fncount && c->fns[fnid]) {
                if (c->fns[fnid]->rettype[0] == '\0' || strcmp(c->fns[fnid]->rettype, "any") == 0) {
                    strncpy(c->fns[fnid]->rettype, inferredret, 63);
                    c->fns[fnid]->rettype[63] = '\0';
                }
            }
            
            TypeCheckStmt(c, ctx, n->fn.body, &fnenv);
            FreeEnv(&fnenv);
            c->current = parent;
            break;
        }
        case NIF: {
            SymbolEnv thenenv, elseenv;
            CopyEnvType(&thenenv, env);
            CopyEnvType(&elseenv, env);
            
            ApplyNarrowing(c, n->ifn.cond, &thenenv, 1);
            ApplyNarrowing(c, n->ifn.cond, &elseenv, 0);
            
            TypeCheckStmt(c, ctx, n->ifn.then, &thenenv);
            if (n->ifn.els) {
                TypeCheckStmt(c, ctx, n->ifn.els, &elseenv);
                MergeEnvType(env, &thenenv, &elseenv);
            } else {
                MergeEnvType(env, &thenenv, env);
            }
            FreeEnv(&thenenv);
            FreeEnv(&elseenv);
            break;
        }
        case NWHILE: {
            SymbolEnv loopenv;
            CopyEnvType(&loopenv, env);
            ApplyNarrowing(c, n->whilen.cond, &loopenv, 1);
            TypeCheckStmt(c, ctx, n->whilen.body, &loopenv);
            MergeEnvType(env, &loopenv, env);
            FreeEnv(&loopenv);
            break;
        }
        case NFOR: {
            SymbolEnv forenv;
            CopyEnvType(&forenv, env);
            if (n->forn.init) TypeCheckStmt(c, ctx, n->forn.init, &forenv);
            if (n->forn.cond) ApplyNarrowing(c, n->forn.cond, &forenv, 1);
            TypeCheckStmt(c, ctx, n->forn.body, &forenv);
            if (n->forn.post) TypeCheckStmt(c, ctx, n->forn.post, &forenv);
            MergeEnvType(env, &forenv, env);
            FreeEnv(&forenv);
            break;
        }
        case NTRYCATCH: {
            c->trydepth++;
            TypeCheckStmt(c, ctx, n->trycatch.tryblock, env);
            c->trydepth--;
            if (n->trycatch.catchblock) {
                SymbolEnv catchenv;
                CopyEnvType(&catchenv, env);
                SetEnvType(&catchenv, n->trycatch.errvar, "string");
                TypeCheckStmt(c, ctx, n->trycatch.catchblock, &catchenv);
                FreeEnv(&catchenv);
            }
            if (n->trycatch.finallyblock) {
                TypeCheckStmt(c, ctx, n->trycatch.finallyblock, env);
            }
            break;
        }
        case NASSIGN: {
            char ValKind[128] = "any";
            if (gBorrowCheckerEnabled && n->assign.value->type == NIDENT) {
                if (GetEnvMoved(env, n->assign.value->sval)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "UseOfMovedValueError: use of moved value '%s'", n->assign.value->sval);
                    Error(c, n->line, buf);
                }
            }
            NodeCheck(c, ctx, n->assign.value, env, ValKind, sizeof(ValKind));
            if (n->assign.target->type == NIDENT) {
                if (GetEnvConst(env, n->assign.target->sval)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "ConstError: cannot assign to immutable variable '%s'", n->assign.target->sval);
                    Error(c, n->line, buf);
                }
                if (gBorrowCheckerEnabled && n->assign.value->type == NIDENT) {
                    if (IsNonPrimitiveType(ValKind)) {
                        SetEnvMoved(env, n->assign.value->sval, 1);
                    }
                }
                SetEnvMoved(env, n->assign.target->sval, 0);
                const char *expectedtype = GetEnvType(env, n->assign.target->sval);
                if (expectedtype && strchr(expectedtype, '{')) {
                    ChkRefConst(c, expectedtype, n->line, n->assign.value);
                }
                SetEnvType(env, n->assign.target->sval, ValKind);
            } else if (n->assign.target->type == NMEMBER) {
                char objtype[128] = "any";
                NodeCheck(c, ctx, n->assign.target->member.obj, env, objtype, sizeof(objtype));
                char cleantype[128];
                strncpy(cleantype, objtype, sizeof(cleantype) - 1);
                cleantype[sizeof(cleantype) - 1] = '\0';
                char *brace = strchr(cleantype, '{');
                if (brace) *brace = '\0';
                
                const char *fieldname = n->assign.target->member.field;
                for (int i = 0; i < c->structcount; i++) {
                    if (strcmp(c->structs[i].name, cleantype) == 0) {
                        for (int j = 0; j < c->structs[i].fc; j++) {
                            if (strcmp(c->structs[i].fnames[j], fieldname) == 0) {
                                if (strcmp(c->structs[i].ftypes[j], "any") == 0 || c->structs[i].ftypes[j][0] == '\0') {
                                    free(c->structs[i].ftypes[j]);
                                    c->structs[i].ftypes[j] = strdup(ValKind);
                                } else {
                                    if (!Unify(c, ctx, c->structs[i].ftypes[j], ValKind)) {
                                        if (gStrictTypes) {
                                            char msg[512];
                                            snprintf(msg, sizeof(msg), "TypeError: field '%s' expects type '%s', got '%s'", fieldname, c->structs[i].ftypes[j], ValKind);
                                            Error(c, n->line, msg);
                                        } else {
                                            fprintf(stderr, "Warning: field '%s' expects type '%s', got '%s'\n", fieldname, c->structs[i].ftypes[j], ValKind);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case NRETURN: {
            char ValKind[128] = "void";
            if (n->single.expr) {
                NodeCheck(c, ctx, n->single.expr, env, ValKind, sizeof(ValKind));
            }
            if (c->current) {
                if (c->current->rettype[0] != '\0' && strcmp(c->current->rettype, "any") != 0 && strcmp(c->current->rettype, "void") != 0) {
                    if (!Unify(c, ctx, c->current->rettype, ValKind)) {
                        if (gStrictTypes) {
                            char msg[512];
                            snprintf(msg, sizeof(msg), "TypeError: function return type expects '%s', got '%s'", c->current->rettype, ValKind);
                            Error(c, n->line, msg);
                        } else {
                            fprintf(stderr, "Warning: function return type expects '%s', got '%s'\n", c->current->rettype, ValKind);
                        }
                    }
                }
            }
            break;
        }
        default: {
            char dummy[128];
            NodeCheck(c, ctx, n, env, dummy, sizeof(dummy));
            break;
        }
    }
}

static void NodeCheck(Compiler *c, TypeContext *ctx, Node *n, SymbolEnv *env, char *outtype, size_t outsize) {
    if (!n) {
        strncpy(outtype, "void", outsize);
        return;
    }
    switch (n->type) {
        case NINT: strncpy(outtype, "int", outsize); break;
        case NFLOAT: strncpy(outtype, "float", outsize); break;
        case NSTR: strncpy(outtype, "string", outsize); break;
        case NBOOL: strncpy(outtype, "bool", outsize); break;
        case NNULL: strncpy(outtype, "null", outsize); break;
        case NARRLIT: {
            char elemtype[128] = "any";
            if (n->arrlit.count > 0) {
                NodeCheck(c, ctx, n->arrlit.elems[0], env, elemtype, sizeof(elemtype));
            }
            snprintf(outtype, outsize, "Array<%s,%d>", elemtype, n->arrlit.count);
            break;
        }
        case NTUPLE: {
            char buf[512] = "(";
            int len = 1;
            for (int i = 0; i < n->tuple.count; i++) {
                if (i > 0) {
                    if (len + 1 < (int)sizeof(buf)) {
                        strcat(buf, ",");
                        len++;
                    }
                }
                char etype[128] = "";
                NodeCheck(c, ctx, n->tuple.elems[i], env, etype, sizeof(etype));
                if (len < (int)sizeof(buf) - 1) {
                    strncat(buf, etype, sizeof(buf) - len - 1);
                    len = (int)strlen(buf);
                }
            }
            if (len + 1 < (int)sizeof(buf)) {
                strcat(buf, ")");
            }
            strncpy(outtype, buf, outsize);
            break;
        }
        case NMAPLIT: strncpy(outtype, "Object", outsize); break;
        case NIDENT: {
            if (!n->sval) {
                strncpy(outtype, "any", outsize);
                break;
            }
            if (gBorrowCheckerEnabled) {
                if (GetEnvMoved(env, n->sval)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "UseOfMovedValueError: use of moved value '%s'", n->sval);
                    Error(c, n->line, buf);
                }
            }
            const char *t = GetEnvType(env, n->sval);
            if (t) {
                strncpy(outtype, t, outsize);
            } else {
                int slot = ResolveLocal(c, n->sval);
                if (slot >= 0 && c->current && slot < c->current->localcount) {
                    const char *tname = c->current->locals[slot].typename;
                    strncpy(outtype, tname ? tname : "any", outsize);
                } else {
                    int fnid = ResolveGlobal(c, n->sval, n->line);
                    if (fnid >= 0) {
                        strncpy(outtype, "fn", outsize);
                    } else {
                        NewTypevar(ctx, outtype, outsize);
                        SetEnvType(env, n->sval, outtype);
                    }
                }
            }
            break;
        }
        case NBINOP: {
            char lt[128] = "", rt[128] = "";
            NodeCheck(c, ctx, n->binop.left, env, lt, sizeof(lt));
            NodeCheck(c, ctx, n->binop.right, env, rt, sizeof(rt));
            
            const char *op = n->binop.op ? n->binop.op : "+";
            if (strcmp(op, "as") == 0) {
                const char *astarget = (n->binop.right && n->binop.right->sval) ? n->binop.right->sval : "any";
                strncpy(outtype, astarget, outsize);
            } else if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
                strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
                strncpy(outtype, "bool", outsize);
            } else {
                Unify(c, ctx, lt, rt);
                if (strcmp(op, "+") == 0 && (strcmp(lt, "string") == 0 || strcmp(rt, "string") == 0)) {
                    strncpy(outtype, "string", outsize);
                } else if (strcmp(lt, "float") == 0 || strcmp(rt, "float") == 0) {
                    strncpy(outtype, "float", outsize);
                } else {
                    strncpy(outtype, "int", outsize);
                }
            }
            break;
        }
        case NUNOP: {
            NodeCheck(c, ctx, n->binop.left, env, outtype, outsize);
            break;
        }
        case NCALL: {
            char calleetype[128] = "";
            NodeCheck(c, ctx, n->call.callee, env, calleetype, sizeof(calleetype));
            for (int i = 0; i < n->call.argc; i++) {
                char argtype[128] = "";
                NodeCheck(c, ctx, n->call.args[i], env, argtype, sizeof(argtype));
            }
            if (n->call.callee && n->call.callee->type == NIDENT && n->call.callee->sval) {
                int fnid = ResolveGlobal(c, n->call.callee->sval, n->line);
                if (fnid >= 0 && fnid < c->fncount && c->fns[fnid]) {
                    FnProto *fn = c->fns[fnid];
                    for (int i = 0; i < n->call.argc && i < fn->paramcount; i++) {
                        const char *ptype = fn->paramtypes[i];
                        if (ptype && strchr(ptype, '{')) {
                            ChkRefConst(c, ptype, n->line, n->call.args[i]);
                        }
                    }
                    if (fn->rettype[0] != '\0') {
                        strncpy(outtype, fn->rettype, outsize);
                    } else {
                        strncpy(outtype, "any", outsize);
                    }
                    break;
                }
            }
            strncpy(outtype, "any", outsize);
            break;
        }
        case NMEMBER: {
            char objtype[128] = "";
            NodeCheck(c, ctx, n->member.obj, env, objtype, sizeof(objtype));
            char cleantype[128];
            strncpy(cleantype, objtype, sizeof(cleantype) - 1);
            cleantype[sizeof(cleantype) - 1] = '\0';
            char *brace = strchr(cleantype, '{');
            if (brace) *brace = '\0';
            const char *field = n->member.field ? n->member.field : "";
            
            for (int i = 0; i < c->structcount; i++) {
                if (c->structs[i].name && strcmp(c->structs[i].name, cleantype) == 0) {
                    for (int j = 0; j < c->structs[i].fc; j++) {
                        if (c->structs[i].fnames && c->structs[i].fnames[j] && strcmp(c->structs[i].fnames[j], field) == 0) {
                            const char *ft = (c->structs[i].ftypes && c->structs[i].ftypes[j]) ? c->structs[i].ftypes[j] : "any";
                            strncpy(outtype, ft, outsize);
                            return;
                        }
                    }
                }
            }
            strncpy(outtype, "any", outsize);
            break;
        }
        case NNEW: {
            const char *objname = n->newobj.name ? n->newobj.name : "Object";
            strncpy(outtype, objname, outsize);
            break;
        }
        case NPRINT: {
            char dummy[128];
            NodeCheck(c, ctx, n->single.expr, env, dummy, sizeof(dummy));
            strncpy(outtype, "void", outsize);
            break;
        }
        case NINDEX: {
            char arrtype[128] = "";
            char idxtype[128] = "";
            NodeCheck(c, ctx, n->index.arr, env, arrtype, sizeof(arrtype));
            NodeCheck(c, ctx, n->index.idx, env, idxtype, sizeof(idxtype));
            strncpy(outtype, "any", outsize);
            break;
        }
        case NTERNARY: {
            char condt[128], tt[128], ft[128];
            NodeCheck(c, ctx, n->ternary.cond, env, condt, sizeof(condt));
            NodeCheck(c, ctx, n->ternary.tval, env, tt, sizeof(tt));
            NodeCheck(c, ctx, n->ternary.fval, env, ft, sizeof(ft));
            Unify(c, ctx, tt, ft);
            strncpy(outtype, tt, outsize);
            break;
        }
        default:
            strncpy(outtype, "any", outsize);
            break;
    }
}

void NodeFlowTypeCheck(Compiler *c, Node *program) {
    c->is_type_checking_pass = 1;
    TypeContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    SymbolEnv env;
    memset(&env, 0, sizeof(env));
    TypeCheckStmt(c, &ctx, program, &env);
    for (int i = 0; i < ctx.nextvar; i++) {
        if (ctx.subs[i]) free(ctx.subs[i]);
    }
    FreeEnv(&env);
    c->is_type_checking_pass = 0;
}
