// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "ast.h"
#include <stdlib.h>
#include <string.h>

// The language's 48-bit signed integer minimum; division/modulo/negation of
// this value by -1 overflows and must reach the VM's runtime checks.
#define INT48_MIN (-140737488355328LL)

typedef struct {
    char name[128];
    Node *decl;
} InlineableFn;

static void NodeSubstitute(Node **nodeptr, const char *varname, int64_t val);
static void ASTPropagateConsts(Node **nodeptr, char **names, Node **vals, int count);
static int CountReferences(Node *node, const char *name);


Node *NodeNew(NType t, int line) {
    Node *n = calloc(1, sizeof(Node));
    n->type = t; n->line = line;
    n->isstack = 0;
    return n;
}

void NodeFree(Node *n) {
    if (!n) return;
    switch (n->type) {
        case NSTR: case NIDENT: free(n->sval); break;
        case NBOOL: case NINT: case NFLOAT: case NNULL: break;
        case NBINOP: case NUNOP:
            free(n->binop.op);
            NodeFree(n->binop.left);
            NodeFree(n->binop.right);
            break;
        case NREF: case NMUTREF:
            NodeFree(n->refnode.expr);
            break;
        case NVARDECL: case NCONSTDECL:
            free(n->decl.name); free(n->decl.tname);
            NodeFree(n->decl.init);
            NodeFree(n->decl.isconst);
            break;
        case NFNDECL:
        case NMACRODECL:
            if (n->fn.name) free(n->fn.name);
            if (n->fn.ret) free(n->fn.ret);
            if (n->fn.effect) free(n->fn.effect);
            for (int i = 0; i < n->fn.pc; i++) {
                if (n->fn.pnames && n->fn.pnames[i]) free(n->fn.pnames[i]);
                if (n->fn.ptypes && n->fn.ptypes[i]) free(n->fn.ptypes[i]);
            }
            if (n->fn.pnames) free(n->fn.pnames);
            if (n->fn.ptypes) free(n->fn.ptypes);
            if (n->fn.pdefaults) {
                for (int i=0;i<n->fn.pc;i++) NodeFree(n->fn.pdefaults[i]);
                free(n->fn.pdefaults);
            }
            if (n->fn.requires) {
                for (int i=0;i<n->fn.req_count;i++) NodeFree(n->fn.requires[i]);
                free(n->fn.requires);
            }
            if (n->fn.ensures) {
                for (int i=0;i<n->fn.ens_count;i++) NodeFree(n->fn.ensures[i]);
                free(n->fn.ensures);
            }
            NodeFree(n->fn.body);
            if (n->fn.tpc > 0 && n->fn.tparams) {
                for (int i=0;i<n->fn.tpc;i++) free(n->fn.tparams[i]);
                free(n->fn.tparams);
            }
            break;
        case NBLOCK: case NPROGRAM:
            for(int i=0;i<n->block.count;i++) NodeFree(n->block.stmts[i]);
            free(n->block.stmts);
            break;
        case NIF:
            NodeFree(n->ifn.cond); NodeFree(n->ifn.then); NodeFree(n->ifn.els);
            break;
        case NTERNARY:
            NodeFree(n->ternary.cond); NodeFree(n->ternary.tval); NodeFree(n->ternary.fval);
            break;
        case NWHILE:
            NodeFree(n->whilen.cond); NodeFree(n->whilen.body);
            if (n->whilen.invariants) {
                for (int i=0;i<n->whilen.inv_count;i++) NodeFree(n->whilen.invariants[i]);
                free(n->whilen.invariants);
            }
            break;
        case NFOR:
            NodeFree(n->forn.init); NodeFree(n->forn.cond);
            NodeFree(n->forn.post); NodeFree(n->forn.body);
            if (n->forn.invariants) {
                for (int i=0;i<n->forn.inv_count;i++) NodeFree(n->forn.invariants[i]);
                free(n->forn.invariants);
            }
            break;
        case NFORIN:
            free(n->forin.var);
            NodeFree(n->forin.iter); NodeFree(n->forin.body);
            break;
        case NSELECT:
            for (int i = 0; i < n->selectn.count; i++) {
                NodeFree(n->selectn.cases[i]);
                NodeFree(n->selectn.bodies[i]);
            }
            free(n->selectn.cases);
            free(n->selectn.bodies);
            NodeFree(n->selectn.defaultbody);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NINPUT:
            NodeFree(n->single.expr); free(n->single.var);
            break;
        case NCAST:
            NodeFree(n->cast.expr); free(n->cast.targettype);
            break;
        case NMODULE:
            free(n->single.var);      /* module name */
            NodeFree(n->single.expr); /* module body block */
            break;
        case NUSE:
            free(n->decl.name);   /* alias (may be NULL) */
            free(n->decl.tname);  /* target path */
            break;
        case NTRYCATCH:
            NodeFree(n->trycatch.tryblock);
            free(n->trycatch.errvar);
            NodeFree(n->trycatch.catchblock);
            NodeFree(n->trycatch.finallyblock);
            break;
        case NTHROW:
            NodeFree(n->throwstmt.expr);
            break;
        case NCOALESCE:
            NodeFree(n->coalesce.left);
            NodeFree(n->coalesce.right);
            break;
        case NOPTCHAIN:
            NodeFree(n->optchain.obj);
            free(n->optchain.field);
            NodeFree(n->optchain.idx);
            break;
        case NSTRUCTINIT:
            if (n->structinit.typename) free(n->structinit.typename);
            if (n->structinit.spread) NodeFree(n->structinit.spread);
            for (int i = 0; i < n->structinit.fc; i++) {
                free(n->structinit.fnames[i]);
                NodeFree(n->structinit.fvals[i]);
            }
            if (n->structinit.fnames) free(n->structinit.fnames);
            if (n->structinit.fvals) free(n->structinit.fvals);
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < n->chainedcmp.count; i++) {
                NodeFree(n->chainedcmp.exprs[i]);
            }
            free(n->chainedcmp.exprs);
            for (int i = 0; i < n->chainedcmp.count - 1; i++) {
                free(n->chainedcmp.ops[i]);
            }
            free(n->chainedcmp.ops);
            break;
        case NRANGE:
            NodeFree(n->range.start);
            NodeFree(n->range.end);
            break;
        case NASSIGN:
            NodeFree(n->assign.target); NodeFree(n->assign.value);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            NodeFree(n->call.callee);
            for(int i=0;i<n->call.argc;i++) NodeFree(n->call.args[i]);
            free(n->call.args);
            free(n->call.isspread);
            if (n->call.tc > 0 && n->call.targs) {
                for (int i=0;i<n->call.tc;i++) free(n->call.targs[i]);
                free(n->call.targs);
            }
            break;
        case NINDEX:
            NodeFree(n->index.arr); NodeFree(n->index.idx);
            break;
        case NMEMBER:
            NodeFree(n->member.obj); free(n->member.field);
            break;
        case NARRLIT:
            for(int i=0;i<n->arrlit.count;i++) NodeFree(n->arrlit.elems[i]);
            free(n->arrlit.elems);
            free(n->arrlit.isspread);
            break;
        case NMAPLIT:
            for(int i=0;i<n->maplit.count;i++) {
                free(n->maplit.keys[i]);
                NodeFree(n->maplit.vals[i]);
            }
            free(n->maplit.keys);
            free(n->maplit.vals);
            break;
        case NSTRUCTDECL:
            free(n->strct.name);
            for(int i=0;i<n->strct.fc;i++){free(n->strct.fnames[i]);free(n->strct.ftypes[i]);}
            free(n->strct.fnames); free(n->strct.ftypes);
            if (n->strct.ic > 0 && n->strct.interfaces) {
                for(int i=0; i<n->strct.ic; i++) free(n->strct.interfaces[i]);
                free(n->strct.interfaces);
            }
            if (n->strct.tpc > 0 && n->strct.tparams) {
                for(int i=0; i<n->strct.tpc; i++) free(n->strct.tparams[i]);
                free(n->strct.tparams);
            }
            if (n->strct.constraintcount > 0) {
                for (int i = 0; i < n->strct.constraintcount; i++) {
                    free(n->strct.constraintparams[i]);
                    free(n->strct.constrainttypes[i]);
                }
                free(n->strct.constraintparams);
                free(n->strct.constrainttypes);
            }
            break;
        case NNEW:
            free(n->newobj.name);
            if (n->newobj.tc > 0 && n->newobj.targs) {
                for(int i=0; i<n->newobj.tc; i++) free(n->newobj.targs[i]);
                free(n->newobj.targs);
            }
            break;
        case NMATCH:
            NodeFree(n->match.expr);
            for(int i=0; i<n->match.count; i++) {
                NodeFree(n->match.patterns[i]);
                NodeFree(n->match.bodies[i]);
                if (n->match.guards && n->match.guards[i]) {
                    NodeFree(n->match.guards[i]);
                }
            }
            free(n->match.patterns);
            free(n->match.bodies);
            if (n->match.guards) free(n->match.guards);
            break;
        case NTUPLE:
            for(int i=0; i<n->tuple.count; i++) {
                NodeFree(n->tuple.elems[i]);
            }
            free(n->tuple.elems);
            break;
        case NTUPLEPAT:
            for(int i=0; i<n->tuplepat.count; i++) {
                NodeFree(n->tuplepat.patterns[i]);
            }
            free(n->tuplepat.patterns);
            break;
        case NSTRUCTPAT:
            free(n->structpat.name);
            for(int i=0; i<n->structpat.count; i++) {
                free(n->structpat.fields[i]);
                NodeFree(n->structpat.patterns[i]);
            }
            free(n->structpat.fields);
            free(n->structpat.patterns);
            break;
        case NORPAT:
            NodeFree(n->orpat.left);
            NodeFree(n->orpat.right);
            break;
        case NINTERFACEDECL:
            free(n->interfacedecl.name);
            for(int i=0; i<n->interfacedecl.count; i++) {
                free(n->interfacedecl.fnames[i]);
                free(n->interfacedecl.rettypes[i]);
                if (n->interfacedecl.paramcounts[i] > 0) {
                    for(int j=0; j<n->interfacedecl.paramcounts[i]; j++) {
                        free(n->interfacedecl.paramtypes[i][j]);
                        free(n->interfacedecl.paramnames[i][j]);
                    }
                    free(n->interfacedecl.paramtypes[i]);
                    free(n->interfacedecl.paramnames[i]);
                }
                if (n->interfacedecl.hasdefault && n->interfacedecl.hasdefault[i]) {
                    NodeFree(n->interfacedecl.defaultbodies[i]);
                }
            }
            free(n->interfacedecl.fnames);
            free(n->interfacedecl.rettypes);
            free(n->interfacedecl.paramtypes);
            free(n->interfacedecl.paramnames);
            free(n->interfacedecl.paramcounts);
            if (n->interfacedecl.hasdefault) {
                free(n->interfacedecl.hasdefault);
                free(n->interfacedecl.defaultbodies);
            }
            if (n->interfacedecl.assoccount > 0) {
                for (int i = 0; i < n->interfacedecl.assoccount; i++) {
                    free(n->interfacedecl.assoctypes[i]);
                }
                free(n->interfacedecl.assoctypes);
            }
            break;
        case NTYPEALIAS:
            free(n->typealias.name);
            free(n->typealias.target);
            for (int i = 0; i < n->typealias.tpc; i++) {
                free(n->typealias.tparams[i]);
            }
            free(n->typealias.tparams);
            break;
        case NINC: case NDEC:
            NodeFree(n->incdec.expr);
            break;
        case NASM:
            free(n->asmstmt.code);
            break;
        case NAWAIT:
            NodeFree(n->awaitn.expr);
            NodeFree(n->awaitn.timeout);
            break;
        case NSPAWN:
            NodeFree(n->spawnn.expr);
            break;
        case NTRYPROPAGATE:
            NodeFree(n->single.expr);
            break;
        case NSWITCH:
            NodeFree(n->sw.expr);
            for (int i=0;i<n->sw.casecount;i++) {
                NodeFree(n->sw.casevals[i]);
                NodeFree(n->sw.casebodies[i]);
            }
            free(n->sw.casevals);
            free(n->sw.casebodies);
            NodeFree(n->sw.defaultbody);
            break;
        case NENUMDECL:
            free(n->enumdecl.name);
            for (int i=0;i<n->enumdecl.count;i++) {
                free(n->enumdecl.members[i]);
                if (n->enumdecl.membertypes && n->enumdecl.membertypes[i]) {
                    for (int j = 0; j < n->enumdecl.membertypecounts[i]; j++) {
                        free(n->enumdecl.membertypes[i][j]);
                    }
                    free(n->enumdecl.membertypes[i]);
                }
            }
            free(n->enumdecl.members);
            free(n->enumdecl.values);
            free(n->enumdecl.membertypes);
            free(n->enumdecl.membertypecounts);
            break;
        case NYIELD:
            NodeFree(n->yieldstmt.expr);
            break;
        case NMULTIRETURN:
            for (int i=0;i<n->multiret.count;i++) NodeFree(n->multiret.exprs[i]);
            free(n->multiret.exprs);
            break;
        case NSPREAD:
            NodeFree(n->spread.expr);
            break;
        case NDESTRUCTARR:
            if (n->destructarr.names) {
                for (int i = 0; i < n->destructarr.count; i++) {
                    free(n->destructarr.names[i]);
                }
                free(n->destructarr.names);
            }
            NodeFree(n->destructarr.value);
            break;
        case NDESTRUCTOBJ:
            if (n->destructobj.keys) {
                for (int i = 0; i < n->destructobj.count; i++) {
                    free(n->destructobj.keys[i]);
                    free(n->destructobj.aliases[i]);
                }
                free(n->destructobj.keys);
                free(n->destructobj.aliases);
            }
            NodeFree(n->destructobj.value);
            break;
        case NNURSERY:
            if (n->nursery.name) free(n->nursery.name);
            NodeFree(n->nursery.body);
            break;
        case NREGION:
            if (n->region.name) free(n->region.name);
            NodeFree(n->region.body);
            break;
        case NARRPAT:
            for (int i = 0; i < n->arrpat.count; i++) {
                NodeFree(n->arrpat.patterns[i]);
            }
            free(n->arrpat.patterns);
            break;
        case NACTORDECL:
            if (n->actordecl.name) free(n->actordecl.name);
            for (int i = 0; i < n->actordecl.fc; i++) {
                if (n->actordecl.fnames && n->actordecl.fnames[i]) free(n->actordecl.fnames[i]);
                if (n->actordecl.ftypes && n->actordecl.ftypes[i]) free(n->actordecl.ftypes[i]);
                if (n->actordecl.fdefaults && n->actordecl.fdefaults[i]) NodeFree(n->actordecl.fdefaults[i]);
            }
            if (n->actordecl.fnames) free(n->actordecl.fnames);
            if (n->actordecl.ftypes) free(n->actordecl.ftypes);
            if (n->actordecl.fdefaults) free(n->actordecl.fdefaults);
            for (int i = 0; i < n->actordecl.mc; i++) {
                if (n->actordecl.methods && n->actordecl.methods[i]) NodeFree(n->actordecl.methods[i]);
            }
            if (n->actordecl.methods) free(n->actordecl.methods);
            break;
        case NEFFECTDECL:
            if (n->effectdecl.name) free(n->effectdecl.name);
            for (int i = 0; i < n->effectdecl.opcount; i++) {
                if (n->effectdecl.opnames && n->effectdecl.opnames[i]) free(n->effectdecl.opnames[i]);
                if (n->effectdecl.opparamcounts && n->effectdecl.opparamcounts[i] > 0) {
                    for (int j = 0; j < n->effectdecl.opparamcounts[i]; j++) {
                        if (n->effectdecl.opparamnames && n->effectdecl.opparamnames[i] && n->effectdecl.opparamnames[i][j]) free(n->effectdecl.opparamnames[i][j]);
                        if (n->effectdecl.opparamtypes && n->effectdecl.opparamtypes[i] && n->effectdecl.opparamtypes[i][j]) free(n->effectdecl.opparamtypes[i][j]);
                    }
                    if (n->effectdecl.opparamnames && n->effectdecl.opparamnames[i]) free(n->effectdecl.opparamnames[i]);
                    if (n->effectdecl.opparamtypes && n->effectdecl.opparamtypes[i]) free(n->effectdecl.opparamtypes[i]);
                }
            }
            if (n->effectdecl.opnames) free(n->effectdecl.opnames);
            if (n->effectdecl.opparamnames) free(n->effectdecl.opparamnames);
            if (n->effectdecl.opparamtypes) free(n->effectdecl.opparamtypes);
            if (n->effectdecl.opparamcounts) free(n->effectdecl.opparamcounts);
            break;
        case NPERFORM:
            if (n->performn.effectname) free(n->performn.effectname);
            if (n->performn.opname) free(n->performn.opname);
            for (int i = 0; i < n->performn.argcount; i++) {
                NodeFree(n->performn.args[i]);
            }
            if (n->performn.args) free(n->performn.args);
            break;
        case NHANDLE:
            NodeFree(n->handlen.body);
            for (int i = 0; i < n->handlen.count; i++) {
                if (n->handlen.opnames && n->handlen.opnames[i]) free(n->handlen.opnames[i]);
                if (n->handlen.handlers && n->handlen.handlers[i]) NodeFree(n->handlen.handlers[i]);
            }
            if (n->handlen.opnames) free(n->handlen.opnames);
            if (n->handlen.handlers) free(n->handlen.handlers);
            break;
        case NRESUME:
            if (n->resumen.expr) NodeFree(n->resumen.expr);
            break;
        case NGPUFN:
            break;
        case NATOMIC:
            if (n->atomicn.body) NodeFree(n->atomicn.body);
            break;
        default: break;
    }
    free(n);
}

static int copy_depth = 0;
Node *NodeCopyInternal(Node *n);

Node *NodeCopy(Node *n) {
    if (!n) return NULL;
    copy_depth++;
    if (copy_depth > 500) {
        fprintf(stderr, "INFINITE RECURSION in nodecopy: type=%d, line=%d\n", n->type, n->line);
        exit(1);
    }
    Node *res = NodeCopyInternal(n);
    copy_depth--;
    return res;
}

Node *NodeCopyInternal(Node *n) {
    if (!n) return NULL;
    Node *copy = NodeNew(n->type, n->line);
    copy->isstack = n->isstack;
    copy->is_static_safe = n->is_static_safe;
    switch (n->type) {
        case NSTR: case NIDENT:
            copy->sval = n->sval ? strdup(n->sval) : NULL;
            break;
        case NINT: case NBOOL:
            copy->ival = n->ival;
            break;
        case NFLOAT:
            copy->fval = n->fval;
            break;
        case NNULL:
            break;
        case NNEW:
            copy->newobj.name = n->newobj.name ? strdup(n->newobj.name) : NULL;
            copy->newobj.tc = n->newobj.tc;
            if (n->newobj.tc > 0 && n->newobj.targs) {
                copy->newobj.targs = malloc(sizeof(char*) * n->newobj.tc);
                for(int i=0; i<n->newobj.tc; i++) copy->newobj.targs[i] = strdup(n->newobj.targs[i]);
            } else {
                copy->newobj.targs = NULL;
            }
            break;
        case NBINOP: case NUNOP:
            copy->binop.op = n->binop.op ? strdup(n->binop.op) : NULL;
            copy->binop.left = NodeCopy(n->binop.left);
            copy->binop.right = NodeCopy(n->binop.right);
            break;
        case NREF: case NMUTREF:
            copy->refnode.expr = NodeCopy(n->refnode.expr);
            copy->refnode.is_mutable = n->refnode.is_mutable;
            break;
        case NVARDECL: case NCONSTDECL:
            copy->decl.name = n->decl.name ? strdup(n->decl.name) : NULL;
            copy->decl.tname = n->decl.tname ? strdup(n->decl.tname) : NULL;
            copy->decl.init = NodeCopy(n->decl.init);
            copy->decl.isconst = NodeCopy(n->decl.isconst);
            copy->decl.isconstflag = n->decl.isconstflag;
            break;
        case NFNDECL:
        case NMACRODECL:
            copy->fn.name = n->fn.name ? strdup(n->fn.name) : NULL;
            copy->fn.ret = n->fn.ret ? strdup(n->fn.ret) : NULL;
            copy->fn.effect = n->fn.effect ? strdup(n->fn.effect) : NULL;
            copy->fn.pc = n->fn.pc;
            copy->fn.isvariadic = n->fn.isvariadic;
            copy->fn.isasync = n->fn.isasync;
            copy->fn.isgen = n->fn.isgen;
            if (n->fn.pc > 0) {
                copy->fn.pnames = malloc(sizeof(char*) * n->fn.pc);
                copy->fn.ptypes = malloc(sizeof(char*) * n->fn.pc);
                for(int i=0; i<n->fn.pc; i++){
                    copy->fn.pnames[i] = n->fn.pnames[i] ? strdup(n->fn.pnames[i]) : NULL;
                    copy->fn.ptypes[i] = n->fn.ptypes[i] ? strdup(n->fn.ptypes[i]) : NULL;
                }
            } else {
                copy->fn.pnames = NULL;
                copy->fn.ptypes = NULL;
            }
            if (n->fn.pdefaults) {
                copy->fn.pdefaults = malloc(sizeof(Node*) * n->fn.pc);
                for (int i=0; i<n->fn.pc; i++) copy->fn.pdefaults[i] = NodeCopy(n->fn.pdefaults[i]);
            } else {
                copy->fn.pdefaults = NULL;
            }
            copy->fn.req_count = n->fn.req_count;
            if (n->fn.req_count > 0 && n->fn.requires) {
                copy->fn.requires = malloc(sizeof(Node*) * n->fn.req_count);
                for (int i=0; i<n->fn.req_count; i++) copy->fn.requires[i] = NodeCopy(n->fn.requires[i]);
            } else {
                copy->fn.requires = NULL;
            }
            copy->fn.ens_count = n->fn.ens_count;
            if (n->fn.ens_count > 0 && n->fn.ensures) {
                copy->fn.ensures = malloc(sizeof(Node*) * n->fn.ens_count);
                for (int i=0; i<n->fn.ens_count; i++) copy->fn.ensures[i] = NodeCopy(n->fn.ensures[i]);
            } else {
                copy->fn.ensures = NULL;
            }
            copy->fn.body = NodeCopy(n->fn.body);
            copy->fn.tparams = NULL;
            copy->fn.tpc = n->fn.tpc;
            if (n->fn.tpc > 0 && n->fn.tparams) {
                copy->fn.tparams = malloc(sizeof(char*) * n->fn.tpc);
                for (int i=0; i<n->fn.tpc; i++) {
                    copy->fn.tparams[i] = strdup(n->fn.tparams[i]);
                }
            }
            break;
        case NBLOCK: case NPROGRAM:
            copy->block.count = n->block.count;
            if (n->block.count > 0) {
                copy->block.stmts = malloc(sizeof(Node*) * n->block.count);
                for(int i=0; i<n->block.count; i++) copy->block.stmts[i] = NodeCopy(n->block.stmts[i]);
            } else {
                copy->block.stmts = NULL;
            }
            break;
        case NIF:
            copy->ifn.cond = NodeCopy(n->ifn.cond);
            copy->ifn.then = NodeCopy(n->ifn.then);
            copy->ifn.els = NodeCopy(n->ifn.els);
            break;
        case NTERNARY:
            copy->ternary.cond = NodeCopy(n->ternary.cond);
            copy->ternary.tval = NodeCopy(n->ternary.tval);
            copy->ternary.fval = NodeCopy(n->ternary.fval);
            break;
        case NWHILE:
            copy->whilen.cond = NodeCopy(n->whilen.cond);
            copy->whilen.body = NodeCopy(n->whilen.body);
            copy->whilen.inv_count = n->whilen.inv_count;
            if (n->whilen.inv_count > 0 && n->whilen.invariants) {
                copy->whilen.invariants = malloc(sizeof(Node*) * n->whilen.inv_count);
                for (int i=0; i<n->whilen.inv_count; i++) copy->whilen.invariants[i] = NodeCopy(n->whilen.invariants[i]);
            } else {
                copy->whilen.invariants = NULL;
            }
            break;
        case NFOR:
            copy->forn.init = NodeCopy(n->forn.init);
            copy->forn.cond = NodeCopy(n->forn.cond);
            copy->forn.post = NodeCopy(n->forn.post);
            copy->forn.body = NodeCopy(n->forn.body);
            copy->forn.inv_count = n->forn.inv_count;
            if (n->forn.inv_count > 0 && n->forn.invariants) {
                copy->forn.invariants = malloc(sizeof(Node*) * n->forn.inv_count);
                for (int i=0; i<n->forn.inv_count; i++) copy->forn.invariants[i] = NodeCopy(n->forn.invariants[i]);
            } else {
                copy->forn.invariants = NULL;
            }
            break;
        case NFORIN:
            copy->forin.var = n->forin.var ? strdup(n->forin.var) : NULL;
            copy->forin.iter = NodeCopy(n->forin.iter);
            copy->forin.body = NodeCopy(n->forin.body);
            copy->forin.isasync = n->forin.isasync;
            break;
        case NSELECT:
            copy->selectn.count = n->selectn.count;
            if (n->selectn.count > 0) {
                copy->selectn.cases = malloc(sizeof(Node*) * n->selectn.count);
                copy->selectn.bodies = malloc(sizeof(Node*) * n->selectn.count);
                for (int i = 0; i < n->selectn.count; i++) {
                    copy->selectn.cases[i] = NodeCopy(n->selectn.cases[i]);
                    copy->selectn.bodies[i] = NodeCopy(n->selectn.bodies[i]);
                }
            } else {
                copy->selectn.cases = NULL;
                copy->selectn.bodies = NULL;
            }
            copy->selectn.defaultbody = NodeCopy(n->selectn.defaultbody);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NINPUT:
            copy->single.expr = NodeCopy(n->single.expr);
            copy->single.var = n->single.var ? strdup(n->single.var) : NULL;
            break;
        case NCAST:
            copy->cast.expr = NodeCopy(n->cast.expr);
            copy->cast.targettype = n->cast.targettype ? strdup(n->cast.targettype) : NULL;
            break;
        case NMODULE:
            copy->single.var = n->single.var ? strdup(n->single.var) : NULL;
            copy->single.expr = NodeCopy(n->single.expr);
            break;
        case NUSE:
            copy->decl.name = n->decl.name ? strdup(n->decl.name) : NULL;
            copy->decl.tname = n->decl.tname ? strdup(n->decl.tname) : NULL;
            copy->decl.isconstflag = n->decl.isconstflag;
            copy->decl.visibility = n->decl.visibility;
            break;
        case NTRYCATCH:
            copy->trycatch.tryblock = NodeCopy(n->trycatch.tryblock);
            copy->trycatch.errvar = n->trycatch.errvar ? strdup(n->trycatch.errvar) : NULL;
            copy->trycatch.catchblock = NodeCopy(n->trycatch.catchblock);
            copy->trycatch.finallyblock = NodeCopy(n->trycatch.finallyblock);
            break;
        case NTHROW:
            copy->throwstmt.expr = NodeCopy(n->throwstmt.expr);
            break;
        case NCOALESCE:
            copy->coalesce.left = NodeCopy(n->coalesce.left);
            copy->coalesce.right = NodeCopy(n->coalesce.right);
            break;
        case NOPTCHAIN:
            copy->optchain.obj = NodeCopy(n->optchain.obj);
            copy->optchain.field = n->optchain.field ? strdup(n->optchain.field) : NULL;
            copy->optchain.idx = NodeCopy(n->optchain.idx);
            break;
        case NSTRUCTINIT:
            copy->structinit.typename = n->structinit.typename ? strdup(n->structinit.typename) : NULL;
            copy->structinit.spread = n->structinit.spread ? NodeCopy(n->structinit.spread) : NULL;
            copy->structinit.fc = n->structinit.fc;
            if (n->structinit.fc > 0) {
                copy->structinit.fnames = malloc(n->structinit.fc * sizeof(char *));
                copy->structinit.fvals = malloc(n->structinit.fc * sizeof(Node *));
                for (int i = 0; i < n->structinit.fc; i++) {
                    copy->structinit.fnames[i] = strdup(n->structinit.fnames[i]);
                    copy->structinit.fvals[i] = NodeCopy(n->structinit.fvals[i]);
                }
            } else {
                copy->structinit.fnames = NULL;
                copy->structinit.fvals = NULL;
            }
            break;
        case NCHAINEDCMP:
            copy->chainedcmp.count = n->chainedcmp.count;
            copy->chainedcmp.exprs = malloc(n->chainedcmp.count * sizeof(Node *));
            for (int i = 0; i < n->chainedcmp.count; i++) {
                copy->chainedcmp.exprs[i] = NodeCopy(n->chainedcmp.exprs[i]);
            }
            copy->chainedcmp.ops = malloc((n->chainedcmp.count - 1) * sizeof(char *));
            for (int i = 0; i < n->chainedcmp.count - 1; i++) {
                copy->chainedcmp.ops[i] = strdup(n->chainedcmp.ops[i]);
            }
            break;
        case NRANGE:
            copy->range.start = NodeCopy(n->range.start);
            copy->range.end = NodeCopy(n->range.end);
            break;
        case NASSIGN:
            copy->assign.target = NodeCopy(n->assign.target);
            copy->assign.value = NodeCopy(n->assign.value);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            copy->call.callee = NodeCopy(n->call.callee);
            copy->call.argc = n->call.argc;
            if (n->call.argc > 0) {
                copy->call.args = malloc(sizeof(Node*) * n->call.argc);
                copy->call.isspread = malloc(sizeof(int) * n->call.argc);
                for(int i=0; i<n->call.argc; i++) {
                    copy->call.args[i] = NodeCopy(n->call.args[i]);
                    copy->call.isspread[i] = n->call.isspread ? n->call.isspread[i] : 0;
                }
            } else {
                copy->call.args = NULL;
                copy->call.isspread = NULL;
            }
            copy->call.tc = n->call.tc;
            if (n->call.tc > 0 && n->call.targs) {
                copy->call.targs = malloc(sizeof(char*) * n->call.tc);
                for (int i=0; i<n->call.tc; i++) {
                    copy->call.targs[i] = strdup(n->call.targs[i]);
                }
            } else {
                copy->call.targs = NULL;
            }
            break;
        case NINDEX:
            copy->index.arr = NodeCopy(n->index.arr);
            copy->index.idx = NodeCopy(n->index.idx);
            break;
        case NMEMBER:
            copy->member.obj = NodeCopy(n->member.obj);
            copy->member.field = n->member.field ? strdup(n->member.field) : NULL;
            break;
        case NARRLIT:
            copy->arrlit.count = n->arrlit.count;
            if (n->arrlit.count > 0) {
                copy->arrlit.elems = malloc(sizeof(Node*) * n->arrlit.count);
                copy->arrlit.isspread = malloc(sizeof(int) * n->arrlit.count);
                for(int i=0; i<n->arrlit.count; i++) {
                    copy->arrlit.elems[i] = NodeCopy(n->arrlit.elems[i]);
                    copy->arrlit.isspread[i] = n->arrlit.isspread ? n->arrlit.isspread[i] : 0;
                }
            } else {
                copy->arrlit.elems = NULL;
                copy->arrlit.isspread = NULL;
            }
            break;
        case NMAPLIT:
            copy->maplit.count = n->maplit.count;
            if (n->maplit.count > 0) {
                copy->maplit.keys = malloc(sizeof(char*) * n->maplit.count);
                copy->maplit.vals = malloc(sizeof(Node*) * n->maplit.count);
                for(int i=0; i<n->maplit.count; i++) {
                    copy->maplit.keys[i] = n->maplit.keys[i] ? strdup(n->maplit.keys[i]) : NULL;
                    copy->maplit.vals[i] = NodeCopy(n->maplit.vals[i]);
                }
            } else {
                copy->maplit.keys = NULL;
                copy->maplit.vals = NULL;
            }
            break;
        case NSTRUCTDECL:
            copy->strct.name = n->strct.name ? strdup(n->strct.name) : NULL;
            copy->strct.fc = n->strct.fc;
            if (n->strct.fc > 0) {
                copy->strct.fnames = malloc(sizeof(char*) * n->strct.fc);
                copy->strct.ftypes = malloc(sizeof(char*) * n->strct.fc);
                for(int i=0; i<n->strct.fc; i++){
                    copy->strct.fnames[i] = n->strct.fnames[i] ? strdup(n->strct.fnames[i]) : NULL;
                    copy->strct.ftypes[i] = n->strct.ftypes[i] ? strdup(n->strct.ftypes[i]) : NULL;
                }
            } else {
                copy->strct.fnames = NULL;
                copy->strct.ftypes = NULL;
            }
            copy->strct.ic = n->strct.ic;
            if (n->strct.ic > 0 && n->strct.interfaces) {
                copy->strct.interfaces = malloc(sizeof(char*) * n->strct.ic);
                for(int i=0; i<n->strct.ic; i++) copy->strct.interfaces[i] = strdup(n->strct.interfaces[i]);
            } else {
                copy->strct.interfaces = NULL;
            }
            copy->strct.tpc = n->strct.tpc;
            if (n->strct.tpc > 0 && n->strct.tparams) {
                copy->strct.tparams = malloc(sizeof(char*) * n->strct.tpc);
                for(int i=0; i<n->strct.tpc; i++) copy->strct.tparams[i] = strdup(n->strct.tparams[i]);
            } else {
                copy->strct.tparams = NULL;
            }
            copy->strct.constraintcount = n->strct.constraintcount;
            if (n->strct.constraintcount > 0) {
                copy->strct.constraintparams = malloc(sizeof(char*) * n->strct.constraintcount);
                copy->strct.constrainttypes = malloc(sizeof(char*) * n->strct.constraintcount);
                for (int i = 0; i < n->strct.constraintcount; i++) {
                    copy->strct.constraintparams[i] = strdup(n->strct.constraintparams[i]);
                    copy->strct.constrainttypes[i] = strdup(n->strct.constrainttypes[i]);
                }
            } else {
                copy->strct.constraintparams = NULL;
                copy->strct.constrainttypes = NULL;
            }
            break;
        case NMATCH:
            copy->match.expr = NodeCopy(n->match.expr);
            copy->match.count = n->match.count;
            if (n->match.count > 0) {
                copy->match.patterns = malloc(sizeof(Node*) * n->match.count);
                copy->match.bodies = malloc(sizeof(Node*) * n->match.count);
                copy->match.guards = malloc(sizeof(Node*) * n->match.count);
                for(int i=0; i<n->match.count; i++) {
                    copy->match.patterns[i] = NodeCopy(n->match.patterns[i]);
                    copy->match.bodies[i] = NodeCopy(n->match.bodies[i]);
                    copy->match.guards[i] = n->match.guards[i] ? NodeCopy(n->match.guards[i]) : NULL;
                }
            } else {
                copy->match.patterns = NULL;
                copy->match.bodies = NULL;
                copy->match.guards = NULL;
            }
            break;
        case NTUPLE:
            copy->tuple.count = n->tuple.count;
            if (n->tuple.count > 0) {
                copy->tuple.elems = malloc(sizeof(Node*) * n->tuple.count);
                for (int i = 0; i < n->tuple.count; i++) {
                    copy->tuple.elems[i] = NodeCopy(n->tuple.elems[i]);
                }
            } else {
                copy->tuple.elems = NULL;
            }
            break;
        case NTUPLEPAT:
            copy->tuplepat.count = n->tuplepat.count;
            if (n->tuplepat.count > 0) {
                copy->tuplepat.patterns = malloc(sizeof(Node*) * n->tuplepat.count);
                for (int i = 0; i < n->tuplepat.count; i++) {
                    copy->tuplepat.patterns[i] = NodeCopy(n->tuplepat.patterns[i]);
                }
            } else {
                copy->tuplepat.patterns = NULL;
            }
            break;
        case NSTRUCTPAT:
            copy->structpat.name = n->structpat.name ? strdup(n->structpat.name) : NULL;
            copy->structpat.count = n->structpat.count;
            if (n->structpat.count > 0) {
                copy->structpat.fields = malloc(sizeof(char*) * n->structpat.count);
                copy->structpat.patterns = malloc(sizeof(Node*) * n->structpat.count);
                for (int i = 0; i < n->structpat.count; i++) {
                    copy->structpat.fields[i] = strdup(n->structpat.fields[i]);
                    copy->structpat.patterns[i] = NodeCopy(n->structpat.patterns[i]);
                }
            } else {
                copy->structpat.fields = NULL;
                copy->structpat.patterns = NULL;
            }
            break;
        case NORPAT:
            copy->orpat.left = NodeCopy(n->orpat.left);
            copy->orpat.right = NodeCopy(n->orpat.right);
            break;
        case NINTERFACEDECL:
            copy->interfacedecl.name = n->interfacedecl.name ? strdup(n->interfacedecl.name) : NULL;
            copy->interfacedecl.count = n->interfacedecl.count;
            if (n->interfacedecl.count > 0) {
                copy->interfacedecl.fnames = malloc(sizeof(char*) * n->interfacedecl.count);
                copy->interfacedecl.rettypes = malloc(sizeof(char*) * n->interfacedecl.count);
                copy->interfacedecl.paramtypes = malloc(sizeof(char**) * n->interfacedecl.count);
                copy->interfacedecl.paramnames = malloc(sizeof(char**) * n->interfacedecl.count);
                copy->interfacedecl.paramcounts = malloc(sizeof(int) * n->interfacedecl.count);
                if (n->interfacedecl.hasdefault) {
                    copy->interfacedecl.hasdefault = malloc(sizeof(int) * n->interfacedecl.count);
                    copy->interfacedecl.defaultbodies = malloc(sizeof(Node*) * n->interfacedecl.count);
                } else {
                    copy->interfacedecl.hasdefault = NULL;
                    copy->interfacedecl.defaultbodies = NULL;
                }
                for(int i=0; i<n->interfacedecl.count; i++) {
                    copy->interfacedecl.fnames[i] = n->interfacedecl.fnames[i] ? strdup(n->interfacedecl.fnames[i]) : NULL;
                    copy->interfacedecl.rettypes[i] = n->interfacedecl.rettypes[i] ? strdup(n->interfacedecl.rettypes[i]) : NULL;
                    copy->interfacedecl.paramcounts[i] = n->interfacedecl.paramcounts[i];
                    if (n->interfacedecl.hasdefault) {
                        copy->interfacedecl.hasdefault[i] = n->interfacedecl.hasdefault[i];
                        copy->interfacedecl.defaultbodies[i] = NodeCopy(n->interfacedecl.defaultbodies[i]);
                    }
                    int pc = n->interfacedecl.paramcounts[i];
                    if (pc > 0) {
                        copy->interfacedecl.paramtypes[i] = malloc(sizeof(char*) * pc);
                        copy->interfacedecl.paramnames[i] = malloc(sizeof(char*) * pc);
                        for(int j=0; j<pc; j++) {
                            copy->interfacedecl.paramtypes[i][j] = strdup(n->interfacedecl.paramtypes[i][j]);
                            copy->interfacedecl.paramnames[i][j] = strdup(n->interfacedecl.paramnames[i][j]);
                        }
                    } else {
                        copy->interfacedecl.paramtypes[i] = NULL;
                        copy->interfacedecl.paramnames[i] = NULL;
                    }
                }
            } else {
                copy->interfacedecl.fnames = NULL;
                copy->interfacedecl.rettypes = NULL;
                copy->interfacedecl.paramtypes = NULL;
                copy->interfacedecl.paramnames = NULL;
                copy->interfacedecl.paramcounts = NULL;
                copy->interfacedecl.hasdefault = NULL;
                copy->interfacedecl.defaultbodies = NULL;
            }
            copy->interfacedecl.assoccount = n->interfacedecl.assoccount;
            if (n->interfacedecl.assoccount > 0) {
                copy->interfacedecl.assoctypes = malloc(sizeof(char*) * n->interfacedecl.assoccount);
                for (int i = 0; i < n->interfacedecl.assoccount; i++) {
                    copy->interfacedecl.assoctypes[i] = strdup(n->interfacedecl.assoctypes[i]);
                }
            } else {
                copy->interfacedecl.assoctypes = NULL;
            }
            break;
        case NTYPEALIAS:
            copy->typealias.name = n->typealias.name ? strdup(n->typealias.name) : NULL;
            copy->typealias.target = n->typealias.target ? strdup(n->typealias.target) : NULL;
            copy->typealias.isopaque = n->typealias.isopaque;
            copy->typealias.tpc = n->typealias.tpc;
            if (n->typealias.tpc > 0) {
                copy->typealias.tparams = malloc(n->typealias.tpc * sizeof(char *));
                for (int i = 0; i < n->typealias.tpc; i++) {
                    copy->typealias.tparams[i] = strdup(n->typealias.tparams[i]);
                }
            } else {
                copy->typealias.tparams = NULL;
            }
            break;
        case NINC: case NDEC:
            copy->incdec.expr = NodeCopy(n->incdec.expr);
            break;
        case NAWAIT:
            copy->awaitn.expr = NodeCopy(n->awaitn.expr);
            copy->awaitn.timeout = NodeCopy(n->awaitn.timeout);
            break;
        case NSPAWN:
            copy->spawnn.expr = NodeCopy(n->spawnn.expr);
            break;
        case NTRYPROPAGATE:
            copy->single.expr = NodeCopy(n->single.expr);
            break;
        case NSWITCH:
            copy->sw.expr = NodeCopy(n->sw.expr);
            copy->sw.casecount = n->sw.casecount;
            if (n->sw.casecount > 0) {
                copy->sw.casevals = malloc(sizeof(Node*) * n->sw.casecount);
                copy->sw.casebodies = malloc(sizeof(Node*) * n->sw.casecount);
                for(int i=0; i<n->sw.casecount; i++) {
                    copy->sw.casevals[i] = NodeCopy(n->sw.casevals[i]);
                    copy->sw.casebodies[i] = NodeCopy(n->sw.casebodies[i]);
                }
            } else {
                copy->sw.casevals = NULL;
                copy->sw.casebodies = NULL;
            }
            copy->sw.defaultbody = NodeCopy(n->sw.defaultbody);
            break;
        case NENUMDECL:
            copy->enumdecl.name = n->enumdecl.name ? strdup(n->enumdecl.name) : NULL;
            copy->enumdecl.count = n->enumdecl.count;
            copy->enumdecl.isadt = n->enumdecl.isadt;
            if (n->enumdecl.count > 0) {
                copy->enumdecl.members = malloc(sizeof(char*) * n->enumdecl.count);
                copy->enumdecl.values = n->enumdecl.values ? malloc(sizeof(int64_t) * n->enumdecl.count) : NULL;
                copy->enumdecl.membertypes = calloc(n->enumdecl.count, sizeof(char**));
                copy->enumdecl.membertypecounts = calloc(n->enumdecl.count, sizeof(int));
                for(int i=0; i<n->enumdecl.count; i++) {
                    copy->enumdecl.members[i] = n->enumdecl.members[i] ? strdup(n->enumdecl.members[i]) : NULL;
                    if (copy->enumdecl.values) copy->enumdecl.values[i] = n->enumdecl.values[i];
                    copy->enumdecl.membertypecounts[i] = n->enumdecl.membertypecounts[i];
                    if (n->enumdecl.membertypecounts[i] > 0 && n->enumdecl.membertypes[i]) {
                        copy->enumdecl.membertypes[i] = malloc(sizeof(char*) * n->enumdecl.membertypecounts[i]);
                        for (int j = 0; j < n->enumdecl.membertypecounts[i]; j++) {
                            copy->enumdecl.membertypes[i][j] = strdup(n->enumdecl.membertypes[i][j]);
                        }
                    }
                }
            } else {
                copy->enumdecl.members = NULL;
                copy->enumdecl.values = NULL;
                copy->enumdecl.membertypes = NULL;
                copy->enumdecl.membertypecounts = NULL;
            }
            break;
        case NYIELD:
            copy->yieldstmt.expr = NodeCopy(n->yieldstmt.expr);
            break;
        case NMULTIRETURN:
            copy->multiret.count = n->multiret.count;
            if (n->multiret.count > 0) {
                copy->multiret.exprs = malloc(sizeof(Node*) * n->multiret.count);
                for (int i=0; i<n->multiret.count; i++) copy->multiret.exprs[i] = NodeCopy(n->multiret.exprs[i]);
            } else {
                copy->multiret.exprs = NULL;
            }
            break;
        case NSPREAD:
            copy->spread.expr = NodeCopy(n->spread.expr);
            break;
        case NDESTRUCTARR:
            copy->destructarr.count = n->destructarr.count;
            if (n->destructarr.count > 0 && n->destructarr.names) {
                copy->destructarr.names = malloc(sizeof(char*) * n->destructarr.count);
                for (int i = 0; i < n->destructarr.count; i++) {
                    copy->destructarr.names[i] = n->destructarr.names[i] ? strdup(n->destructarr.names[i]) : NULL;
                }
            } else {
                copy->destructarr.names = NULL;
            }
            copy->destructarr.value = NodeCopy(n->destructarr.value);
            break;
        case NDESTRUCTOBJ:
            copy->destructobj.count = n->destructobj.count;
            if (n->destructobj.count > 0 && n->destructobj.keys) {
                copy->destructobj.keys = malloc(sizeof(char*) * n->destructobj.count);
                copy->destructobj.aliases = malloc(sizeof(char*) * n->destructobj.count);
                for (int i = 0; i < n->destructobj.count; i++) {
                    copy->destructobj.keys[i] = n->destructobj.keys[i] ? strdup(n->destructobj.keys[i]) : NULL;
                    copy->destructobj.aliases[i] = n->destructobj.aliases[i] ? strdup(n->destructobj.aliases[i]) : NULL;
                }
            } else {
                copy->destructobj.keys = NULL;
                copy->destructobj.aliases = NULL;
            }
            copy->destructobj.value = NodeCopy(n->destructobj.value);
            break;
        case NNURSERY:
            copy->nursery.name = n->nursery.name ? strdup(n->nursery.name) : NULL;
            copy->nursery.body = NodeCopy(n->nursery.body);
            break;
        case NREGION:
            copy->region.name = n->region.name ? strdup(n->region.name) : NULL;
            copy->region.body = NodeCopy(n->region.body);
            break;
        case NARRPAT:
            copy->arrpat.count = n->arrpat.count;
            copy->arrpat.hasrest = n->arrpat.hasrest;
            copy->arrpat.restidx = n->arrpat.restidx;
            if (n->arrpat.count > 0) {
                copy->arrpat.patterns = malloc(sizeof(Node*) * n->arrpat.count);
                for (int i = 0; i < n->arrpat.count; i++) {
                    copy->arrpat.patterns[i] = NodeCopy(n->arrpat.patterns[i]);
                }
            } else {
                copy->arrpat.patterns = NULL;
            }
            break;
        case NACTORDECL:
            copy->actordecl.name = n->actordecl.name ? strdup(n->actordecl.name) : NULL;
            copy->actordecl.fc = n->actordecl.fc;
            if (n->actordecl.fc > 0) {
                copy->actordecl.fnames = malloc(sizeof(char*) * n->actordecl.fc);
                copy->actordecl.ftypes = malloc(sizeof(char*) * n->actordecl.fc);
                copy->actordecl.fdefaults = malloc(sizeof(Node*) * n->actordecl.fc);
                for (int i = 0; i < n->actordecl.fc; i++) {
                    copy->actordecl.fnames[i] = n->actordecl.fnames[i] ? strdup(n->actordecl.fnames[i]) : NULL;
                    copy->actordecl.ftypes[i] = n->actordecl.ftypes[i] ? strdup(n->actordecl.ftypes[i]) : NULL;
                    copy->actordecl.fdefaults[i] = n->actordecl.fdefaults[i] ? NodeCopy(n->actordecl.fdefaults[i]) : NULL;
                }
            } else {
                copy->actordecl.fnames = NULL;
                copy->actordecl.ftypes = NULL;
                copy->actordecl.fdefaults = NULL;
            }
            copy->actordecl.mc = n->actordecl.mc;
            if (n->actordecl.mc > 0) {
                copy->actordecl.methods = malloc(sizeof(Node*) * n->actordecl.mc);
                for (int i = 0; i < n->actordecl.mc; i++) {
                    copy->actordecl.methods[i] = NodeCopy(n->actordecl.methods[i]);
                }
            } else {
                copy->actordecl.methods = NULL;
            }
            copy->actordecl.visibility = n->actordecl.visibility;
            break;
        default:
            break;
    }
    return copy;
}

static int IsPowerOfTwo(int64_t val, int *shift) {
    if (val <= 0) return 0;
    if ((val & (val - 1)) != 0) return 0;
    int s = 0;
    while (val > 1) {
        val >>= 1;
        s++;
    }
    *shift = s;
    return 1;
}

static int NodesEqual(Node *a, Node *b) {
    if (!a || !b) return a == b;
    if (a->type != b->type) return 0;
    switch (a->type) {
        case NIDENT: return a->sval && b->sval && strcmp(a->sval, b->sval) == 0;
        case NINT: return a->ival == b->ival;
        case NFLOAT: return a->fval == b->fval;
        case NBOOL: return a->sval && b->sval && strcmp(a->sval, b->sval) == 0;
        case NNULL: return 1;
        case NSTR: return a->sval && b->sval && strcmp(a->sval, b->sval) == 0;
        case NUNOP:
            return a->binop.op && b->binop.op && strcmp(a->binop.op, b->binop.op) == 0 &&
                   NodesEqual(a->binop.left, b->binop.left);
        case NBINOP:
            return a->binop.op && b->binop.op && strcmp(a->binop.op, b->binop.op) == 0 &&
                   NodesEqual(a->binop.left, b->binop.left) &&
                   NodesEqual(a->binop.right, b->binop.right);
        default: return 0;
    }
}

static int IsPureNode(const Node *n) {
    if (!n) return 1;
    switch (n->type) {
        case NINT:
        case NFLOAT:
        case NBOOL:
        case NSTR:
        case NNULL:
        case NIDENT:
            return 1;
        case NBINOP:
            if ((strcmp(n->binop.op, "/") == 0 || strcmp(n->binop.op, "%") == 0) &&
                n->binop.right && n->binop.right->type == NINT && n->binop.right->ival == 0) {
                return 0;
            }
            return IsPureNode(n->binop.left) && IsPureNode(n->binop.right);
        case NUNOP:
            return IsPureNode(n->binop.left);
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) {
                if (!IsPureNode(n->tuple.elems[i])) return 0;
            }
            return 1;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                if (!IsPureNode(n->arrlit.elems[i])) return 0;
            }
            return 1;
        case NCAST:
            return IsPureNode(n->cast.expr);
        case NREF:
        case NMUTREF:
            return IsPureNode(n->refnode.expr);
        default:
            return 0;
    }
}

static int StmtModIfiesVar(Node *stmt, const char *varname) {
    if (!stmt) return 0;
    if (stmt->type == NASSIGN) {
        Node *target = stmt->assign.target;
        if (target->type == NIDENT && strcmp(target->sval, varname) == 0) {
            return 1;
        }
    }
    if (stmt->type == NINC || stmt->type == NDEC) {
        Node *target = stmt->incdec.expr;
        if (target->type == NIDENT && strcmp(target->sval, varname) == 0) {
            return 1;
        }
    }
    if (stmt->type == NVARDECL || stmt->type == NCONSTDECL) {
        if (strcmp(stmt->decl.name, varname) == 0) {
            return 1;
        }
    }
    if (stmt->type == NBLOCK || stmt->type == NPROGRAM) {
        for (int i = 0; i < stmt->block.count; i++) {
            if (StmtModIfiesVar(stmt->block.stmts[i], varname)) return 1;
        }
    }
    if (stmt->type == NIF) {
        return StmtModIfiesVar(stmt->ifn.then, varname) ||
               (stmt->ifn.els && StmtModIfiesVar(stmt->ifn.els, varname));
    }
    if (stmt->type == NWHILE) {
        return StmtModIfiesVar(stmt->whilen.body, varname);
    }
    if (stmt->type == NFOR) {
        return (stmt->forn.init && StmtModIfiesVar(stmt->forn.init, varname)) ||
               (stmt->forn.post && StmtModIfiesVar(stmt->forn.post, varname)) ||
               StmtModIfiesVar(stmt->forn.body, varname);
    }
    return 0;
}

static int ExprInvalidatedByStmt(Node *expr, Node *stmt) {
    if (!expr || !stmt) return 0;
    if (expr->type == NIDENT) {
        return StmtModIfiesVar(stmt, expr->sval);
    }
    if (expr->type == NBINOP) {
        return ExprInvalidatedByStmt(expr->binop.left, stmt) ||
               ExprInvalidatedByStmt(expr->binop.right, stmt);
    }
    return 0;
}

static int gcsecounter = 0;

Node *NodeCseOpt(Node *n) {
    if (!n) return NULL;
    switch (n->type) {
        case NFNDECL:
            if (!n->fn.isgen && !n->fn.isasync)
                n->fn.body = NodeCseOpt(n->fn.body);
            break;
        case NIF:
            n->ifn.then = NodeCseOpt(n->ifn.then);
            if (n->ifn.els) n->ifn.els = NodeCseOpt(n->ifn.els);
            break;
        case NTERNARY:
            n->ternary.cond = NodeCseOpt(n->ternary.cond);
            n->ternary.tval = NodeCseOpt(n->ternary.tval);
            n->ternary.fval = NodeCseOpt(n->ternary.fval);
            break;
        case NWHILE:
            n->whilen.body = NodeCseOpt(n->whilen.body);
            break;
        case NFOR:
            n->forn.body = NodeCseOpt(n->forn.body);
            break;
        case NTRYCATCH:
            n->trycatch.tryblock = NodeCseOpt(n->trycatch.tryblock);
            n->trycatch.catchblock = NodeCseOpt(n->trycatch.catchblock);
            break;
        case NBLOCK:
        case NPROGRAM: {
            for (int i = 0; i < n->block.count; i++) {
                n->block.stmts[i] = NodeCseOpt(n->block.stmts[i]);
            }
            for (int i = 0; i < n->block.count; i++) {
                Node *stmt1 = n->block.stmts[i];
                if (!stmt1) continue;
                Node *expr1 = NULL;
                if (stmt1->type == NVARDECL || stmt1->type == NCONSTDECL) {
                    expr1 = stmt1->decl.init;
                } else if (stmt1->type == NASSIGN) {
                    expr1 = stmt1->assign.value;
                } else if (stmt1->type == NEXPRSTMT) {
                    expr1 = stmt1->single.expr;
                }
                if (expr1 && expr1->type == NBINOP) {
                    Node *left = expr1->binop.left;
                    Node *right = expr1->binop.right;
                    if ((left->type == NIDENT || left->type == NINT || left->type == NFLOAT || left->type == NBOOL) &&
                        (right->type == NIDENT || right->type == NINT || right->type == NFLOAT || right->type == NBOOL)) {
                        int dupindex = -1;
                        for (int j = i + 1; j < n->block.count; j++) {
                            Node *stmt2 = n->block.stmts[j];
                            if (!stmt2) continue;
                            if (ExprInvalidatedByStmt(expr1, stmt2)) {
                                break;
                            }
                            Node *expr2 = NULL;
                            if (stmt2->type == NVARDECL || stmt2->type == NCONSTDECL) {
                                expr2 = stmt2->decl.init;
                            } else if (stmt2->type == NASSIGN) {
                                expr2 = stmt2->assign.value;
                            } else if (stmt2->type == NEXPRSTMT) {
                                expr2 = stmt2->single.expr;
                            }
                            if (expr2 && NodesEqual(expr2, expr1)) {
                                dupindex = j;
                                break;
                            }
                        }
                        if (dupindex != -1) {
                            char tmpname[64];
                            snprintf(tmpname, sizeof(tmpname), "csetmp%d", gcsecounter++);
                            Node *decl = NodeNew(NVARDECL, expr1->line);
                            decl->decl.name = strdup(tmpname);
                            decl->decl.init = expr1;
                            decl->decl.isconst = NULL;
                            decl->decl.isconstflag = 0;
                            decl->decl.tname = strdup("any");
                            Node *repl1 = NodeNew(NIDENT, expr1->line);
                            repl1->sval = strdup(tmpname);
                            if (stmt1->type == NVARDECL || stmt1->type == NCONSTDECL) {
                                stmt1->decl.init = repl1;
                            } else if (stmt1->type == NASSIGN) {
                                stmt1->assign.value = repl1;
                            } else if (stmt1->type == NEXPRSTMT) {
                                stmt1->single.expr = repl1;
                            }
                            Node *repl2 = NodeNew(NIDENT, expr1->line);
                            repl2->sval = strdup(tmpname);
                            Node *stmt2 = n->block.stmts[dupindex];
                            if (stmt2->type == NVARDECL || stmt2->type == NCONSTDECL) {
                                NodeFree(stmt2->decl.init);
                                stmt2->decl.init = repl2;
                            } else if (stmt2->type == NASSIGN) {
                                NodeFree(stmt2->assign.value);
                                stmt2->assign.value = repl2;
                            } else if (stmt2->type == NEXPRSTMT) {
                                NodeFree(stmt2->single.expr);
                                stmt2->single.expr = repl2;
                            }
                            n->block.stmts = realloc(n->block.stmts, (n->block.count + 1) * sizeof(Node*));
                            for (int k = n->block.count; k > i; k--) {
                                n->block.stmts[k] = n->block.stmts[k - 1];
                            }
                            n->block.stmts[i] = decl;
                            n->block.count++;
                            i++;
                        }
                    }
                }
            }
            break;
        }
        default: break;
    }
    return n;
}

Node *NodeOpt(Node *n) {
    if (!n) return NULL;


    switch (n->type) {
        case NBINOP: {
            n->binop.left = NodeOpt(n->binop.left);
            n->binop.right = NodeOpt(n->binop.right);
            if (!n->binop.left || !n->binop.right) return n;

            char *op = n->binop.op;
            if (strcmp(op, "&&") == 0) {
                if (n->binop.left->type == NBOOL) {
                    if (strcmp(n->binop.left->sval, "false") == 0) {
                        Node *res = NodeNew(NBOOL, n->line);
                        res->sval = strdup("false");
                        NodeFree(n);
                        return res;
                    } else if (strcmp(n->binop.left->sval, "true") == 0) {
                        Node *res = n->binop.right;
                        n->binop.right = NULL;
                        NodeFree(n);
                        return res;
                    }
                }
            } else if (strcmp(op, "||") == 0) {
                if (n->binop.left->type == NBOOL) {
                    if (strcmp(n->binop.left->sval, "true") == 0) {
                        Node *res = NodeNew(NBOOL, n->line);
                        res->sval = strdup("true");
                        NodeFree(n);
                        return res;
                    } else if (strcmp(n->binop.left->sval, "false") == 0) {
                        Node *res = n->binop.right;
                        n->binop.right = NULL;
                        NodeFree(n);
                        return res;
                    }
                }
            } else if (strcmp(op, "+") == 0 && n->binop.left->type == NSTR && n->binop.right->type == NSTR) {
                size_t len1 = strlen(n->binop.left->sval);
                size_t len2 = strlen(n->binop.right->sval);
                char *newstr = malloc(len1 + len2 + 1);
                strcpy(newstr, n->binop.left->sval);
                strcat(newstr, n->binop.right->sval);
                Node *res = NodeNew(NSTR, n->line);
                res->sval = newstr;
                NodeFree(n);
                return res;
            }

            if (n->binop.left->type == NINT && n->binop.right->type == NINT) {
                int64_t l = n->binop.left->ival;
                int64_t r = n->binop.right->ival;
                int64_t val = 0;
                int folded = 0;
                char *op = n->binop.op;
                
                if (strcmp(op, "+") == 0) { val = l + r; folded = 1; }
                else if (strcmp(op, "-") == 0) { val = l - r; folded = 1; }
                else if (strcmp(op, "*") == 0) { val = l * r; folded = 1; }
                else if (strcmp(op, "/") == 0) { if (r != 0 && !(l == INT48_MIN && r == -1)) { val = l / r; folded = 1; } }
                else if (strcmp(op, "%") == 0) { if (r != 0 && !(l == INT48_MIN && r == -1)) { val = l % r; folded = 1; } }
                else if (strcmp(op, "&") == 0) { val = l & r; folded = 1; }
                else if (strcmp(op, "|") == 0) { val = l | r; folded = 1; }
                else if (strcmp(op, "^") == 0) { val = l ^ r; folded = 1; }
                else if (strcmp(op, "<<") == 0) { if (r >= 0 && r < 64) { val = l << r; folded = 1; } }
                else if (strcmp(op, ">>") == 0) { if (r >= 0 && r < 64) { val = l >> r; folded = 1; } }
                else if (strcmp(op, "<") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l < r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, ">") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l > r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, "<=") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l <= r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, ">=") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l >= r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, "==") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l == r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, "!=") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l != r ? "true" : "false");
                    NodeFree(n);
                    return res;
                }

                if (folded) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = val;
                    NodeFree(n);
                    return res;
                }
            }
            else if ((n->binop.left->type == NFLOAT || n->binop.left->type == NINT) &&
                     (n->binop.right->type == NFLOAT || n->binop.right->type == NINT) &&
                     (n->binop.left->type == NFLOAT || n->binop.right->type == NFLOAT)) {
                double l = n->binop.left->type == NFLOAT ? n->binop.left->fval : (double)n->binop.left->ival;
                double r = n->binop.right->type == NFLOAT ? n->binop.right->fval : (double)n->binop.right->ival;
                double val = 0.0;
                int folded = 0;
                char *op = n->binop.op;

                if (strcmp(op, "+") == 0) { val = l + r; folded = 1; }
                else if (strcmp(op, "-") == 0) { val = l - r; folded = 1; }
                else if (strcmp(op, "*") == 0) { val = l * r; folded = 1; }
                else if (strcmp(op, "/") == 0) { if (r != 0.0) { val = l / r; folded = 1; } }
                else if (strcmp(op, "<") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l < r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, ">") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l > r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, "<=") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l <= r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, ">=") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l >= r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, "==") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l == r ? "true" : "false");
                    NodeFree(n);
                    return res;
                } else if (strcmp(op, "!=") == 0) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup(l != r ? "true" : "false");
                    NodeFree(n);
                    return res;
                }

                if (folded) {
                    Node *res = NodeNew(NFLOAT, n->line);
                    res->fval = val;
                    NodeFree(n);
                    return res;
                }
            }

            /* Algebraic identity & zero elimination */
            if (strcmp(n->binop.op, "+") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 0 && n->binop.left->type != NSTR) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 0 && n->binop.right->type != NSTR) {
                    Node *res = n->binop.right;
                    n->binop.right = NULL;
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, "-") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 0) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                } else if (NodesEqual(n->binop.left, n->binop.right) && IsPureNode(n->binop.left)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, "*") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 1) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 1) {
                    Node *res = n->binop.right;
                    n->binop.right = NULL;
                    NodeFree(n);
                    return res;
                } else if (n->binop.right->type == NINT && n->binop.right->ival == 0 && IsPureNode(n->binop.left)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 0 && IsPureNode(n->binop.right)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                }
                int shift = 0;
                if (n->binop.right->type == NINT && n->binop.right->ival > 1 && IsPowerOfTwo(n->binop.right->ival, &shift)) {
                    NodeFree(n->binop.right);
                    Node *shiftnode = NodeNew(NINT, n->line);
                    shiftnode->ival = shift;
                    n->binop.right = shiftnode;
                    free(n->binop.op);
                    n->binop.op = strdup("<<");
                } else if (n->binop.left->type == NINT && n->binop.left->ival > 1 && IsPowerOfTwo(n->binop.left->ival, &shift)) {
                    Node *varnode = n->binop.right;
                    NodeFree(n->binop.left);
                    Node *shiftnode = NodeNew(NINT, n->line);
                    shiftnode->ival = shift;
                    n->binop.left = varnode;
                    n->binop.right = shiftnode;
                    free(n->binop.op);
                    n->binop.op = strdup("<<");
                }
            } else if (strcmp(n->binop.op, "/") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 1) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                }
                int shift = 0;
                if (n->binop.right->type == NINT && n->binop.right->ival > 1 && IsPowerOfTwo(n->binop.right->ival, &shift)) {
                    NodeFree(n->binop.right);
                    Node *shiftnode = NodeNew(NINT, n->line);
                    shiftnode->ival = shift;
                    n->binop.right = shiftnode;
                    free(n->binop.op);
                    n->binop.op = strdup(">>");
                }
            } else if (strcmp(n->binop.op, "%") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 1 && IsPureNode(n->binop.left)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                }
                int shift = 0;
                if (n->binop.right->type == NINT && n->binop.right->ival > 1 && IsPowerOfTwo(n->binop.right->ival, &shift)) {
                    int64_t mask = n->binop.right->ival - 1;
                    NodeFree(n->binop.right);
                    Node *masknode = NodeNew(NINT, n->line);
                    masknode->ival = mask;
                    n->binop.right = masknode;
                    free(n->binop.op);
                    n->binop.op = strdup("&");
                }
            } else if (strcmp(n->binop.op, "|") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 0) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 0) {
                    Node *res = n->binop.right;
                    n->binop.right = NULL;
                    NodeFree(n);
                    return res;
                } else if (NodesEqual(n->binop.left, n->binop.right) && IsPureNode(n->binop.left)) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, "&") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 0 && IsPureNode(n->binop.left)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 0 && IsPureNode(n->binop.right)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                } else if (NodesEqual(n->binop.left, n->binop.right) && IsPureNode(n->binop.left)) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, "^") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 0) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 0) {
                    Node *res = n->binop.right;
                    n->binop.right = NULL;
                    NodeFree(n);
                    return res;
                } else if (NodesEqual(n->binop.left, n->binop.right) && IsPureNode(n->binop.left)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, "<<") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 0) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 0 && IsPureNode(n->binop.right)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, ">>") == 0) {
                if (n->binop.right->type == NINT && n->binop.right->ival == 0) {
                    Node *res = n->binop.left;
                    n->binop.left = NULL;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NINT && n->binop.left->ival == 0 && IsPureNode(n->binop.right)) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = 0;
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, "==") == 0) {
                if (NodesEqual(n->binop.left, n->binop.right) && IsPureNode(n->binop.left) &&
                    (n->binop.left->type == NINT || n->binop.left->type == NBOOL || n->binop.left->type == NSTR)) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup("true");
                    NodeFree(n);
                    return res;
                }
            } else if (strcmp(n->binop.op, "!=") == 0) {
                if (NodesEqual(n->binop.left, n->binop.right) && IsPureNode(n->binop.left) &&
                    (n->binop.left->type == NINT || n->binop.left->type == NBOOL || n->binop.left->type == NSTR)) {
                    Node *res = NodeNew(NBOOL, n->line);
                    res->sval = strdup("false");
                    NodeFree(n);
                    return res;
                }
            }
            break;
        }

        case NUNOP: {
            n->binop.left = NodeOpt(n->binop.left);
            if (!n->binop.left) return n;

            if (strcmp(n->binop.op, "!") == 0 && n->binop.left->type == NBOOL) {
                int val = strcmp(n->binop.left->sval, "true") == 0;
                Node *res = NodeNew(NBOOL, n->line);
                res->sval = strdup(val ? "false" : "true");
                NodeFree(n);
                return res;
            }
            if (strcmp(n->binop.op, "~") == 0 && n->binop.left->type == NINT) {
                Node *res = NodeNew(NINT, n->line);
                res->ival = ~n->binop.left->ival;
                NodeFree(n);
                return res;
            }
            if (strcmp(n->binop.op, "-") == 0) {
                if (n->binop.left->type == NINT && n->binop.left->ival != INT48_MIN) {
                    Node *res = NodeNew(NINT, n->line);
                    res->ival = -n->binop.left->ival;
                    NodeFree(n);
                    return res;
                } else if (n->binop.left->type == NFLOAT) {
                    Node *res = NodeNew(NFLOAT, n->line);
                    res->fval = -n->binop.left->fval;
                    NodeFree(n);
                    return res;
                }
            }
            break;
        }

        case NIF: {
            n->ifn.cond = NodeOpt(n->ifn.cond);
            n->ifn.then = NodeOpt(n->ifn.then);
            n->ifn.els = NodeOpt(n->ifn.els);

            if (n->ifn.cond->type == NBOOL) {
                int istrue = strcmp(n->ifn.cond->sval, "true") == 0;
                Node *res = istrue ? n->ifn.then : n->ifn.els;
                if (istrue) {
                    n->ifn.then = NULL;
                } else {
                    n->ifn.els = NULL;
                }
                NodeFree(n);
                return res;
            } else if (n->ifn.cond->type == NINT) {
                int istrue = n->ifn.cond->ival != 0;
                Node *res = istrue ? n->ifn.then : n->ifn.els;
                if (istrue) {
                    n->ifn.then = NULL;
                } else {
                    n->ifn.els = NULL;
                }
                NodeFree(n);
                return res;
            }
            break;
        }

        case NWHILE: {
            n->whilen.cond = NodeOpt(n->whilen.cond);
            n->whilen.body = NodeOpt(n->whilen.body);

            if ((n->whilen.cond->type == NBOOL && strcmp(n->whilen.cond->sval, "false") == 0) ||
                (n->whilen.cond->type == NINT && n->whilen.cond->ival == 0)) {
                NodeFree(n);
                return NULL;
            }
            break;
        }

        case NFOR: {
            int unrolled = 0;
            if (n->forn.init && n->forn.cond && n->forn.post) {
                Node *init = n->forn.init;
                Node *cond = n->forn.cond;
                Node *post = n->forn.post;
                char *varname = NULL;
                int64_t start = 0;
                int hasstart = 0;
                if (init->type == NVARDECL) {
                    varname = init->decl.name;
                    if (init->decl.init && init->decl.init->type == NINT) {
                        start = init->decl.init->ival;
                        hasstart = 1;
                    }
                } else if (init->type == NASSIGN) {
                    if (init->assign.target->type == NIDENT) {
                        varname = init->assign.target->sval;
                        if (init->assign.value && init->assign.value->type == NINT) {
                            start = init->assign.value->ival;
                            hasstart = 1;
                        }
                    }
                } else if (init->type == NEXPRSTMT && init->single.expr && init->single.expr->type == NASSIGN) {
                    Node *asg = init->single.expr;
                    if (asg->assign.target->type == NIDENT) {
                        varname = asg->assign.target->sval;
                        if (asg->assign.value && asg->assign.value->type == NINT) {
                            start = asg->assign.value->ival;
                            hasstart = 1;
                        }
                    }
                }
                int64_t limit = 0;
                int haslimit = 0;
                int isless = 0;
                int islesseq = 0;
                if (varname && cond->type == NBINOP) {
                    if (cond->binop.left->type == NIDENT && strcmp(cond->binop.left->sval, varname) == 0) {
                        if (cond->binop.right->type == NINT) {
                            limit = cond->binop.right->ival;
                            haslimit = 1;
                            if (strcmp(cond->binop.op, "<") == 0) {
                                isless = 1;
                            } else if (strcmp(cond->binop.op, "<=") == 0) {
                                islesseq = 1;
                            }
                        }
                    }
                }
                int64_t step = 0;
                int hasstep = 0;
                if (varname && hasstart && haslimit) {
                    if (post->type == NINC) {
                        if (post->incdec.expr->type == NIDENT && strcmp(post->incdec.expr->sval, varname) == 0) {
                            step = 1;
                            hasstep = 1;
                        }
                    } else if (post->type == NDEC) {
                        if (post->incdec.expr->type == NIDENT && strcmp(post->incdec.expr->sval, varname) == 0) {
                            step = -1;
                            hasstep = 1;
                        }
                    } else if (post->type == NASSIGN) {
                        if (post->assign.target->type == NIDENT && strcmp(post->assign.target->sval, varname) == 0) {
                            Node *val = post->assign.value;
                            if (val->type == NBINOP && val->binop.left->type == NIDENT && strcmp(val->binop.left->sval, varname) == 0) {
                                if (val->binop.right->type == NINT) {
                                    int64_t valright = val->binop.right->ival;
                                    if (strcmp(val->binop.op, "+") == 0) {
                                        step = valright;
                                        hasstep = 1;
                                    } else if (strcmp(val->binop.op, "-") == 0) {
                                        step = -valright;
                                        hasstep = 1;
                                    }
                                }
                            }
                        }
                    } else if (post->type == NEXPRSTMT && post->single.expr) {
                        Node *expr = post->single.expr;
                        if (expr->type == NINC) {
                            if (expr->incdec.expr->type == NIDENT && strcmp(expr->incdec.expr->sval, varname) == 0) {
                                step = 1;
                                hasstep = 1;
                            }
                        } else if (expr->type == NDEC) {
                            if (expr->incdec.expr->type == NIDENT && strcmp(expr->incdec.expr->sval, varname) == 0) {
                                step = -1;
                                hasstep = 1;
                            }
                        } else if (expr->type == NASSIGN) {
                            if (expr->assign.target->type == NIDENT && strcmp(expr->assign.target->sval, varname) == 0) {
                                Node *val = expr->assign.value;
                                if (val->type == NBINOP && val->binop.left->type == NIDENT && strcmp(val->binop.left->sval, varname) == 0) {
                                    if (val->binop.right->type == NINT) {
                                        int64_t valright = val->binop.right->ival;
                                        if (strcmp(val->binop.op, "+") == 0) {
                                            step = valright;
                                            hasstep = 1;
                                        } else if (strcmp(val->binop.op, "-") == 0) {
                                            step = -valright;
                                            hasstep = 1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (hasstart && haslimit && hasstep && step != 0) {
                    int64_t itercount = 0;
                    int possible = 0;
                    if (step > 0) {
                        if (isless && start <= limit) {
                            itercount = (limit - start + step - 1) / step;
                            possible = 1;
                        } else if (islesseq && start <= limit) {
                            itercount = (limit - start + step) / step;
                            possible = 1;
                        }
                    }
                    if (possible && itercount >= 0 && itercount <= 4) {
                        Node *block = NodeNew(NBLOCK, n->line);
                        block->block.count = (int)itercount;
                        block->block.stmts = malloc(itercount * sizeof(Node*));
                        int64_t currentval = start;
                        for (int64_t idx = 0; idx < itercount; idx++) {
                            Node *bodycopy = NodeCopy(n->forn.body);
                            NodeSubstitute(&bodycopy, varname, currentval);
                            block->block.stmts[idx] = NodeOpt(bodycopy);
                            currentval += step;
                        }
                        NodeFree(n);
                        return block;
                    }
                }
            }
            if (n->forn.init) n->forn.init = NodeOpt(n->forn.init);
            if (n->forn.cond) n->forn.cond = NodeOpt(n->forn.cond);
            if (n->forn.post) n->forn.post = NodeOpt(n->forn.post);
            n->forn.body = NodeOpt(n->forn.body);
            if (n->forn.cond &&
                ((n->forn.cond->type == NBOOL && strcmp(n->forn.cond->sval, "false") == 0) ||
                 (n->forn.cond->type == NINT && n->forn.cond->ival == 0))) {
                NodeFree(n);
                return NULL;
            }
            break;
        }


        case NBLOCK:
        case NPROGRAM: {
            int newcount = 0;
            int constcapacity = 16;
            int constcount = 0;
            char **constnames = malloc(constcapacity * sizeof(char*));
            Node **constvals = malloc(constcapacity * sizeof(Node*));

            for (int i = 0; i < n->block.count; i++) {
                if (constcount > 0 && n->block.stmts[i]) {
                    ASTPropagateConsts(&n->block.stmts[i], constnames, constvals, constcount);
                }

                n->block.stmts[i] = NodeOpt(n->block.stmts[i]);

                if (n->block.stmts[i] && (n->block.stmts[i]->type == NCONSTDECL ||
                                         (n->block.stmts[i]->type == NVARDECL && n->block.stmts[i]->decl.isconstflag))) {
                    Node *decl = n->block.stmts[i];
                    if (decl->decl.init && (decl->decl.init->type == NINT ||
                                            decl->decl.init->type == NFLOAT ||
                                            decl->decl.init->type == NSTR ||
                                            decl->decl.init->type == NBOOL)) {
                        if (constcount >= constcapacity) {
                            constcapacity *= 2;
                            constnames = realloc(constnames, constcapacity * sizeof(char*));
                            constvals = realloc(constvals, constcapacity * sizeof(Node*));
                        }
                        constnames[constcount] = decl->decl.name;
                        constvals[constcount] = decl->decl.init;
                        constcount++;
                    }
                }

                if (n->block.stmts[i]) {
                    n->block.stmts[newcount++] = n->block.stmts[i];
                    if (n->block.stmts[i]->type == NRETURN || n->block.stmts[i]->type == NTHROW) {
                        for (int j = i + 1; j < n->block.count; j++) {
                            NodeFree(n->block.stmts[j]);
                        }
                        break;
                    }
                }
            }
            n->block.count = newcount;
            free(constnames);
            free(constvals);
            break;
        }

        case NVARDECL:
        case NCONSTDECL:
            if (n->decl.init) n->decl.init = NodeOpt(n->decl.init);
            break;
        case NFNDECL:
            /* Skip optimizing generator/async bodies  -  they use NYIELD and require
               special compiler handling; constant folding and DCE can corrupt them. */
            if (!n->fn.isgen && !n->fn.isasync)
                n->fn.body = NodeOpt(n->fn.body);
            break;
        case NRETURN:
        case NPRINT:
        case NEXPRSTMT:
            if (n->single.expr) n->single.expr = NodeOpt(n->single.expr);
            break;
        case NTRYCATCH:
            n->trycatch.tryblock = NodeOpt(n->trycatch.tryblock);
            n->trycatch.catchblock = NodeOpt(n->trycatch.catchblock);
            break;
        case NTHROW:
            n->throwstmt.expr = NodeOpt(n->throwstmt.expr);
            break;
        case NCOALESCE: {
            n->coalesce.left = NodeOpt(n->coalesce.left);
            n->coalesce.right = NodeOpt(n->coalesce.right);
            if (n->coalesce.left->type == NNULL) {
                Node *res = n->coalesce.right;
                n->coalesce.right = NULL;
                NodeFree(n);
                return res;
            } else if (n->coalesce.left->type == NINT || n->coalesce.left->type == NFLOAT ||
                       n->coalesce.left->type == NSTR || n->coalesce.left->type == NBOOL) {
                Node *res = n->coalesce.left;
                n->coalesce.left = NULL;
                NodeFree(n);
                return res;
            }
            break;
        }
        case NOPTCHAIN:
            n->optchain.obj = NodeOpt(n->optchain.obj);
            if (n->optchain.idx) n->optchain.idx = NodeOpt(n->optchain.idx);
            break;
        case NRANGE:
            n->range.start = NodeOpt(n->range.start);
            n->range.end = NodeOpt(n->range.end);
            break;
        case NASSIGN:
            n->assign.target = NodeOpt(n->assign.target);
            n->assign.value = NodeOpt(n->assign.value);
            break;
        case NCALL:
            n->call.callee = NodeOpt(n->call.callee);
            for (int i = 0; i < n->call.argc; i++) n->call.args[i] = NodeOpt(n->call.args[i]);
            break;
        case NINDEX:
            n->index.arr = NodeOpt(n->index.arr);
            n->index.idx = NodeOpt(n->index.idx);
            break;
        case NMEMBER:
            n->member.obj = NodeOpt(n->member.obj);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) n->arrlit.elems[i] = NodeOpt(n->arrlit.elems[i]);
            break;
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) n->tuple.elems[i] = NodeOpt(n->tuple.elems[i]);
            break;
        case NINC:
        case NDEC:
            n->incdec.expr = NodeOpt(n->incdec.expr);
            break;
        case NAWAIT:
            n->awaitn.expr = NodeOpt(n->awaitn.expr);
            break;
        case NSPAWN:
            n->spawnn.expr = NodeOpt(n->spawnn.expr);
            break;
        case NTRYPROPAGATE:
            n->single.expr = NodeOpt(n->single.expr);
            break;
        case NREF: case NMUTREF:
            n->refnode.expr = NodeOpt(n->refnode.expr);
            break;
        case NYIELD:
            if (n->yieldstmt.expr) n->yieldstmt.expr = NodeOpt(n->yieldstmt.expr);
            break;
        case NMULTIRETURN:
            for (int i = 0; i < n->multiret.count; i++)
                n->multiret.exprs[i] = NodeOpt(n->multiret.exprs[i]);
            break;
        case NSPREAD:
            n->spread.expr = NodeOpt(n->spread.expr);
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++)
                n->maplit.vals[i] = NodeOpt(n->maplit.vals[i]);
            break;
        case NSWITCH:
            n->sw.expr = NodeOpt(n->sw.expr);
            for (int i = 0; i < n->sw.casecount; i++) {
                if (n->sw.casevals[i]) n->sw.casevals[i] = NodeOpt(n->sw.casevals[i]);
                if (n->sw.casebodies[i]) n->sw.casebodies[i] = NodeOpt(n->sw.casebodies[i]);
            }
            if (n->sw.defaultbody) n->sw.defaultbody = NodeOpt(n->sw.defaultbody);
            break;
        case NMATCH:
            n->match.expr = NodeOpt(n->match.expr);
            for (int i = 0; i < n->match.count; i++) {
                if (n->match.guards[i]) n->match.guards[i] = NodeOpt(n->match.guards[i]);
                if (n->match.bodies[i]) n->match.bodies[i] = NodeOpt(n->match.bodies[i]);
            }
            break;
        case NNURSERY:
            if (n->nursery.body) n->nursery.body = NodeOpt(n->nursery.body);
            break;
        case NREGION:
            if (n->region.body) n->region.body = NodeOpt(n->region.body);
            break;
        case NSELECT:
            for (int i = 0; i < n->selectn.count; i++) {
                if (n->selectn.cases[i])  n->selectn.cases[i]  = NodeOpt(n->selectn.cases[i]);
                if (n->selectn.bodies[i]) n->selectn.bodies[i] = NodeOpt(n->selectn.bodies[i]);
            }
            if (n->selectn.defaultbody) n->selectn.defaultbody = NodeOpt(n->selectn.defaultbody);
            break;
        case NCAST:
            n->cast.expr = NodeOpt(n->cast.expr);
            break;
        case NSTRUCTINIT:
            if (n->structinit.spread) n->structinit.spread = NodeOpt(n->structinit.spread);
            for (int i = 0; i < n->structinit.fc; i++) {
                n->structinit.fvals[i] = NodeOpt(n->structinit.fvals[i]);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < n->chainedcmp.count; i++) {
                n->chainedcmp.exprs[i] = NodeOpt(n->chainedcmp.exprs[i]);
            }
            break;
        case NFORIN:
            n->forin.iter = NodeOpt(n->forin.iter);
            n->forin.body = NodeOpt(n->forin.body);
            break;
        case NTERNARY: {
            n->ternary.cond = NodeOpt(n->ternary.cond);
            n->ternary.tval = NodeOpt(n->ternary.tval);
            n->ternary.fval = NodeOpt(n->ternary.fval);

            if (n->ternary.cond) {
                if (n->ternary.cond->type == NBOOL) {
                    int istrue = strcmp(n->ternary.cond->sval, "true") == 0;
                    Node *res = istrue ? n->ternary.tval : n->ternary.fval;
                    if (istrue) {
                        n->ternary.tval = NULL;
                    } else {
                        n->ternary.fval = NULL;
                    }
                    NodeFree(n);
                    return res;
                } else if (n->ternary.cond->type == NINT) {
                    int istrue = n->ternary.cond->ival != 0;
                    Node *res = istrue ? n->ternary.tval : n->ternary.fval;
                    if (istrue) {
                        n->ternary.tval = NULL;
                    } else {
                        n->ternary.fval = NULL;
                    }
                    NodeFree(n);
                    return res;
                }
            }
            break;
        }
        default: break;
    }

    return n;
}

static void NodeSubstitute(Node **nodeptr, const char *varname, int64_t val) {
    Node *n = *nodeptr;
    if (!n) return;
    if (n->type == NIDENT && strcmp(n->sval, varname) == 0) {
        Node *newval = NodeNew(NINT, n->line);
        newval->ival = val;
        NodeFree(n);
        *nodeptr = newval;
        return;
    }
    switch (n->type) {
        case NBINOP: case NUNOP:
            NodeSubstitute(&n->binop.left, varname, val);
            NodeSubstitute(&n->binop.right, varname, val);
            break;
        case NVARDECL: case NCONSTDECL:
            NodeSubstitute(&n->decl.init, varname, val);
            NodeSubstitute(&n->decl.isconst, varname, val);
            break;
        case NFNDECL:
        case NMACRODECL:
            for (int i = 0; i < n->fn.pc; i++) {
                if (strcmp(n->fn.pnames[i], varname) == 0) return;
            }
            if (n->fn.pdefaults) {
                for (int i = 0; i < n->fn.pc; i++) {
                    NodeSubstitute(&n->fn.pdefaults[i], varname, val);
                }
            }
            NodeSubstitute(&n->fn.body, varname, val);
            break;
        case NBLOCK: case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) {
                NodeSubstitute(&n->block.stmts[i], varname, val);
            }
            break;
        case NIF:
            NodeSubstitute(&n->ifn.cond, varname, val);
            NodeSubstitute(&n->ifn.then, varname, val);
            NodeSubstitute(&n->ifn.els, varname, val);
            break;
        case NTERNARY:
            NodeSubstitute(&n->ternary.cond, varname, val);
            NodeSubstitute(&n->ternary.tval, varname, val);
            NodeSubstitute(&n->ternary.fval, varname, val);
            break;
        case NWHILE:
            NodeSubstitute(&n->whilen.cond, varname, val);
            NodeSubstitute(&n->whilen.body, varname, val);
            break;
        case NFOR:
            if (n->forn.init && n->forn.init->type == NVARDECL && strcmp(n->forn.init->decl.name, varname) == 0) {
                NodeSubstitute(&n->forn.init->decl.init, varname, val);
                break;
            }
            if (n->forn.init) NodeSubstitute(&n->forn.init, varname, val);
            if (n->forn.cond) NodeSubstitute(&n->forn.cond, varname, val);
            if (n->forn.post) NodeSubstitute(&n->forn.post, varname, val);
            NodeSubstitute(&n->forn.body, varname, val);
            break;
        case NFORIN:
            if (strcmp(n->forin.var, varname) == 0) {
                NodeSubstitute(&n->forin.iter, varname, val);
                break;
            }
            NodeSubstitute(&n->forin.iter, varname, val);
            NodeSubstitute(&n->forin.body, varname, val);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NMODULE:
            NodeSubstitute(&n->single.expr, varname, val);
            break;
        case NUSE:
            break;
        case NTRYCATCH:
            NodeSubstitute(&n->trycatch.tryblock, varname, val);
            if (n->trycatch.errvar && strcmp(n->trycatch.errvar, varname) == 0) {
                if (n->trycatch.finallyblock) NodeSubstitute(&n->trycatch.finallyblock, varname, val);
                break;
            }
            NodeSubstitute(&n->trycatch.catchblock, varname, val);
            if (n->trycatch.finallyblock) NodeSubstitute(&n->trycatch.finallyblock, varname, val);
            break;
        case NTHROW:
            NodeSubstitute(&n->throwstmt.expr, varname, val);
            break;
        case NCOALESCE:
            NodeSubstitute(&n->coalesce.left, varname, val);
            NodeSubstitute(&n->coalesce.right, varname, val);
            break;
        case NOPTCHAIN:
            NodeSubstitute(&n->optchain.obj, varname, val);
            NodeSubstitute(&n->optchain.idx, varname, val);
            break;
        case NSTRUCTINIT:
            if (n->structinit.spread) NodeSubstitute(&n->structinit.spread, varname, val);
            for (int i = 0; i < n->structinit.fc; i++) {
                NodeSubstitute(&n->structinit.fvals[i], varname, val);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < n->chainedcmp.count; i++) {
                NodeSubstitute(&n->chainedcmp.exprs[i], varname, val);
            }
            break;
        case NRANGE:
            NodeSubstitute(&n->range.start, varname, val);
            NodeSubstitute(&n->range.end, varname, val);
            break;
        case NASSIGN:
            NodeSubstitute(&n->assign.target, varname, val);
            NodeSubstitute(&n->assign.value, varname, val);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            NodeSubstitute(&n->call.callee, varname, val);
            for (int i = 0; i < n->call.argc; i++) {
                NodeSubstitute(&n->call.args[i], varname, val);
            }
            break;
        case NINDEX:
            NodeSubstitute(&n->index.arr, varname, val);
            NodeSubstitute(&n->index.idx, varname, val);
            break;
        case NMEMBER:
            NodeSubstitute(&n->member.obj, varname, val);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                NodeSubstitute(&n->arrlit.elems[i], varname, val);
            }
            break;
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) {
                NodeSubstitute(&n->tuple.elems[i], varname, val);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++) {
                NodeSubstitute(&n->maplit.vals[i], varname, val);
            }
            break;
        case NINC: case NDEC:
            NodeSubstitute(&n->incdec.expr, varname, val);
            break;
        case NNEW:
            break;
        case NSWITCH:
            NodeSubstitute(&n->sw.expr, varname, val);
            for (int i = 0; i < n->sw.casecount; i++) {
                NodeSubstitute(&n->sw.casevals[i], varname, val);
                NodeSubstitute(&n->sw.casebodies[i], varname, val);
            }
            if (n->sw.defaultbody) NodeSubstitute(&n->sw.defaultbody, varname, val);
            break;
        case NYIELD:
            NodeSubstitute(&n->yieldstmt.expr, varname, val);
            break;
        case NMULTIRETURN:
            for (int i = 0; i < n->multiret.count; i++) {
                NodeSubstitute(&n->multiret.exprs[i], varname, val);
            }
            break;
        case NAWAIT:
            NodeSubstitute(&n->awaitn.expr, varname, val);
            break;
        case NSPAWN:
            NodeSubstitute(&n->spawnn.expr, varname, val);
            break;
        case NTRYPROPAGATE:
            NodeSubstitute(&n->single.expr, varname, val);
            break;
        case NMATCH:
            NodeSubstitute(&n->match.expr, varname, val);
            for (int i = 0; i < n->match.count; i++) {
                NodeSubstitute(&n->match.patterns[i], varname, val);
                NodeSubstitute(&n->match.bodies[i], varname, val);
            }
            break;
        default:
            break;
    }
}

void ASTSubstitute(Node **nodeptr, char **pnames, Node **args, int pc) {
    Node *n = *nodeptr;
    if (!n) return;
    if (n->type == NIDENT) {
        for (int i = 0; i < pc; i++) {
            if (strcmp(n->sval, pnames[i]) == 0) {
                Node *argcopy = NodeCopy(args[i]);
                NodeFree(n);
                *nodeptr = argcopy;
                return;
            }
        }
    }
    switch (n->type) {
        case NBINOP: case NUNOP:
            ASTSubstitute(&n->binop.left, pnames, args, pc);
            ASTSubstitute(&n->binop.right, pnames, args, pc);
            break;
        case NVARDECL: case NCONSTDECL:
            ASTSubstitute(&n->decl.init, pnames, args, pc);
            ASTSubstitute(&n->decl.isconst, pnames, args, pc);
            break;
        case NFNDECL:
        case NMACRODECL:
            if (n->fn.pdefaults) {
                for (int i = 0; i < n->fn.pc; i++) {
                    ASTSubstitute(&n->fn.pdefaults[i], pnames, args, pc);
                }
            }
            ASTSubstitute(&n->fn.body, pnames, args, pc);
            break;
        case NBLOCK: case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) {
                ASTSubstitute(&n->block.stmts[i], pnames, args, pc);
            }
            break;
        case NIF:
            ASTSubstitute(&n->ifn.cond, pnames, args, pc);
            ASTSubstitute(&n->ifn.then, pnames, args, pc);
            ASTSubstitute(&n->ifn.els, pnames, args, pc);
            break;
        case NTERNARY:
            ASTSubstitute(&n->ternary.cond, pnames, args, pc);
            ASTSubstitute(&n->ternary.tval, pnames, args, pc);
            ASTSubstitute(&n->ternary.fval, pnames, args, pc);
            break;
        case NWHILE:
            ASTSubstitute(&n->whilen.cond, pnames, args, pc);
            ASTSubstitute(&n->whilen.body, pnames, args, pc);
            break;
        case NFOR:
            if (n->forn.init) ASTSubstitute(&n->forn.init, pnames, args, pc);
            if (n->forn.cond) ASTSubstitute(&n->forn.cond, pnames, args, pc);
            if (n->forn.post) ASTSubstitute(&n->forn.post, pnames, args, pc);
            ASTSubstitute(&n->forn.body, pnames, args, pc);
            break;
        case NFORIN:
            ASTSubstitute(&n->forin.iter, pnames, args, pc);
            ASTSubstitute(&n->forin.body, pnames, args, pc);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NMODULE:
            ASTSubstitute(&n->single.expr, pnames, args, pc);
            break;
        case NUSE:
            break;
        case NTRYCATCH:
            ASTSubstitute(&n->trycatch.tryblock, pnames, args, pc);
            ASTSubstitute(&n->trycatch.catchblock, pnames, args, pc);
            if (n->trycatch.finallyblock) ASTSubstitute(&n->trycatch.finallyblock, pnames, args, pc);
            break;
        case NTHROW:
            ASTSubstitute(&n->throwstmt.expr, pnames, args, pc);
            break;
        case NCOALESCE:
            ASTSubstitute(&n->coalesce.left, pnames, args, pc);
            ASTSubstitute(&n->coalesce.right, pnames, args, pc);
            break;
        case NOPTCHAIN:
            ASTSubstitute(&n->optchain.obj, pnames, args, pc);
            if (n->optchain.idx) ASTSubstitute(&n->optchain.idx, pnames, args, pc);
            break;
        case NSTRUCTINIT:
            if (n->structinit.spread) ASTSubstitute(&n->structinit.spread, pnames, args, pc);
            for (int i = 0; i < n->structinit.fc; i++) {
                ASTSubstitute(&n->structinit.fvals[i], pnames, args, pc);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < n->chainedcmp.count; i++) {
                ASTSubstitute(&n->chainedcmp.exprs[i], pnames, args, pc);
            }
            break;
        case NRANGE:
            ASTSubstitute(&n->range.start, pnames, args, pc);
            ASTSubstitute(&n->range.end, pnames, args, pc);
            break;
        case NASSIGN:
            ASTSubstitute(&n->assign.target, pnames, args, pc);
            ASTSubstitute(&n->assign.value, pnames, args, pc);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            ASTSubstitute(&n->call.callee, pnames, args, pc);
            for (int i = 0; i < n->call.argc; i++) {
                ASTSubstitute(&n->call.args[i], pnames, args, pc);
            }
            break;
        case NINDEX:
            ASTSubstitute(&n->index.arr, pnames, args, pc);
            ASTSubstitute(&n->index.idx, pnames, args, pc);
            break;
        case NMEMBER:
            ASTSubstitute(&n->member.obj, pnames, args, pc);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                ASTSubstitute(&n->arrlit.elems[i], pnames, args, pc);
            }
            break;
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) {
                ASTSubstitute(&n->tuple.elems[i], pnames, args, pc);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++) {
                ASTSubstitute(&n->maplit.vals[i], pnames, args, pc);
            }
            break;
        case NINC: case NDEC:
            ASTSubstitute(&n->incdec.expr, pnames, args, pc);
            break;
        case NAWAIT:
            ASTSubstitute(&n->awaitn.expr, pnames, args, pc);
            break;
        case NSPAWN:
            ASTSubstitute(&n->spawnn.expr, pnames, args, pc);
            break;
        case NTRYPROPAGATE:
            ASTSubstitute(&n->single.expr, pnames, args, pc);
            break;
        case NSWITCH:
            ASTSubstitute(&n->sw.expr, pnames, args, pc);
            for (int i = 0; i < n->sw.casecount; i++) {
                ASTSubstitute(&n->sw.casevals[i], pnames, args, pc);
                ASTSubstitute(&n->sw.casebodies[i], pnames, args, pc);
            }
            ASTSubstitute(&n->sw.defaultbody, pnames, args, pc);
            break;
        case NYIELD:
            ASTSubstitute(&n->yieldstmt.expr, pnames, args, pc);
            break;
        case NSPREAD:
            ASTSubstitute(&n->spread.expr, pnames, args, pc);
            break;
        case NDESTRUCTARR:
            ASTSubstitute(&n->destructarr.value, pnames, args, pc);
            break;
        case NDESTRUCTOBJ:
            ASTSubstitute(&n->destructobj.value, pnames, args, pc);
            break;
        case NMULTIRETURN:
            for (int i = 0; i < n->multiret.count; i++) {
                ASTSubstitute(&n->multiret.exprs[i], pnames, args, pc);
            }
            break;
        default: break;
    }
}

static void ASTPropagateConsts(Node **nodeptr, char **names, Node **vals, int count) {
    Node *n = *nodeptr;
    if (!n) return;
    if (n->type == NIDENT) {
        for (int i = 0; i < count; i++) {
            if (strcmp(n->sval, names[i]) == 0) {
                Node *copy = NodeCopy(vals[i]);
                NodeFree(n);
                *nodeptr = copy;
                return;
            }
        }
    }
    switch (n->type) {
        case NBINOP: case NUNOP:
            ASTPropagateConsts(&n->binop.left, names, vals, count);
            ASTPropagateConsts(&n->binop.right, names, vals, count);
            break;
        case NVARDECL: case NCONSTDECL:
            ASTPropagateConsts(&n->decl.init, names, vals, count);
            ASTPropagateConsts(&n->decl.isconst, names, vals, count);
            break;
        case NFNDECL:
        case NMACRODECL:
            if (n->fn.pdefaults) {
                for (int i = 0; i < n->fn.pc; i++) {
                    ASTPropagateConsts(&n->fn.pdefaults[i], names, vals, count);
                }
            }
            /* Don't propagate outer-scope constants into generator/async bodies.
               Their local state (e.g. loop counters) must remain mutable. */
            if (!n->fn.isgen && !n->fn.isasync)
                ASTPropagateConsts(&n->fn.body, names, vals, count);
            break;
        case NBLOCK: case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) {
                ASTPropagateConsts(&n->block.stmts[i], names, vals, count);
            }
            break;
        case NIF:
            ASTPropagateConsts(&n->ifn.cond, names, vals, count);
            ASTPropagateConsts(&n->ifn.then, names, vals, count);
            ASTPropagateConsts(&n->ifn.els, names, vals, count);
            break;
        case NTERNARY:
            ASTPropagateConsts(&n->ternary.cond, names, vals, count);
            ASTPropagateConsts(&n->ternary.tval, names, vals, count);
            ASTPropagateConsts(&n->ternary.fval, names, vals, count);
            break;
        case NWHILE:
            ASTPropagateConsts(&n->whilen.cond, names, vals, count);
            ASTPropagateConsts(&n->whilen.body, names, vals, count);
            break;
        case NFOR:
            if (n->forn.init) ASTPropagateConsts(&n->forn.init, names, vals, count);
            if (n->forn.cond) ASTPropagateConsts(&n->forn.cond, names, vals, count);
            if (n->forn.post) ASTPropagateConsts(&n->forn.post, names, vals, count);
            ASTPropagateConsts(&n->forn.body, names, vals, count);
            break;
        case NFORIN:
            ASTPropagateConsts(&n->forin.iter, names, vals, count);
            ASTPropagateConsts(&n->forin.body, names, vals, count);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NMODULE:
            ASTPropagateConsts(&n->single.expr, names, vals, count);
            break;
        case NUSE:
            break;
        case NTRYCATCH:
            ASTPropagateConsts(&n->trycatch.tryblock, names, vals, count);
            ASTPropagateConsts(&n->trycatch.catchblock, names, vals, count);
            if (n->trycatch.finallyblock) ASTPropagateConsts(&n->trycatch.finallyblock, names, vals, count);
            break;
        case NTHROW:
            ASTPropagateConsts(&n->throwstmt.expr, names, vals, count);
            break;
        case NCOALESCE:
            ASTPropagateConsts(&n->coalesce.left, names, vals, count);
            ASTPropagateConsts(&n->coalesce.right, names, vals, count);
            break;
        case NOPTCHAIN:
            ASTPropagateConsts(&n->optchain.obj, names, vals, count);
            if (n->optchain.idx) ASTPropagateConsts(&n->optchain.idx, names, vals, count);
            break;
        case NSTRUCTINIT:
            if (n->structinit.spread) ASTPropagateConsts(&n->structinit.spread, names, vals, count);
            for (int i = 0; i < n->structinit.fc; i++) {
                ASTPropagateConsts(&n->structinit.fvals[i], names, vals, count);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < n->chainedcmp.count; i++) {
                ASTPropagateConsts(&n->chainedcmp.exprs[i], names, vals, count);
            }
            break;
        case NRANGE:
            ASTPropagateConsts(&n->range.start, names, vals, count);
            ASTPropagateConsts(&n->range.end, names, vals, count);
            break;
        case NASSIGN:
            ASTPropagateConsts(&n->assign.target, names, vals, count);
            ASTPropagateConsts(&n->assign.value, names, vals, count);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            ASTPropagateConsts(&n->call.callee, names, vals, count);
            for (int i = 0; i < n->call.argc; i++) {
                ASTPropagateConsts(&n->call.args[i], names, vals, count);
            }
            break;
        case NINDEX:
            ASTPropagateConsts(&n->index.arr, names, vals, count);
            ASTPropagateConsts(&n->index.idx, names, vals, count);
            break;
        case NMEMBER:
            ASTPropagateConsts(&n->member.obj, names, vals, count);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                ASTPropagateConsts(&n->arrlit.elems[i], names, vals, count);
            }
            break;
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) {
                ASTPropagateConsts(&n->tuple.elems[i], names, vals, count);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++) {
                ASTPropagateConsts(&n->maplit.vals[i], names, vals, count);
            }
            break;
        case NINC: case NDEC:
            ASTPropagateConsts(&n->incdec.expr, names, vals, count);
            break;
        case NAWAIT:
            ASTPropagateConsts(&n->awaitn.expr, names, vals, count);
            break;
        case NSPAWN:
            ASTPropagateConsts(&n->spawnn.expr, names, vals, count);
            break;
        case NTRYPROPAGATE:
            ASTPropagateConsts(&n->single.expr, names, vals, count);
            break;
        case NSWITCH:
            ASTPropagateConsts(&n->sw.expr, names, vals, count);
            for (int i = 0; i < n->sw.casecount; i++) {
                ASTPropagateConsts(&n->sw.casevals[i], names, vals, count);
                ASTPropagateConsts(&n->sw.casebodies[i], names, vals, count);
            }
            ASTPropagateConsts(&n->sw.defaultbody, names, vals, count);
            break;
        case NYIELD:
            ASTPropagateConsts(&n->yieldstmt.expr, names, vals, count);
            break;
        case NSPREAD:
            ASTPropagateConsts(&n->spread.expr, names, vals, count);
            break;
        case NDESTRUCTARR:
            ASTPropagateConsts(&n->destructarr.value, names, vals, count);
            break;
        case NDESTRUCTOBJ:
            ASTPropagateConsts(&n->destructobj.value, names, vals, count);
            break;
        case NMULTIRETURN:
            for (int i = 0; i < n->multiret.count; i++) {
                ASTPropagateConsts(&n->multiret.exprs[i], names, vals, count);
            }
            break;
        default: break;
    }
}

static int CountNodes(Node *node) {
    if (!node) return 0;
    int count = 1;
    switch (node->type) {
        case NBINOP: case NUNOP:
            count += CountNodes(node->binop.left);
            count += CountNodes(node->binop.right);
            break;
        case NVARDECL: case NCONSTDECL:
            count += CountNodes(node->decl.init);
            count += CountNodes(node->decl.isconst);
            break;
        case NFNDECL:
        case NMACRODECL:
            if (node->fn.pdefaults) {
                for (int i = 0; i < node->fn.pc; i++) {
                    count += CountNodes(node->fn.pdefaults[i]);
                }
            }
            count += CountNodes(node->fn.body);
            break;
        case NBLOCK: case NPROGRAM:
            for (int i = 0; i < node->block.count; i++) {
                count += CountNodes(node->block.stmts[i]);
            }
            break;
        case NIF:
            count += CountNodes(node->ifn.cond);
            count += CountNodes(node->ifn.then);
            count += CountNodes(node->ifn.els);
            break;
        case NTERNARY:
            count += CountNodes(node->ternary.cond);
            count += CountNodes(node->ternary.tval);
            count += CountNodes(node->ternary.fval);
            break;
        case NWHILE:
            count += CountNodes(node->whilen.cond);
            count += CountNodes(node->whilen.body);
            break;
        case NFOR:
            count += CountNodes(node->forn.init);
            count += CountNodes(node->forn.cond);
            count += CountNodes(node->forn.post);
            count += CountNodes(node->forn.body);
            break;
        case NFORIN:
            count += CountNodes(node->forin.iter);
            count += CountNodes(node->forin.body);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NMODULE:
            count += CountNodes(node->single.expr);
            break;
        case NUSE:
            break;
        case NTRYCATCH:
            count += CountNodes(node->trycatch.tryblock);
            count += CountNodes(node->trycatch.catchblock);
            if (node->trycatch.finallyblock) count += CountNodes(node->trycatch.finallyblock);
            break;
        case NTHROW:
            count += CountNodes(node->throwstmt.expr);
            break;
        case NCOALESCE:
            count += CountNodes(node->coalesce.left);
            count += CountNodes(node->coalesce.right);
            break;
        case NOPTCHAIN:
            count += CountNodes(node->optchain.obj);
            count += CountNodes(node->optchain.idx);
            break;
        case NSTRUCTINIT:
            if (node->structinit.spread) count += CountNodes(node->structinit.spread);
            for (int i = 0; i < node->structinit.fc; i++) {
                count += CountNodes(node->structinit.fvals[i]);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < node->chainedcmp.count; i++) {
                count += CountNodes(node->chainedcmp.exprs[i]);
            }
            break;
        case NRANGE:
            count += CountNodes(node->range.start);
            count += CountNodes(node->range.end);
            break;
        case NASSIGN:
            count += CountNodes(node->assign.target);
            count += CountNodes(node->assign.value);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            count += CountNodes(node->call.callee);
            for (int i = 0; i < node->call.argc; i++) {
                count += CountNodes(node->call.args[i]);
            }
            break;
        case NINDEX:
            count += CountNodes(node->index.arr);
            count += CountNodes(node->index.idx);
            break;
        case NMEMBER:
            count += CountNodes(node->member.obj);
            break;
        case NARRLIT:
            for (int i = 0; i < node->arrlit.count; i++) {
                count += CountNodes(node->arrlit.elems[i]);
            }
            break;
        case NTUPLE:
            for (int i = 0; i < node->tuple.count; i++) {
                count += CountNodes(node->tuple.elems[i]);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < node->maplit.count; i++) {
                count += CountNodes(node->maplit.vals[i]);
            }
            break;
        case NINC: case NDEC:
            count += CountNodes(node->incdec.expr);
            break;
        case NSWITCH:
            count += CountNodes(node->sw.expr);
            for (int i = 0; i < node->sw.casecount; i++) {
                count += CountNodes(node->sw.casevals[i]);
                count += CountNodes(node->sw.casebodies[i]);
            }
            if (node->sw.defaultbody) count += CountNodes(node->sw.defaultbody);
            break;
        case NYIELD:
            count += CountNodes(node->yieldstmt.expr);
            break;
        case NMULTIRETURN:
            for (int i = 0; i < node->multiret.count; i++) {
                count += CountNodes(node->multiret.exprs[i]);
            }
            break;
        case NAWAIT:
            count += CountNodes(node->awaitn.expr);
            break;
        case NSPAWN:
            count += CountNodes(node->spawnn.expr);
            break;
        case NTRYPROPAGATE:
            count += CountNodes(node->single.expr);
            break;
        case NMATCH:
            count += CountNodes(node->match.expr);
            for (int i = 0; i < node->match.count; i++) {
                count += CountNodes(node->match.patterns[i]);
                count += CountNodes(node->match.bodies[i]);
            }
            break;
        default: break;
    }
    return count;
}

static Node *GetSingleExpr(Node *body) {
    if (!body) return NULL;
    if (body->type == NBLOCK || body->type == NPROGRAM) {
        if (body->block.count == 1) {
            return GetSingleExpr(body->block.stmts[0]);
        }
    }
    if (body->type == NRETURN) {
        return body->single.expr;
    }
    if (body->type == NEXPRSTMT) {
        return body->single.expr;
    }
    return NULL;
}

static void InlineCallS(Node **nodeptr, InlineableFn *fns, int count) {
    Node *n = *nodeptr;
    if (!n) return;
    if (n->type == NCALL && n->call.callee->type == NIDENT) {
        const char *name = n->call.callee->sval;
        int idx = -1;
        for (int i = 0; i < count; i++) {
            if (strcmp(fns[i].name, name) == 0) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            Node *fn = fns[idx].decl;
            Node *single = GetSingleExpr(fn->fn.body);
            if (single && n->call.argc == fn->fn.pc) {
                Node *cloned = NodeCopy(single);
                ASTSubstitute(&cloned, fn->fn.pnames, n->call.args, fn->fn.pc);
                NodeFree(n);
                *nodeptr = cloned;
                InlineCallS(nodeptr, fns, count);
                return;
            }
        }
    }
    switch (n->type) {
        case NBINOP: case NUNOP:
            InlineCallS(&n->binop.left, fns, count);
            InlineCallS(&n->binop.right, fns, count);
            break;
        case NVARDECL: case NCONSTDECL:
            InlineCallS(&n->decl.init, fns, count);
            InlineCallS(&n->decl.isconst, fns, count);
            break;
        case NFNDECL:
        case NMACRODECL:
            if (n->fn.pdefaults) {
                for (int i = 0; i < n->fn.pc; i++) {
                    InlineCallS(&n->fn.pdefaults[i], fns, count);
                }
            }
            InlineCallS(&n->fn.body, fns, count);
            break;
        case NBLOCK: case NPROGRAM:
            for (int i = 0; i < n->block.count; i++) {
                InlineCallS(&n->block.stmts[i], fns, count);
            }
            break;
        case NIF:
            InlineCallS(&n->ifn.cond, fns, count);
            InlineCallS(&n->ifn.then, fns, count);
            InlineCallS(&n->ifn.els, fns, count);
            break;
        case NTERNARY:
            InlineCallS(&n->ternary.cond, fns, count);
            InlineCallS(&n->ternary.tval, fns, count);
            InlineCallS(&n->ternary.fval, fns, count);
            break;
        case NWHILE:
            InlineCallS(&n->whilen.cond, fns, count);
            InlineCallS(&n->whilen.body, fns, count);
            break;
        case NFOR:
            if (n->forn.init) InlineCallS(&n->forn.init, fns, count);
            if (n->forn.cond) InlineCallS(&n->forn.cond, fns, count);
            if (n->forn.post) InlineCallS(&n->forn.post, fns, count);
            InlineCallS(&n->forn.body, fns, count);
            break;
        case NFORIN:
            InlineCallS(&n->forin.iter, fns, count);
            InlineCallS(&n->forin.body, fns, count);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NMODULE:
            InlineCallS(&n->single.expr, fns, count);
            break;
        case NUSE:
            break;
        case NTRYCATCH:
            InlineCallS(&n->trycatch.tryblock, fns, count);
            InlineCallS(&n->trycatch.catchblock, fns, count);
            if (n->trycatch.finallyblock) InlineCallS(&n->trycatch.finallyblock, fns, count);
            break;
        case NTHROW:
            InlineCallS(&n->throwstmt.expr, fns, count);
            break;
        case NCOALESCE:
            InlineCallS(&n->coalesce.left, fns, count);
            InlineCallS(&n->coalesce.right, fns, count);
            break;
        case NOPTCHAIN:
            InlineCallS(&n->optchain.obj, fns, count);
            if (n->optchain.idx) InlineCallS(&n->optchain.idx, fns, count);
            break;
        case NSTRUCTINIT:
            if (n->structinit.spread) InlineCallS(&n->structinit.spread, fns, count);
            for (int i = 0; i < n->structinit.fc; i++) {
                InlineCallS(&n->structinit.fvals[i], fns, count);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < n->chainedcmp.count; i++) {
                InlineCallS(&n->chainedcmp.exprs[i], fns, count);
            }
            break;
        case NRANGE:
            InlineCallS(&n->range.start, fns, count);
            InlineCallS(&n->range.end, fns, count);
            break;
        case NASSIGN:
            InlineCallS(&n->assign.target, fns, count);
            InlineCallS(&n->assign.value, fns, count);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            InlineCallS(&n->call.callee, fns, count);
            for (int i = 0; i < n->call.argc; i++) {
                InlineCallS(&n->call.args[i], fns, count);
            }
            break;
        case NINDEX:
            InlineCallS(&n->index.arr, fns, count);
            InlineCallS(&n->index.idx, fns, count);
            break;
        case NMEMBER:
            InlineCallS(&n->member.obj, fns, count);
            break;
        case NARRLIT:
            for (int i = 0; i < n->arrlit.count; i++) {
                InlineCallS(&n->arrlit.elems[i], fns, count);
            }
            break;
        case NTUPLE:
            for (int i = 0; i < n->tuple.count; i++) {
                InlineCallS(&n->tuple.elems[i], fns, count);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < n->maplit.count; i++) {
                InlineCallS(&n->maplit.vals[i], fns, count);
            }
            break;
        case NINC: case NDEC:
            InlineCallS(&n->incdec.expr, fns, count);
            break;
        case NSWITCH:
            InlineCallS(&n->sw.expr, fns, count);
            for (int i = 0; i < n->sw.casecount; i++) {
                InlineCallS(&n->sw.casevals[i], fns, count);
                InlineCallS(&n->sw.casebodies[i], fns, count);
            }
            if (n->sw.defaultbody) InlineCallS(&n->sw.defaultbody, fns, count);
            break;
        case NYIELD:
            InlineCallS(&n->yieldstmt.expr, fns, count);
            break;
        case NMULTIRETURN:
            for (int i = 0; i < n->multiret.count; i++) {
                InlineCallS(&n->multiret.exprs[i], fns, count);
            }
            break;
        case NAWAIT:
            InlineCallS(&n->awaitn.expr, fns, count);
            break;
        case NSPAWN:
            InlineCallS(&n->spawnn.expr, fns, count);
            break;
        case NTRYPROPAGATE:
            InlineCallS(&n->single.expr, fns, count);
            break;
        case NMATCH:
            InlineCallS(&n->match.expr, fns, count);
            for (int i = 0; i < n->match.count; i++) {
                InlineCallS(&n->match.patterns[i], fns, count);
                InlineCallS(&n->match.bodies[i], fns, count);
            }
            break;
        default: break;
    }
}

Node *NodeInlineOpt(Node *n) {
    if (!n) return NULL;
    if (n->type == NBLOCK || n->type == NPROGRAM) {
        int cap = 16;
        int count = 0;
        InlineableFn *fns = malloc(cap * sizeof(InlineableFn));
        for (int i = 0; i < n->block.count; i++) {
            Node *stmt = n->block.stmts[i];
            if (stmt && stmt->type == NFNDECL) {
                int nodes = CountNodes(stmt->fn.body);
                int recursive = CountReferences(stmt->fn.body, stmt->fn.name) > 0;
                if (nodes <= 15 && !recursive && !stmt->fn.isgen && !stmt->fn.isasync) {
                    if (count >= cap) {
                        cap *= 2;
                        fns = realloc(fns, cap * sizeof(InlineableFn));
                    }
                    strncpy(fns[count].name, stmt->fn.name, 127);
                    fns[count].name[127] = '\0';
                    fns[count].decl = stmt;
                    count++;
                }
            }
        }
        if (count > 0) {
            for (int i = 0; i < n->block.count; i++) {
                InlineCallS(&n->block.stmts[i], fns, count);
            }
        }
        free(fns);
    }
    switch (n->type) {
        case NFNDECL:
            if (!n->fn.isgen && !n->fn.isasync)
                n->fn.body = NodeInlineOpt(n->fn.body);
            break;
        case NIF:
            n->ifn.then = NodeInlineOpt(n->ifn.then);
            if (n->ifn.els) n->ifn.els = NodeInlineOpt(n->ifn.els);
            break;
        case NTERNARY:
            n->ternary.cond = NodeInlineOpt(n->ternary.cond);
            n->ternary.tval = NodeInlineOpt(n->ternary.tval);
            n->ternary.fval = NodeInlineOpt(n->ternary.fval);
            break;
        case NWHILE:
            n->whilen.body = NodeInlineOpt(n->whilen.body);
            break;
        case NFOR:
            n->forn.body = NodeInlineOpt(n->forn.body);
            break;
        case NTRYCATCH:
            n->trycatch.tryblock = NodeInlineOpt(n->trycatch.tryblock);
            n->trycatch.catchblock = NodeInlineOpt(n->trycatch.catchblock);
            if (n->trycatch.finallyblock) n->trycatch.finallyblock = NodeInlineOpt(n->trycatch.finallyblock);
            break;
        case NSWITCH:
            for (int i = 0; i < n->sw.casecount; i++) {
                n->sw.casebodies[i] = NodeInlineOpt(n->sw.casebodies[i]);
            }
            if (n->sw.defaultbody) n->sw.defaultbody = NodeInlineOpt(n->sw.defaultbody);
            break;
        case NMATCH:
            for (int i = 0; i < n->match.count; i++) {
                n->match.bodies[i] = NodeInlineOpt(n->match.bodies[i]);
            }
            break;
        default:
            break;
    }
    return n;
}

static int CountReferences(Node *node, const char *name) {
    if (!node) return 0;
    if (node->type == NIDENT && strcmp(node->sval, name) == 0) {
        return 1;
    }
    int count = 0;
    switch (node->type) {
        case NBINOP: case NUNOP:
            count += CountReferences(node->binop.left, name);
            count += CountReferences(node->binop.right, name);
            break;
        case NVARDECL: case NCONSTDECL:
            count += CountReferences(node->decl.init, name);
            count += CountReferences(node->decl.isconst, name);
            break;
        case NFNDECL:
        case NMACRODECL:
            for (int i = 0; i < node->fn.pc; i++) {
                if (strcmp(node->fn.pnames[i], name) == 0) return 0;
            }
            if (node->fn.pdefaults) {
                for (int i = 0; i < node->fn.pc; i++) {
                    count += CountReferences(node->fn.pdefaults[i], name);
                }
            }
            count += CountReferences(node->fn.body, name);
            break;
        case NBLOCK: case NPROGRAM:
            for (int i = 0; i < node->block.count; i++) {
                count += CountReferences(node->block.stmts[i], name);
            }
            break;
        case NIF:
            count += CountReferences(node->ifn.cond, name);
            count += CountReferences(node->ifn.then, name);
            count += CountReferences(node->ifn.els, name);
            break;
        case NTERNARY:
            count += CountReferences(node->ternary.cond, name);
            count += CountReferences(node->ternary.tval, name);
            count += CountReferences(node->ternary.fval, name);
            break;
        case NWHILE:
            count += CountReferences(node->whilen.cond, name);
            count += CountReferences(node->whilen.body, name);
            break;
        case NFOR:
            count += CountReferences(node->forn.init, name);
            count += CountReferences(node->forn.cond, name);
            count += CountReferences(node->forn.post, name);
            count += CountReferences(node->forn.body, name);
            break;
        case NFORIN:
            if (strcmp(node->forin.var, name) != 0) {
                count += CountReferences(node->forin.body, name);
            }
            count += CountReferences(node->forin.iter, name);
            break;
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NMODULE:
            count += CountReferences(node->single.expr, name);
            break;
        case NUSE:
            break;
        case NTRYCATCH:
            count += CountReferences(node->trycatch.tryblock, name);
            if (!node->trycatch.errvar || strcmp(node->trycatch.errvar, name) != 0) {
                count += CountReferences(node->trycatch.catchblock, name);
            }
            if (node->trycatch.finallyblock) count += CountReferences(node->trycatch.finallyblock, name);
            break;
        case NTHROW:
            count += CountReferences(node->throwstmt.expr, name);
            break;
        case NCOALESCE:
            count += CountReferences(node->coalesce.left, name);
            count += CountReferences(node->coalesce.right, name);
            break;
        case NOPTCHAIN:
            count += CountReferences(node->optchain.obj, name);
            count += CountReferences(node->optchain.idx, name);
            break;
        case NSTRUCTINIT:
            if (node->structinit.spread) count += CountReferences(node->structinit.spread, name);
            for (int i = 0; i < node->structinit.fc; i++) {
                count += CountReferences(node->structinit.fvals[i], name);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < node->chainedcmp.count; i++) {
                count += CountReferences(node->chainedcmp.exprs[i], name);
            }
            break;
        case NRANGE:
            count += CountReferences(node->range.start, name);
            count += CountReferences(node->range.end, name);
            break;
        case NASSIGN:
            count += CountReferences(node->assign.target, name);
            count += CountReferences(node->assign.value, name);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            count += CountReferences(node->call.callee, name);
            for (int i = 0; i < node->call.argc; i++) {
                count += CountReferences(node->call.args[i], name);
            }
            break;
        case NINDEX:
            count += CountReferences(node->index.arr, name);
            count += CountReferences(node->index.idx, name);
            break;
        case NMEMBER:
            count += CountReferences(node->member.obj, name);
            break;
        case NARRLIT:
            for (int i = 0; i < node->arrlit.count; i++) {
                count += CountReferences(node->arrlit.elems[i], name);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < node->maplit.count; i++) {
                count += CountReferences(node->maplit.vals[i], name);
            }
            break;
        case NINC: case NDEC:
            count += CountReferences(node->incdec.expr, name);
            break;
        case NSWITCH:
            count += CountReferences(node->sw.expr, name);
            for (int i = 0; i < node->sw.casecount; i++) {
                count += CountReferences(node->sw.casevals[i], name);
                count += CountReferences(node->sw.casebodies[i], name);
            }
            if (node->sw.defaultbody) count += CountReferences(node->sw.defaultbody, name);
            break;
        case NYIELD:
            count += CountReferences(node->yieldstmt.expr, name);
            break;
        case NMULTIRETURN:
            for (int i = 0; i < node->multiret.count; i++) {
                count += CountReferences(node->multiret.exprs[i], name);
            }
            break;
        case NAWAIT:
            count += CountReferences(node->awaitn.expr, name);
            break;
        case NSPAWN:
            count += CountReferences(node->spawnn.expr, name);
            break;
        case NTRYPROPAGATE:
            count += CountReferences(node->single.expr, name);
            break;
        case NTUPLE:
            for (int i = 0; i < node->tuple.count; i++) {
                count += CountReferences(node->tuple.elems[i], name);
            }
            break;
        case NMATCH:
            count += CountReferences(node->match.expr, name);
            for (int i = 0; i < node->match.count; i++) {
                count += CountReferences(node->match.patterns[i], name);
                count += CountReferences(node->match.bodies[i], name);
                if (node->match.guards && node->match.guards[i]) {
                    count += CountReferences(node->match.guards[i], name);
                }
            }
            break;
        case NDESTRUCTARR:
            count += CountReferences(node->destructarr.value, name);
            break;
        case NDESTRUCTOBJ:
            count += CountReferences(node->destructobj.value, name);
            break;
        case NSPREAD:
            count += CountReferences(node->spread.expr, name);
            break;
        case NSELECT:
            for (int i = 0; i < node->selectn.count; i++) {
                count += CountReferences(node->selectn.cases[i], name);
                count += CountReferences(node->selectn.bodies[i], name);
            }
            if (node->selectn.defaultbody) {
                count += CountReferences(node->selectn.defaultbody, name);
            }
            break;
        case NTUPLEPAT:
            for (int i = 0; i < node->tuplepat.count; i++) {
                count += CountReferences(node->tuplepat.patterns[i], name);
            }
            break;
        case NSTRUCTPAT:
            for (int i = 0; i < node->structpat.count; i++) {
                count += CountReferences(node->structpat.patterns[i], name);
            }
            break;
        case NORPAT:
            count += CountReferences(node->orpat.left, name);
            count += CountReferences(node->orpat.right, name);
            break;
        case NCAST:
            count += CountReferences(node->cast.expr, name);
            break;
        default: break;
    }
    return count;
}

static int HasSideEffects(Node *node) {
    if (!node) return 0;
    switch (node->type) {
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
        case NNEW:
        case NINPUT:
        case NYIELD:
        case NAWAIT:
        case NSPAWN:
        case NTRYPROPAGATE:
        case NINC:
        case NDEC:
        case NASSIGN:
        case NTHROW:
            return 1;
        default:
            break;
    }
    if (node->type == NBINOP || node->type == NUNOP || node->type == NCOALESCE) {
        return HasSideEffects(node->binop.left) || HasSideEffects(node->binop.right);
    }
    if (node->type == NBLOCK || node->type == NPROGRAM) {
        for (int i = 0; i < node->block.count; i++) {
            if (HasSideEffects(node->block.stmts[i])) return 1;
        }
    }
    if (node->type == NIF) {
        return HasSideEffects(node->ifn.cond) || HasSideEffects(node->ifn.then) || (node->ifn.els && HasSideEffects(node->ifn.els));
    }
    if (node->type == NTERNARY) {
        return HasSideEffects(node->ternary.cond) || HasSideEffects(node->ternary.tval) || HasSideEffects(node->ternary.fval);
    }
    if (node->type == NWHILE) {
        return HasSideEffects(node->whilen.cond) || HasSideEffects(node->whilen.body);
    }
    if (node->type == NFOR) {
        return (node->forn.init && HasSideEffects(node->forn.init)) ||
               (node->forn.cond && HasSideEffects(node->forn.cond)) ||
               (node->forn.post && HasSideEffects(node->forn.post)) ||
               HasSideEffects(node->forn.body);
    }
    if (node->type == NFORIN) {
        return HasSideEffects(node->forin.iter) || HasSideEffects(node->forin.body);
    }
    if (node->type == NRETURN || node->type == NPRINT || node->type == NEXPRSTMT ||
        node->type == NIMPORT || node->type == NTRYPROPAGATE) {
        return node->single.expr ? HasSideEffects(node->single.expr) : 0;
    }
    if (node->type == NTRYCATCH) {
        return HasSideEffects(node->trycatch.tryblock) || HasSideEffects(node->trycatch.catchblock) || (node->trycatch.finallyblock && HasSideEffects(node->trycatch.finallyblock));
    }
    if (node->type == NOPTCHAIN) {
        return HasSideEffects(node->optchain.obj) || (node->optchain.idx && HasSideEffects(node->optchain.idx));
    }
    if (node->type == NRANGE) {
        return HasSideEffects(node->range.start) || HasSideEffects(node->range.end);
    }
    if (node->type == NINDEX) {
        return HasSideEffects(node->index.arr) || HasSideEffects(node->index.idx);
    }
    if (node->type == NMEMBER) {
        return HasSideEffects(node->member.obj);
    }
    if (node->type == NARRLIT) {
        for (int i = 0; i < node->arrlit.count; i++) {
            if (HasSideEffects(node->arrlit.elems[i])) return 1;
        }
    }
    if (node->type == NMAPLIT) {
        for (int i = 0; i < node->maplit.count; i++) {
            if (HasSideEffects(node->maplit.vals[i])) return 1;
        }
    }
    if (node->type == NSWITCH) {
        if (HasSideEffects(node->sw.expr)) return 1;
        for (int i = 0; i < node->sw.casecount; i++) {
            if (HasSideEffects(node->sw.casevals[i]) || HasSideEffects(node->sw.casebodies[i])) return 1;
        }
        if (node->sw.defaultbody && HasSideEffects(node->sw.defaultbody)) return 1;
    }
    if (node->type == NTUPLE) {
        for (int i = 0; i < node->tuple.count; i++) {
            if (HasSideEffects(node->tuple.elems[i])) return 1;
        }
    }
    if (node->type == NMATCH) {
        if (HasSideEffects(node->match.expr)) return 1;
        for (int i = 0; i < node->match.count; i++) {
            if (HasSideEffects(node->match.patterns[i]) || HasSideEffects(node->match.bodies[i])) return 1;
            if (node->match.guards && node->match.guards[i] && HasSideEffects(node->match.guards[i])) return 1;
        }
    }
    if (node->type == NCAST) {
        return HasSideEffects(node->cast.expr);
    }
    return 0;
}

Node *NodeDceOpt(Node *n) {
    if (!n) return NULL;
    switch (n->type) {
        case NBLOCK:
        case NPROGRAM: {
            for (int i = 0; i < n->block.count; i++) {
                n->block.stmts[i] = NodeDceOpt(n->block.stmts[i]);
            }
            int newcount = 0;
            int unreachable = 0;
            for (int i = 0; i < n->block.count; i++) {
                Node *stmt = n->block.stmts[i];
                if (!stmt) continue;
                if (unreachable) {
                    NodeFree(stmt);
                    n->block.stmts[i] = NULL;
                    continue;
                }
                if (stmt->type == NVARDECL || stmt->type == NCONSTDECL) {
                    const char *varname = stmt->decl.name;
                    int refs = 0;
                    for (int j = i + 1; j < n->block.count; j++) {
                        refs += CountReferences(n->block.stmts[j], varname);
                    }
                    if (refs == 0 && !HasSideEffects(stmt->decl.init)) {
                        NodeFree(stmt);
                        n->block.stmts[i] = NULL;
                        continue;
                    }
                } else if (stmt->type == NFNDECL) {
                    const char *fnname = stmt->fn.name;
                    if (n->type == NPROGRAM || strchr(fnname, '.')) {
                        // Keep top-level functions and methods!
                        n->block.stmts[newcount++] = stmt;
                        continue;
                    }
                    int refs = 0;
                    for (int j = 0; j < n->block.count; j++) {
                        if (i == j) continue;
                        refs += CountReferences(n->block.stmts[j], fnname);
                    }
                    if (refs == 0 && !strstr(fnname, ".drop")) {
                        NodeFree(stmt);
                        n->block.stmts[i] = NULL;
                        continue;
                    }
                }
                n->block.stmts[newcount++] = stmt;
                if (stmt->type == NRETURN || stmt->type == NTHROW || stmt->type == NBREAK || stmt->type == NCONTINUE) {
                    unreachable = 1;
                }
            }
            n->block.count = newcount;
            break;
        }
        case NFNDECL:
            if (!n->fn.isgen && !n->fn.isasync)
                n->fn.body = NodeDceOpt(n->fn.body);
            break;
        case NIF:
            n->ifn.then = NodeDceOpt(n->ifn.then);
            if (n->ifn.els) n->ifn.els = NodeDceOpt(n->ifn.els);
            break;
        case NTERNARY:
            n->ternary.cond = NodeDceOpt(n->ternary.cond);
            n->ternary.tval = NodeDceOpt(n->ternary.tval);
            n->ternary.fval = NodeDceOpt(n->ternary.fval);
            break;
        case NWHILE:
            n->whilen.body = NodeDceOpt(n->whilen.body);
            break;
        case NFOR:
            n->forn.body = NodeDceOpt(n->forn.body);
            break;
        case NTRYCATCH:
            n->trycatch.tryblock = NodeDceOpt(n->trycatch.tryblock);
            n->trycatch.catchblock = NodeDceOpt(n->trycatch.catchblock);
            if (n->trycatch.finallyblock) n->trycatch.finallyblock = NodeDceOpt(n->trycatch.finallyblock);
            break;
        case NSWITCH:
            for (int i = 0; i < n->sw.casecount; i++) {
                n->sw.casebodies[i] = NodeDceOpt(n->sw.casebodies[i]);
            }
            if (n->sw.defaultbody) n->sw.defaultbody = NodeDceOpt(n->sw.defaultbody);
            break;
        case NMATCH:
            for (int i = 0; i < n->match.count; i++) {
                n->match.bodies[i] = NodeDceOpt(n->match.bodies[i]);
            }
            break;
        default:
            break;
    }
    return n;
}

static int IsPureReadFn(Node *callee) {
    if (!callee || callee->type != NIDENT || !callee->sval) return 0;
    const char *fnname = callee->sval;
    return (strcmp(fnname, "len") == 0 || strcmp(fnname, "Length") == 0 ||
            strcmp(fnname, "print") == 0 || strcmp(fnname, "println") == 0 ||
            strcmp(fnname, "assert") == 0 || strcmp(fnname, "Assert") == 0 ||
            strcmp(fnname, "type") == 0 || strcmp(fnname, "TypeName") == 0 ||
            strcmp(fnname, "typeof") == 0 || strcmp(fnname, "sizeof") == 0);
}

static int ChecksEscape(Node *node, const char *name) {
    if (!node) return 0;
    if (node->type == NRETURN) {
        if (node->single.expr && CountReferences(node->single.expr, name) > 0) {
            return 1;
        }
    }
    if (node->type == NTHROW) {
        if (node->throwstmt.expr && CountReferences(node->throwstmt.expr, name) > 0) {
            return 1;
        }
    }
    if (node->type == NASSIGN) {
        if (CountReferences(node->assign.value, name) > 0) {
            return 1;
        }
    }
    if (node->type == NVARDECL || node->type == NCONSTDECL) {
        if (node->decl.init && CountReferences(node->decl.init, name) > 0) {
            return 1;
        }
    }
    if (node->type == NCALL || node->type == NMACROCALL || node->type == NTEMPLATECALL) {
        if (!IsPureReadFn(node->call.callee)) {
            for (int i = 0; i < node->call.argc; i++) {
                if (CountReferences(node->call.args[i], name) > 0) {
                    return 1;
                }
            }
        }
    }
    if (node->type == NSPAWN) {
        if (CountReferences(node->spawnn.expr, name) > 0) {
            return 1;
        }
    }
    if (node->type == NYIELD) {
        if (CountReferences(node->yieldstmt.expr, name) > 0) {
            return 1;
        }
    }
    switch (node->type) {
        case NBINOP: case NUNOP:
            return ChecksEscape(node->binop.left, name) || ChecksEscape(node->binop.right, name);
        case NVARDECL: case NCONSTDECL:
            return ChecksEscape(node->decl.init, name) || ChecksEscape(node->decl.isconst, name);
        case NFNDECL:
        case NMACRODECL:
            for (int i = 0; i < node->fn.pc; i++) {
                if (strcmp(node->fn.pnames[i], name) == 0) return 0;
            }
            if (node->fn.pdefaults) {
                for (int i = 0; i < node->fn.pc; i++) {
                    if (ChecksEscape(node->fn.pdefaults[i], name)) return 1;
                }
            }
            return ChecksEscape(node->fn.body, name);
        case NBLOCK: case NPROGRAM:
            for (int i = 0; i < node->block.count; i++) {
                if (ChecksEscape(node->block.stmts[i], name)) return 1;
            }
            break;
        case NIF:
            return ChecksEscape(node->ifn.cond, name) || ChecksEscape(node->ifn.then, name) || ChecksEscape(node->ifn.els, name);
        case NTERNARY:
            return ChecksEscape(node->ternary.cond, name) || ChecksEscape(node->ternary.tval, name) || ChecksEscape(node->ternary.fval, name);
        case NWHILE:
            return ChecksEscape(node->whilen.cond, name) || ChecksEscape(node->whilen.body, name);
        case NFOR:
            return ChecksEscape(node->forn.init, name) || ChecksEscape(node->forn.cond, name) || ChecksEscape(node->forn.post, name) || ChecksEscape(node->forn.body, name);
        case NFORIN:
            if (strcmp(node->forin.var, name) == 0) {
                return ChecksEscape(node->forin.iter, name);
            }
            return ChecksEscape(node->forin.iter, name) || ChecksEscape(node->forin.body, name);
        case NRETURN: case NPRINT: case NEXPRSTMT: case NIMPORT: case NMODULE:
            return ChecksEscape(node->single.expr, name);
        case NUSE:
            return 0;
        case NTRYCATCH:
            if (ChecksEscape(node->trycatch.tryblock, name)) return 1;
            if (node->trycatch.errvar && strcmp(node->trycatch.errvar, name) == 0) {
                if (node->trycatch.finallyblock) return ChecksEscape(node->trycatch.finallyblock, name);
                return 0;
            }
            return ChecksEscape(node->trycatch.catchblock, name) || (node->trycatch.finallyblock && ChecksEscape(node->trycatch.finallyblock, name));
        case NTHROW:
            return ChecksEscape(node->throwstmt.expr, name);
        case NCOALESCE:
            return ChecksEscape(node->coalesce.left, name) || ChecksEscape(node->coalesce.right, name);
        case NOPTCHAIN:
            return ChecksEscape(node->optchain.obj, name) || ChecksEscape(node->optchain.idx, name);
        case NSTRUCTINIT:
            if (node->structinit.spread && ChecksEscape(node->structinit.spread, name)) return 1;
            for (int i = 0; i < node->structinit.fc; i++) {
                if (ChecksEscape(node->structinit.fvals[i], name)) return 1;
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < node->chainedcmp.count; i++) {
                if (ChecksEscape(node->chainedcmp.exprs[i], name)) return 1;
            }
            break;
        case NRANGE:
            return ChecksEscape(node->range.start, name) || ChecksEscape(node->range.end, name);
        case NASSIGN:
            return ChecksEscape(node->assign.target, name) || ChecksEscape(node->assign.value, name);
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            if (ChecksEscape(node->call.callee, name)) return 1;
            if (!IsPureReadFn(node->call.callee)) {
                for (int i = 0; i < node->call.argc; i++) {
                    if (ChecksEscape(node->call.args[i], name)) return 1;
                }
            }
            break;
        case NINDEX:
            return ChecksEscape(node->index.arr, name) || ChecksEscape(node->index.idx, name);
        case NMEMBER:
            return ChecksEscape(node->member.obj, name);
        case NARRLIT:
            for (int i = 0; i < node->arrlit.count; i++) {
                if (ChecksEscape(node->arrlit.elems[i], name)) return 1;
            }
            break;
        case NTUPLE:
            for (int i = 0; i < node->tuple.count; i++) {
                if (ChecksEscape(node->tuple.elems[i], name)) return 1;
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < node->maplit.count; i++) {
                if (ChecksEscape(node->maplit.vals[i], name)) return 1;
            }
            break;
        case NINC: case NDEC:
            return ChecksEscape(node->incdec.expr, name);
        case NSWITCH:
            if (ChecksEscape(node->sw.expr, name)) return 1;
            for (int i = 0; i < node->sw.casecount; i++) {
                if (ChecksEscape(node->sw.casevals[i], name) || ChecksEscape(node->sw.casebodies[i], name)) return 1;
            }
            if (node->sw.defaultbody && ChecksEscape(node->sw.defaultbody, name)) return 1;
            break;
        case NYIELD:
            return ChecksEscape(node->yieldstmt.expr, name);
        case NMULTIRETURN:
            for (int i = 0; i < node->multiret.count; i++) {
                if (ChecksEscape(node->multiret.exprs[i], name)) return 1;
            }
            break;
        case NAWAIT:
            return ChecksEscape(node->awaitn.expr, name);
        case NSPAWN:
            return ChecksEscape(node->spawnn.expr, name);
        case NTRYPROPAGATE:
            return ChecksEscape(node->single.expr, name);
        case NMATCH:
            if (ChecksEscape(node->match.expr, name)) return 1;
            for (int i = 0; i < node->match.count; i++) {
                if (ChecksEscape(node->match.patterns[i], name) || ChecksEscape(node->match.bodies[i], name)) return 1;
                if (node->match.guards && node->match.guards[i] && ChecksEscape(node->match.guards[i], name)) return 1;
            }
            break;
        default: break;
    }
    return 0;
}

static void NodeEscapeAnalysisHelper(Node *n, int in_loop) {
    if (!n) return;
    if (n->type == NBLOCK || n->type == NPROGRAM) {
        for (int i = 0; i < n->block.count; i++) {
            Node *stmt = n->block.stmts[i];
            if (stmt && (stmt->type == NVARDECL || stmt->type == NCONSTDECL)) {
                Node *init = stmt->decl.init;
                if (init && (init->type == NARRLIT || init->type == NNEW || init->type == NMAPLIT || init->type == NTUPLE || init->type == NSTRUCTINIT)) {
                    int escapes = 0;
                    for (int j = 0; j < n->block.count; j++) {
                        if (ChecksEscape(n->block.stmts[j], stmt->decl.name)) {
                            escapes = 1;
                            break;
                        }
                    }
                    if (!escapes && n->type == NBLOCK && !in_loop) {
                        init->isstack = 1;
                        stmt->is_static_safe = 1;
                        init->is_static_safe = 1;
                    }
                }
            } else if (stmt && stmt->type == NEXPRSTMT) {
                stmt->is_static_safe = 1;
                if (stmt->single.expr) stmt->single.expr->is_static_safe = 1;
            }
            NodeEscapeAnalysisHelper(stmt, in_loop);
        }
        return;
    }
    switch (n->type) {
        case NFNDECL:
            NodeEscapeAnalysisHelper(n->fn.body, 0);
            break;
        case NIF:
            NodeEscapeAnalysisHelper(n->ifn.then, in_loop);
            if (n->ifn.els) NodeEscapeAnalysisHelper(n->ifn.els, in_loop);
            break;
        case NTERNARY:
            NodeEscapeAnalysisHelper(n->ternary.cond, in_loop);
            NodeEscapeAnalysisHelper(n->ternary.tval, in_loop);
            NodeEscapeAnalysisHelper(n->ternary.fval, in_loop);
            break;
        case NWHILE:
            NodeEscapeAnalysisHelper(n->whilen.body, 1);
            break;
        case NFOR:
            NodeEscapeAnalysisHelper(n->forn.body, 1);
            break;
        case NFORIN:
            NodeEscapeAnalysisHelper(n->forin.body, 1);
            break;
        case NTRYCATCH:
            NodeEscapeAnalysisHelper(n->trycatch.tryblock, in_loop);
            NodeEscapeAnalysisHelper(n->trycatch.catchblock, in_loop);
            if (n->trycatch.finallyblock) NodeEscapeAnalysisHelper(n->trycatch.finallyblock, in_loop);
            break;
        case NSWITCH:
            for (int i = 0; i < n->sw.casecount; i++) {
                NodeEscapeAnalysisHelper(n->sw.casebodies[i], in_loop);
            }
            if (n->sw.defaultbody) NodeEscapeAnalysisHelper(n->sw.defaultbody, in_loop);
            break;
        case NMATCH:
            for (int i = 0; i < n->match.count; i++) {
                NodeEscapeAnalysisHelper(n->match.bodies[i], in_loop);
            }
            break;
        default:
            break;
    }
}

void NodeEscapeAnalysis(Node *n) {
    NodeEscapeAnalysisHelper(n, 0);
}
