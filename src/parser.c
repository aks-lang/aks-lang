// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "ast.h"
#include "lexer.h"
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSER_MAX_RECURSION_DEPTH 512

typedef struct {
  Lexer lex;
  Token cur, peek;
  int err;
  int suppresserrors;
  int recursion_depth;
  int error_count;
} Parser;

static Node *ParseStmt(Parser *p);
static Node *ParserExpr(Parser *p);
static Node *ParseBlock(Parser *p);
static Node *ParsePrimary(Parser *p);
static Node *ParseDecoratedStmt(Parser *p);

static Token Consume(Parser *p) {
  Token old = p->cur;
  p->cur = p->peek;
  p->peek = LexNext(&p->lex);
  return old;
}
static int Check(Parser *p, TType t) { return p->cur.type == t; }
static int Check2(Parser *p, TType t) { return p->peek.type == t; }
static Token Expect(Parser *p, TType t) {
  if (!Check(p, t)) {
    if (!p->suppresserrors) {
      if (p->error_count < 50) {
        fprintf(
            stderr,
            "Syntax error on line %d: expected '%s' (%d) but found '%s' (%d)\n",
            p->cur.line, ToktypeName(t), t, ToktypeName(p->cur.type),
            p->cur.type);
      } else if (p->error_count == 50) {
        fprintf(stderr, "... (further syntax errors suppressed)\n");
      }
      p->error_count++;
    }
    p->err = 1;
  }
  return Consume(p);
}
static int Match(Parser *p, TType t) {
  if (!Check(p, t))
    return 0;
  Token old = Consume(p);
  TokFree(&old);
  return 1;
}

static void ParseSynchronize(Parser *p) {
  while (!Check(p, TEOF)) {
    if (p->cur.type == TSEMI) {
      Token semi = Consume(p);
      TokFree(&semi);
      return;
    }
    switch (p->cur.type) {
      case TFN:
      case TLET:
      case TCONST:
      case TIF:
      case TWHILE:
      case TFOR:
      case TRETURN:
      case TSTRUCT:
      case TENUM:
      case TIMPORT:
      case TTYPE:
      case TINTERFACE:
      case TRBRACE:
        return;
      default:
        break;
    }
    Token discard = Consume(p);
    TokFree(&discard);
  }
}

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} TypeStr;

static void TypeStrInit(TypeStr *ts) {
  ts->buf = malloc(16);
  ts->buf[0] = '\0';
  ts->len = 0;
  ts->cap = 16;
}

static void TypeStrAppend(TypeStr *ts, const char *s) {
  size_t slen = strlen(s);
  if (ts->len + slen + 1 > ts->cap) {
    while (ts->len + slen + 1 > ts->cap)
      ts->cap *= 2;
    ts->buf = realloc(ts->buf, ts->cap);
  }
  memcpy(ts->buf + ts->len, s, slen + 1);
  ts->len += slen;
}

static void TypeStrFree(TypeStr *ts) { free(ts->buf); }

static void ParseUnionType(Parser *p, TypeStr *ts);

static void ParseBaseType(Parser *p, TypeStr *ts) {
  if (Check(p, TAMPERSAND)) {
    Token amp = Consume(p);
    TokFree(&amp);
    TypeStrAppend(ts, "&");
    if (Check(p, TLIFETIME)) {
      Token life = Consume(p);
      TypeStrAppend(ts, life.val);
      TypeStrAppend(ts, " ");
      TokFree(&life);
    }
    if (Check(p, TMUT)) {
      Token mt = Consume(p);
      TypeStrAppend(ts, "mut ");
      TokFree(&mt);
    }
    ParseUnionType(p, ts);
    return;
  }
  if (Check(p, TLBRACKET)) {
    Token lb = Consume(p);
    TokFree(&lb);
    TypeStrAppend(ts, "[");
    ParseUnionType(p, ts);
    if (Check(p, TSEMI)) {
      Token sm = Consume(p);
      TokFree(&sm);
      TypeStrAppend(ts, "; ");
      if (Check(p, TINTLIT) || Check(p, TIDENT)) {
        Token len = Consume(p);
        TypeStrAppend(ts, len.val);
        TokFree(&len);
      }
    }
    Token rb = Expect(p, TRBRACKET);
    TokFree(&rb);
    TypeStrAppend(ts, "]");
    return;
  }
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p);
    TokFree(&lp);
    TypeStrAppend(ts, "(");
    int first = 1;
    while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
      if (!first) {
        Token cm = Expect(p, TCOMMA);
        TypeStrAppend(ts, ",");
        TokFree(&cm);
      }
      first = 0;
      ParseUnionType(p, ts);
    }
    Token rp = Expect(p, TRPAREN);
    TypeStrAppend(ts, ")");
    TokFree(&rp);
    return;
  }
  if (Check(p, TFN)) {
    Token tok = Consume(p);
    TypeStrAppend(ts, "fn");
    TokFree(&tok);

    Token lp = Expect(p, TLPAREN);
    TypeStrAppend(ts, "(");
    TokFree(&lp);

    while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
      ParseUnionType(p, ts);
      if (!Check(p, TRPAREN)) {
        Token cm = Expect(p, TCOMMA);
        TypeStrAppend(ts, ",");
        TokFree(&cm);
      }
    }
    Token rp = Expect(p, TRPAREN);
    TypeStrAppend(ts, ")");
    TokFree(&rp);

    Token arrow = Expect(p, TARROW);
    TypeStrAppend(ts, "->");
    TokFree(&arrow);

    ParseUnionType(p, ts);
  } else if ((Check(p, TIDENT) && strcmp(p->cur.val, "dyn") == 0)) {
    Token dyn = Consume(p);
    TypeStrAppend(ts, "dyn ");
    TokFree(&dyn);
    ParseUnionType(p, ts);
    return;
  } else if (Check(p, TIDENT) || Check(p, TINT) || Check(p, TFLOAT) ||
             Check(p, TSTR) || Check(p, TBOOL) || Check(p, TVOID) ||
             Check(p, TNULL) || Check(p, TINTLIT)) {
    Token tok = Consume(p);
    TypeStrAppend(ts, tok.val);
    TokFree(&tok);

    if (Check(p, TLT)) {
      Token lt = Consume(p);
      TypeStrAppend(ts, "<");
      TokFree(&lt);

      while (!Check(p, TGT) && !Check(p, TEOF)) {
        ParseUnionType(p, ts);
        if (!Check(p, TGT)) {
          Token cm = Expect(p, TCOMMA);
          TypeStrAppend(ts, ",");
          TokFree(&cm);
        }
      }
      Token gt = Expect(p, TGT);
      TypeStrAppend(ts, ">");
      TokFree(&gt);
    }
  } else {
    Token tok = Consume(p);
    if (!p->suppresserrors) {
      if (p->error_count < 50) {
        fprintf(stderr, "Syntax error on line %d: expected type but found '%s'\n",
                tok.line, tok.val);
      } else if (p->error_count == 50) {
        fprintf(stderr, "... (further syntax errors suppressed)\n");
      }
      p->error_count++;
    }
    TypeStrAppend(ts, tok.val);
    TokFree(&tok);
    p->err = 1;
  }
}

static void ParseIsectType(Parser *p, TypeStr *ts) {
  ParseBaseType(p, ts);
  while (Check(p, TAMPERSAND)) {
    Token amp = Consume(p);
    TypeStrAppend(ts, "&");
    TokFree(&amp);
    ParseBaseType(p, ts);
  }
}

static void ParseUnionType(Parser *p, TypeStr *ts) {
  ParseIsectType(p, ts);
  while (Check(p, TPIPE)) {
    Token pipetok = Consume(p);
    TypeStrAppend(ts, "|");
    TokFree(&pipetok);
    ParseIsectType(p, ts);
  }
}

static void ParseRefTypeSuffix(Parser *p, TypeStr *ts) {
  if (Check(p, TLBRACE)) {
    Token lb = Consume(p);
    TypeStrAppend(ts, "{");
    TokFree(&lb);
    int depth = 1;
    int first = 1;
    while (depth > 0 && !Check(p, TEOF)) {
      if (Check(p, TLBRACE)) {
        depth++;
      } else if (Check(p, TRBRACE)) {
        depth--;
        if (depth == 0) {
          Token rb = Consume(p);
          TypeStrAppend(ts, "}");
          TokFree(&rb);
          break;
        }
      }
      Token tok = Consume(p);
      if (!first && tok.type != TCOLON) {
        TypeStrAppend(ts, " ");
      }
      TypeStrAppend(ts, tok.val);
      TokFree(&tok);
      first = 0;
    }
  }
}

static char *ParseType(Parser *p) {
  TypeStr ts;
  TypeStrInit(&ts);
  ParseUnionType(p, &ts);
  // only consume { as refinement suffix if it looks like {ident: ...}
  // this disambiguates from function body braces
  if (Check(p, TLBRACE) && Check2(p, TIDENT)) {
    // save state and peek ahead to confirm ident followed by colon
    Parser saved = *p;
    if (saved.cur.val)
      saved.cur.val = strdup(saved.cur.val);
    if (saved.peek.val)
      saved.peek.val = strdup(saved.peek.val);
    Token lb = Consume(p);
    TokFree(&lb); // consume {
    Token id = Consume(p);
    TokFree(&id); // consume ident
    int isref = Check(p, TCOLON);
    // restore parser state
    TokFree(&p->cur);
    TokFree(&p->peek);
    *p = saved;
    if (isref) {
      ParseRefTypeSuffix(p, &ts);
    }
  }
  char *res = strdup(ts.buf);
  TypeStrFree(&ts);
  return res;
}

// bracket-aware scan for interpolation end
static const char *FindIntErpEnd(const char *s) {
  int depth = 0;
  while (*s) {
    if (*s == '{')
      depth++;
    else if (*s == '}') {
      if (depth == 0)
        return s;
      depth--;
    }
    s++;
  }
  return NULL;
}

static Node *ParseExprFromStr(const char *source, int line);

static Node *ParseInterpolatedStr(const char *str, int line) {
  const char *start = str;
  const char *p = strstr(start, "${");
  int brace_len = 2;
  if (!p) {
    p = strchr(start, '{');
    brace_len = 1;
  }
  if (!p) {
    Node *n = NodeNew(NSTR, line);
    n->sval = strdup(str);
    return n;
  }
  size_t prefixlen = p - start;
  char *prefix = malloc(prefixlen + 1);
  memcpy(prefix, start, prefixlen);
  prefix[prefixlen] = '\0';
  Node *prefixnode = NodeNew(NSTR, line);
  prefixnode->sval = prefix;

  const char *exprstart = p + brace_len;
  const char *exprend = FindIntErpEnd(exprstart);
  if (!exprend) {
    free(prefix);
    Node *n = NodeNew(NSTR, line);
    n->sval = strdup(str);
    return n;
  }
  size_t exprlen = exprend - exprstart;
  char *exprstr = malloc(exprlen + 1);
  memcpy(exprstr, exprstart, exprlen);
  exprstr[exprlen] = '\0';
  Node *exprnode = ParseExprFromStr(exprstr, line);
  free(exprstr);

  Node *add1 = NodeNew(NBINOP, line);
  add1->binop.op = strdup("+");
  add1->binop.left = prefixnode;
  add1->binop.right = exprnode;

  Node *suffixnode = ParseInterpolatedStr(exprend + 1, line);
  Node *add2 = NodeNew(NBINOP, line);
  add2->binop.op = strdup("+");
  add2->binop.left = add1;
  add2->binop.right = suffixnode;
  return add2;
}

static Node *ParseExprFromStr(const char *source, int line) {
  Parser p;
  p.lex = LexNew(source);
  p.err = 0;
  p.suppresserrors = 0;
  p.cur = LexNext(&p.lex);
  p.peek = LexNext(&p.lex);
  Node *expr = ParserExpr(&p);
  TokFree(&p.cur);
  TokFree(&p.peek);
  return expr;
}

static Node *ParseBlock(Parser *p) {
  int line = p->cur.line;
  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);
  Node *blk = NodeNew(NBLOCK, line);
  int cap = 8;
  blk->block.stmts = malloc(cap * sizeof(Node *));
  blk->block.count = 0;
  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    Node *s = ParseStmt(p);
    if (!s) {
      ParseSynchronize(p);
      continue;
    }
    if (blk->block.count == cap) {
      cap *= 2;
      blk->block.stmts = realloc(blk->block.stmts, cap * sizeof(Node *));
    }
    blk->block.stmts[blk->block.count++] = s;
  }
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);
  return blk;
}

static Node *ParseVar(Parser *p, int isconst) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  if (Check(p, TMUT)) {
    Token mt = Consume(p);
    TokFree(&mt);
  }

  // tuple destructure: let (a, b) = expr;
  if (Check(p, TLPAREN)) {
    Token lb = Consume(p);
    TokFree(&lb);
    int cap = 4, count = 0;
    char **names = malloc(cap * sizeof(char *));
    while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
      if (count == cap) {
        cap *= 2;
        names = realloc(names, cap * sizeof(char *));
      }
      Token nm = Expect(p, TIDENT);
      names[count++] = strdup(nm.val);
      TokFree(&nm);
      if (!Check(p, TRPAREN)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token rb = Expect(p, TRPAREN);
    TokFree(&rb);
    Token eq = Expect(p, TEQ);
    TokFree(&eq);
    Node *val = ParserExpr(p);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NDESTRUCTARR, line);
    n->destructarr.names = names;
    n->destructarr.count = count;
    n->destructarr.value = val;
    return n;
  }

  // array destructure: let [a, b] = expr;
  if (Check(p, TLBRACKET)) {
    Token lb = Consume(p);
    TokFree(&lb);
    int cap = 4, count = 0;
    char **names = malloc(cap * sizeof(char *));
    while (!Check(p, TRBRACKET) && !Check(p, TEOF)) {
      if (count == cap) {
        cap *= 2;
        names = realloc(names, cap * sizeof(char *));
      }
      Token nm = Expect(p, TIDENT);
      names[count++] = strdup(nm.val);
      TokFree(&nm);
      if (!Check(p, TRBRACKET)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token rb = Expect(p, TRBRACKET);
    TokFree(&rb);
    Token eq = Expect(p, TEQ);
    TokFree(&eq);
    Node *val = ParserExpr(p);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NDESTRUCTARR, line);
    n->destructarr.names = names;
    n->destructarr.count = count;
    n->destructarr.value = val;
    return n;
  }

  // object destructure: let {x, y} = expr;
  if (Check(p, TLBRACE)) {
    Token lb = Consume(p);
    TokFree(&lb);
    int cap = 4, count = 0;
    char **keys = malloc(cap * sizeof(char *));
    char **aliases = malloc(cap * sizeof(char *));
    while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
      if (count == cap) {
        cap *= 2;
        keys = realloc(keys, cap * sizeof(char *));
        aliases = realloc(aliases, cap * sizeof(char *));
      }
      Token nm = Expect(p, TIDENT);
      keys[count] = strdup(nm.val);
      TokFree(&nm);
      if (Match(p, TCOLON)) {
        Token alias = Expect(p, TIDENT);
        aliases[count] = strdup(alias.val);
        TokFree(&alias);
      } else
        aliases[count] = strdup(keys[count]);
      count++;
      if (!Check(p, TRBRACE)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token rb = Expect(p, TRBRACE);
    TokFree(&rb);
    Token eq = Expect(p, TEQ);
    TokFree(&eq);
    Node *val = ParserExpr(p);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NDESTRUCTOBJ, line);
    n->destructobj.keys = keys;
    n->destructobj.aliases = aliases;
    n->destructobj.count = count;
    n->destructobj.value = val;
    return n;
  }

  char *name = NULL;
  if (Check(p, TIDENT) || Check(p, TACTOR) || (p->cur.val && p->cur.type >= 0)) {
    Token nm = Consume(p);
    name = strdup(nm.val);
    TokFree(&nm);
  } else {
    Token nm = Expect(p, TIDENT);
    name = strdup(nm.val);
    TokFree(&nm);
  }
  char *tname = NULL;
  if (Match(p, TCOLON))
    tname = ParseType(p);
  Node *init = NULL;
  if (Match(p, TEQ))
    init = ParserExpr(p);
  Token sc = Expect(p, TSEMI);
  TokFree(&sc);
  Node *n = NodeNew(isconst ? NCONSTDECL : NVARDECL, line);
  n->decl.name = name;
  n->decl.tname = tname;
  n->decl.init = init;
  n->decl.isconstflag = isconst;
  return n;
}

static Node *ParseFnBody(Parser *p, int line, char *fname) {
  Token lp = Expect(p, TLPAREN);
  TokFree(&lp);
  int pc = 0, pcap = 4, isvariadic = 0;
  char **pnames = malloc(pcap * sizeof(char *));
  char **ptypes = malloc(pcap * sizeof(char *));
  Node **pdefaults = malloc(pcap * sizeof(Node *));
  while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
    // variadic ...args
    if (Check(p, TDOTDOTDOT)) {
      Token dt = Consume(p);
      TokFree(&dt);
      isvariadic = 1;
    }
    if (pc == pcap) {
      pcap *= 2;
      pnames = realloc(pnames, pcap * sizeof(char *));
      ptypes = realloc(ptypes, pcap * sizeof(char *));
      pdefaults = realloc(pdefaults, pcap * sizeof(Node *));
    }
    Token pn = Expect(p, TIDENT);
    pnames[pc] = strdup(pn.val);
    TokFree(&pn);
    ptypes[pc] = NULL;
    if (Match(p, TCOLON))
      ptypes[pc] = ParseType(p);
    pdefaults[pc] = NULL;
    if (Match(p, TEQ))
      pdefaults[pc] = ParserExpr(p);
    pc++;
    if (isvariadic)
      break; // rest param must be last
    if (!Check(p, TRPAREN)) {
      Token cm = Expect(p, TCOMMA);
      TokFree(&cm);
    }
  }
  Token rp = Expect(p, TRPAREN);
  TokFree(&rp);
  char *ret = NULL;
  if (Match(p, TARROW))
    ret = ParseType(p);
  char *effect = NULL;
  if (Check(p, TNOT)) {
    Token nt = Consume(p);
    TokFree(&nt);
    Token eff = Expect(p, TIDENT);
    effect = strdup(eff.val);
    TokFree(&eff);
  }
  Node **requires = NULL;
  int req_count = 0;
  Node **ensures = NULL;
  int ens_count = 0;
  while (Check(p, TREQUIRES) || Check(p, TENSURES) ||
         (Check(p, TIDENT) && (!strcmp(p->cur.val, "req") || !strcmp(p->cur.val, "ens") ||
                               !strcmp(p->cur.val, "requires") || !strcmp(p->cur.val, "ensures")))) {
      int is_req = (Check(p, TREQUIRES) || (Check(p, TIDENT) && (!strcmp(p->cur.val, "req") || !strcmp(p->cur.val, "requires"))));
      Token kw = Consume(p);
      TokFree(&kw);
      Node *expr = NULL;
      if (Check(p, TLBRACE)) {
          Token lb = Consume(p);
          TokFree(&lb);
          expr = ParserExpr(p);
          Token rb = Expect(p, TRBRACE);
          TokFree(&rb);
      } else {
          expr = ParserExpr(p);
      }
      if (is_req) {
          requires = realloc(requires, (req_count + 1) * sizeof(Node *));
          requires[req_count++] = expr;
      } else {
          ensures = realloc(ensures, (ens_count + 1) * sizeof(Node *));
          ensures[ens_count++] = expr;
      }
  }
  if (Check(p, TWHERE) || (Check(p, TIDENT) && strcmp(p->cur.val, "where") == 0)) {
    Token wh = Consume(p);
    TokFree(&wh);
    while ((Check(p, TIDENT) || Check(p, TLIFETIME)) && !Check(p, TLBRACE) && !Check(p, TEOF)) {
      Token param = Consume(p);
      TokFree(&param);
      if (Check(p, TCOLON)) {
        Token col = Consume(p);
        TokFree(&col);
        char *btype = ParseType(p);
        free(btype);
        while (Check(p, TPLUS)) {
          Token pl = Consume(p);
          TokFree(&pl);
          char *b2 = ParseType(p);
          free(b2);
        }
      }
      if (Check(p, TCOMMA)) {
        Token cm = Consume(p);
        TokFree(&cm);
      } else {
        break;
      }
    }
  }
  Node *body = NULL;
  if (Check(p, TFATARROW)) {
    Token fa = Consume(p); TokFree(&fa);
    Node *expr = ParserExpr(p);
    Node *retnode = NodeNew(NRETURN, line);
    retnode->single.expr = expr;
    body = NodeNew(NBLOCK, line);
    body->block.count = 1;
    body->block.stmts = malloc(sizeof(Node *));
    body->block.stmts[0] = retnode;
  } else {
    body = ParseBlock(p);
  }
  Node *n = NodeNew(NFNDECL, line);
  n->fn.name = fname;
  n->fn.ret = ret;
  n->fn.pnames = pnames;
  n->fn.ptypes = ptypes;
  n->fn.pdefaults = pdefaults;
  n->fn.pc = pc;
  n->fn.isvariadic = isvariadic;
  n->fn.requires = requires;
  n->fn.req_count = req_count;
  n->fn.ensures = ensures;
  n->fn.ens_count = ens_count;
  n->fn.body = body;
  n->fn.effect = effect;
  return n;
}

static Node *ParseFn(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  int isgen = 0;
  if (Check(p, TSTAR)) {
    Token star = Consume(p);
    TokFree(&star);
    isgen = 1;
  }
  Token nm;
  if (Check(p, TIDENT) || Check(p, TACTOR) || (p->cur.val && p->cur.type >= 0)) {
    nm = Consume(p);
  } else {
    nm = Expect(p, TIDENT);
  }
  char *fname;
  if (Check(p, TDOT)) {
    Token dot = Consume(p);
    TokFree(&dot);
    Token sub;
    if (Check(p, TIDENT) || Check(p, TACTOR) || (p->cur.val && p->cur.type >= 0)) {
      sub = Consume(p);
    } else {
      sub = Expect(p, TIDENT);
    }
    size_t len = strlen(nm.val) + 1 + strlen(sub.val) + 1;
    fname = malloc(len);
    snprintf(fname, len, "%s.%s", nm.val, sub.val);
    TokFree(&sub);
  } else {
    fname = strdup(nm.val);
  }
  TokFree(&nm);
  char **tparams = NULL;
  int tpc = 0;
  if (Check(p, TLT)) {
    Token lt = Consume(p);
    TokFree(&lt);
    int cap = 4;
    tparams = malloc(cap * sizeof(char *));
    while (!Check(p, TGT) && !Check(p, TEOF)) {
      if (tpc == cap) {
        cap *= 2;
        tparams = realloc(tparams, cap * sizeof(char *));
      }
      Token param = (Check(p, TLIFETIME) || Check(p, TIDENT)) ? Consume(p) : Expect(p, TIDENT);
      tparams[tpc++] = strdup(param.val);
      TokFree(&param);
      if (Check(p, TCOLON)) {
        Token col = Consume(p);
        TokFree(&col);
        Token bound = Expect(p, TIDENT);
        TokFree(&bound);
        while (Check(p, TPLUS)) {
          Token pl = Consume(p);
          TokFree(&pl);
          Token b2 = Expect(p, TIDENT);
          TokFree(&b2);
        }
      }
      if (!Check(p, TGT)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token gt = Expect(p, TGT);
    TokFree(&gt);
  }
  Node *fndecl = ParseFnBody(p, line, fname);
  fndecl->fn.tparams = tparams;
  fndecl->fn.tpc = tpc;
  fndecl->fn.isgen = isgen;
  return fndecl;
}

static Node *ParseIf(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p); TokFree(&kw);
  Node *cond = NULL;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p); TokFree(&lp);
    cond = ParserExpr(p);
    Token rp = Expect(p, TRPAREN); TokFree(&rp);
  } else {
    cond = ParserExpr(p);
  }
  Node *then = ParseBlock(p);
  Node *els = NULL;
  if (Match(p, TELSE)) {
    if (Check(p, TIF))
      els = ParseIf(p);
    else
      els = ParseBlock(p);
  }
  Node *n = NodeNew(NIF, line);
  n->ifn.cond = cond;
  n->ifn.then = then;
  n->ifn.els = els;
  return n;
}

static Node *ParseWhile(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p); TokFree(&kw);
  Node *cond = NULL;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p); TokFree(&lp);
    cond = ParserExpr(p);
    Token rp = Expect(p, TRPAREN); TokFree(&rp);
  } else {
    cond = ParserExpr(p);
  }
  Node *body = ParseBlock(p);
  Node *n = NodeNew(NWHILE, line);
  n->whilen.cond = cond;
  n->whilen.body = body;
  return n;
}

static Node *ParseFor(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p); TokFree(&kw);
  int has_paren = 0;
  if (Check(p, TLPAREN)) {
    has_paren = 1;
    Token lp = Consume(p); TokFree(&lp);
  }

  // for (item in iterable) or for item in iterable
  if ((Check(p, TIDENT) || Check(p, TLET)) &&
      (Check2(p, TIN) || (p->cur.type == TIDENT && p->peek.type == TIN))) {
    char *var = NULL;
    if (Check(p, TLET)) {
      Token kw2 = Consume(p); TokFree(&kw2);
    }
    Token vt = Expect(p, TIDENT);
    var = strdup(vt.val); TokFree(&vt);
    Token inkw = Expect(p, TIN); TokFree(&inkw);
    Node *iter = ParserExpr(p);
    if (has_paren) {
      Token rp = Expect(p, TRPAREN); TokFree(&rp);
    }
    Node *body = ParseBlock(p);
    Node *n = NodeNew(NFORIN, line);
    n->forin.var = var;
    n->forin.iter = iter;
    n->forin.body = body;
    return n;
  }

  Node *init = NULL;
  if (!Check(p, TSEMI)) {
    if (Check(p, TLET))
      init = ParseVar(p, 0);
    else {
      int l2 = p->cur.line;
      Node *e = ParserExpr(p);
      Token sc = Expect(p, TSEMI);
      TokFree(&sc);
      init = NodeNew(NEXPRSTMT, l2);
      init->single.expr = e;
    }
  } else {
    Token sc = Consume(p);
    TokFree(&sc);
  }
  Node *cond = NULL;
  if (!Check(p, TSEMI))
    cond = ParserExpr(p);
  Token sc = Expect(p, TSEMI);
  TokFree(&sc);
  Node *post = NULL;
  if (!Check(p, TRPAREN))
    post = ParserExpr(p);
  Token rp = Expect(p, TRPAREN);
  TokFree(&rp);
  Node **invariants = NULL;
  int inv_count = 0;
  while (Match(p, TINVARIANT)) {
      invariants = realloc(invariants, (inv_count + 1) * sizeof(Node *));
      invariants[inv_count++] = ParserExpr(p);
  }
  Node *body = ParseBlock(p);
  Node *n = NodeNew(NFOR, line);
  n->forn.init = init;
  n->forn.cond = cond;
  n->forn.post = post;
  n->forn.invariants = invariants;
  n->forn.inv_count = inv_count;
  n->forn.body = body;
  return n;
}

static Node *ParseStruct(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  Token nm = Expect(p, TIDENT);
  char *sname = strdup(nm.val);
  TokFree(&nm);

  char **tparams = NULL;
  int tpc = 0;
  if (Check(p, TLT)) {
    Token lt = Consume(p);
    TokFree(&lt);
    int tpcap = 4;
    tparams = malloc(tpcap * sizeof(char *));
    while (!Check(p, TGT) && !Check(p, TEOF)) {
      Token tp = Expect(p, TIDENT);
      if (tpc == tpcap) {
        tpcap *= 2;
        tparams = realloc(tparams, tpcap * sizeof(char *));
      }
      tparams[tpc++] = strdup(tp.val);
      TokFree(&tp);
      if (!Check(p, TGT)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token gt = Expect(p, TGT);
    TokFree(&gt);
  }

  char **interfaces = NULL;
  int ic = 0;
  if (Check(p, TIMPLEMENTS)) {
    Token impl = Consume(p);
    TokFree(&impl);
    int icap = 4;
    interfaces = malloc(icap * sizeof(char *));
    while (!Check(p, TLBRACE) && !Check(p, TEOF)) {
      Token it = Expect(p, TIDENT);
      if (ic == icap) {
        icap *= 2;
        interfaces = realloc(interfaces, icap * sizeof(char *));
      }
      interfaces[ic++] = strdup(it.val);
      TokFree(&it);
      if (!Check(p, TLBRACE)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
  }

  char **constraintparams = NULL;
  char **constrainttypes = NULL;
  int constraintcount = 0;
  if (Check(p, TWHERE)) {
    Token wh = Consume(p);
    TokFree(&wh);
    int ccap = 4;
    constraintparams = malloc(ccap * sizeof(char *));
    constrainttypes = malloc(ccap * sizeof(char *));
    while (!Check(p, TLBRACE) && !Check(p, TEOF)) {
      Token param = Expect(p, TIDENT);
      Token col = Expect(p, TCOLON);
      TokFree(&col);
      char *bound = ParseType(p);

      if (constraintcount == ccap) {
        ccap *= 2;
        constraintparams = realloc(constraintparams, ccap * sizeof(char *));
        constrainttypes = realloc(constrainttypes, ccap * sizeof(char *));
      }
      constraintparams[constraintcount] = strdup(param.val);
      constrainttypes[constraintcount] = bound;
      constraintcount++;
      TokFree(&param);

      if (!Check(p, TLBRACE) && Check(p, TCOMMA)) {
        Token cm = Consume(p);
        TokFree(&cm);
      }
    }
  }

  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);
  int fc = 0, fcap = 4;
  char **fnames = malloc(fcap * sizeof(char *));
  char **ftypes = malloc(fcap * sizeof(char *));
  int mcount = 0, mcap = 4;
  Node **methods = malloc(mcap * sizeof(Node *));
  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (Check(p, TFN)) {
      Token fnkw = Consume(p); TokFree(&fnkw);
      Token mname = Expect(p, TIDENT);
      size_t len = strlen(sname) + 1 + strlen(mname.val) + 1;
      char *fullname = malloc(len);
      snprintf(fullname, len, "%s.%s", sname, mname.val);
      TokFree(&mname);
      Node *fndecl = ParseFnBody(p, p->cur.line, fullname);
      if (mcount == mcap) {
        mcap *= 2;
        methods = realloc(methods, mcap * sizeof(Node *));
      }
      methods[mcount++] = fndecl;
      if (Check(p, TSEMI)) {
        Token sc = Consume(p); TokFree(&sc);
      } else if (Check(p, TCOMMA)) {
        Token cm = Consume(p); TokFree(&cm);
      }
      continue;
    }
    Token fn = Expect(p, TIDENT);
    char *fname = fn.val ? strdup(fn.val) : strdup("");
    TokFree(&fn);
    Token co = Expect(p, TCOLON);
    TokFree(&co);
    char *ftype = ParseType(p);
    if (Check(p, TSEMI)) {
      Token sc = Consume(p); TokFree(&sc);
    } else if (Check(p, TCOMMA)) {
      Token cm = Consume(p); TokFree(&cm);
    }
    if (fc == fcap) {
      fcap *= 2;
      fnames = realloc(fnames, fcap * sizeof(char *));
      ftypes = realloc(ftypes, fcap * sizeof(char *));
    }
    fnames[fc] = fname;
    ftypes[fc] = ftype;
    fc++;
  }
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);
  Node *n = NodeNew(NSTRUCTDECL, line);
  n->strct.name = sname;
  n->strct.fnames = fnames;
  n->strct.ftypes = ftypes;
  n->strct.fc = fc;
  n->strct.tparams = tparams;
  n->strct.tpc = tpc;
  n->strct.interfaces = interfaces;
  n->strct.ic = ic;
  n->strct.constraintparams = constraintparams;
  n->strct.constrainttypes = constrainttypes;
  n->strct.constraintcount = constraintcount;

  if (mcount > 0) {
    Node *block = NodeNew(NBLOCK, line);
    block->block.count = 1 + mcount;
    block->block.stmts = malloc((1 + mcount) * sizeof(Node *));
    block->block.stmts[0] = n;
    for (int i = 0; i < mcount; i++) {
      block->block.stmts[1 + i] = methods[i];
    }
    free(methods);
    return block;
  }
  free(methods);
  return n;
}

static Node *ParseActor(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  Token nm = Expect(p, TIDENT);
  char *aname = strdup(nm.val);
  TokFree(&nm);

  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);
  int fc = 0, fcap = 4;
  char **fnames = malloc(fcap * sizeof(char *));
  char **ftypes = malloc(fcap * sizeof(char *));
  Node **fdefaults = malloc(fcap * sizeof(Node *));
  int mcount = 0, mcap = 4;
  Node **methods = malloc(mcap * sizeof(Node *));

  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (Check(p, TFN) || Check(p, TASYNC)) {
      int is_async = 0;
      if (Check(p, TASYNC)) {
        Token as = Consume(p); TokFree(&as);
        is_async = 1;
      }
      Token fnkw = Expect(p, TFN); TokFree(&fnkw);
      Token mname = Expect(p, TIDENT);
      size_t len = strlen(aname) + 1 + strlen(mname.val) + 1;
      char *fullname = malloc(len);
      snprintf(fullname, len, "%s.%s", aname, mname.val);
      TokFree(&mname);
      Node *fndecl = ParseFnBody(p, p->cur.line, fullname);
      if (is_async) fndecl->fn.isasync = 1;
      if (fndecl->fn.pc == 0 || strcmp(fndecl->fn.pnames[0], "self") != 0) {
        char **new_pnames = malloc((fndecl->fn.pc + 1) * sizeof(char *));
        char **new_ptypes = malloc((fndecl->fn.pc + 1) * sizeof(char *));
        Node **new_pdefaults = fndecl->fn.pdefaults ? malloc((fndecl->fn.pc + 1) * sizeof(Node *)) : NULL;
        new_pnames[0] = strdup("self");
        new_ptypes[0] = strdup(aname);
        if (new_pdefaults) new_pdefaults[0] = NULL;
        for (int k = 0; k < fndecl->fn.pc; k++) {
          new_pnames[k + 1] = fndecl->fn.pnames[k];
          new_ptypes[k + 1] = fndecl->fn.ptypes[k];
          if (new_pdefaults && fndecl->fn.pdefaults) new_pdefaults[k + 1] = fndecl->fn.pdefaults[k];
        }
        if (fndecl->fn.pnames) free(fndecl->fn.pnames);
        if (fndecl->fn.ptypes) free(fndecl->fn.ptypes);
        if (fndecl->fn.pdefaults) free(fndecl->fn.pdefaults);
        fndecl->fn.pnames = new_pnames;
        fndecl->fn.ptypes = new_ptypes;
        fndecl->fn.pdefaults = new_pdefaults;
        fndecl->fn.pc++;
      }
      if (mcount == mcap) {
        mcap *= 2;
        methods = realloc(methods, mcap * sizeof(Node *));
      }
      methods[mcount++] = fndecl;
      if (Check(p, TSEMI)) {
        Token sc = Consume(p); TokFree(&sc);
      } else if (Check(p, TCOMMA)) {
        Token cm = Consume(p); TokFree(&cm);
      }
      continue;
    }
    if (Check(p, TLET) || Check(p, TMUT)) {
      Token letkw = Consume(p); TokFree(&letkw);
    }
    Token fn = Expect(p, TIDENT);
    char *fname = fn.val ? strdup(fn.val) : strdup("");
    TokFree(&fn);
    char *ftype = strdup("any");
    if (Check(p, TCOLON)) {
      Token co = Consume(p); TokFree(&co);
      free(ftype);
      ftype = ParseType(p);
    }
    Node *fdef = NULL;
    if (Check(p, TEQ)) {
      Token eq = Consume(p); TokFree(&eq);
      fdef = ParserExpr(p);
    }
    if (Check(p, TSEMI)) {
      Token sc = Consume(p); TokFree(&sc);
    } else if (Check(p, TCOMMA)) {
      Token cm = Consume(p); TokFree(&cm);
    }
    if (fc == fcap) {
      fcap *= 2;
      fnames = realloc(fnames, fcap * sizeof(char *));
      ftypes = realloc(ftypes, fcap * sizeof(char *));
      fdefaults = realloc(fdefaults, fcap * sizeof(Node *));
    }
    fnames[fc] = fname;
    ftypes[fc] = ftype;
    fdefaults[fc] = fdef;
    fc++;
  }
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);

  Node *n = NodeNew(NACTORDECL, line);
  n->actordecl.name = aname;
  n->actordecl.fnames = fnames;
  n->actordecl.ftypes = ftypes;
  n->actordecl.fdefaults = fdefaults;
  n->actordecl.fc = fc;
  n->actordecl.methods = methods;
  n->actordecl.mc = mcount;
  n->actordecl.visibility = VIS_PUB;
  return n;
}

static Node *ParseEffect(Parser *p) {
  int line = p->cur.line;
  Token ekw = Expect(p, TEFFECT);
  TokFree(&ekw);
  Token ename = Expect(p, TIDENT);
  char *eff_name = strdup(ename.val);
  TokFree(&ename);
  if (Check(p, TLT)) {
    Token lt = Consume(p); TokFree(&lt);
    Token tp = Expect(p, TIDENT); TokFree(&tp);
    Token gt = Expect(p, TGT); TokFree(&gt);
  }
  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);
  int opcap = 4, opcount = 0;
  char **opnames = malloc(opcap * sizeof(char *));
  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (Check(p, TFN)) {
      Token fnkw = Consume(p); TokFree(&fnkw);
    }
    Token opTok = Expect(p, TIDENT);
    if (opcount == opcap) {
      opcap *= 2;
      opnames = realloc(opnames, opcap * sizeof(char *));
    }
    opnames[opcount++] = strdup(opTok.val);
    TokFree(&opTok);
    if (Check(p, TLPAREN)) {
      Token lp = Consume(p); TokFree(&lp);
      while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        Token pt = Consume(p); TokFree(&pt);
      }
      if (Check(p, TRPAREN)) {
        Token rp = Consume(p); TokFree(&rp);
      }
    }
    if (Check(p, TARROW) || Check(p, TFATARROW)) {
      Token ar = Consume(p); TokFree(&ar);
      if (Check(p, TIDENT) || Check(p, TSTR) || Check(p, TINT) || Check(p, TVOID)) {
        Token rt = Consume(p); TokFree(&rt);
      }
    }
    if (Check(p, TSEMI)) {
      Token sc = Consume(p); TokFree(&sc);
    }
  }
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);
  Node *node = NodeNew(NEFFECTDECL, line);
  node->effectdecl.name = eff_name;
  node->effectdecl.opnames = opnames;
  node->effectdecl.opcount = opcount;
  node->effectdecl.opparamnames = NULL;
  node->effectdecl.opparamtypes = NULL;
  node->effectdecl.opparamcounts = NULL;
  return node;
}

static Node *ParsePerform(Parser *p) {
  int line = p->cur.line;
  Token pkw = Expect(p, TPERFORM);
  TokFree(&pkw);
  Token firstTok = Expect(p, TIDENT);
  char *effname = NULL;
  char *opname = NULL;
  if (Check(p, TDOT)) {
    Token dot = Consume(p); TokFree(&dot);
    effname = strdup(firstTok.val);
    Token secondTok = Expect(p, TIDENT);
    opname = strdup(secondTok.val);
    TokFree(&secondTok);
  } else {
    opname = strdup(firstTok.val);
  }
  TokFree(&firstTok);
  int argcap = 4, argcount = 0;
  Node **args = malloc(argcap * sizeof(Node *));
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p); TokFree(&lp);
    while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
      if (argcount == argcap) {
        argcap *= 2;
        args = realloc(args, argcap * sizeof(Node *));
      }
      args[argcount++] = ParserExpr(p);
      if (Check(p, TCOMMA)) {
        Token cm = Consume(p); TokFree(&cm);
      } else {
        break;
      }
    }
    Token rp = Expect(p, TRPAREN); TokFree(&rp);
  }
  Node *node = NodeNew(NPERFORM, line);
  node->performn.effectname = effname;
  node->performn.opname = opname;
  node->performn.args = args;
  node->performn.argcount = argcount;
  return node;
}

static Node *ParseHandle(Parser *p) {
  int line = p->cur.line;
  Token hkw = Expect(p, THANDLE);
  TokFree(&hkw);
  Node *body = NULL;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p); TokFree(&lp);
    body = ParserExpr(p);
    Token rp = Expect(p, TRPAREN); TokFree(&rp);
  } else {
    body = ParserExpr(p);
  }
  if (Check(p, TIDENT) && strcmp(p->cur.val, "with") == 0) {
    Token wkw = Consume(p); TokFree(&wkw);
  }
  Token lb = Expect(p, TLBRACE); TokFree(&lb);
  int hcap = 4, hcount = 0;
  char **opnames = malloc(hcap * sizeof(char *));
  Node **handlers = malloc(hcap * sizeof(Node *));
  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    Token opTok = Expect(p, TIDENT);
    char *opname = NULL;
    if (Check(p, TDOT)) {
      Token dot = Consume(p); TokFree(&dot);
      Token subTok = Expect(p, TIDENT);
      size_t len = strlen(opTok.val) + 1 + strlen(subTok.val) + 1;
      opname = malloc(len);
      snprintf(opname, len, "%s.%s", opTok.val, subTok.val);
      TokFree(&subTok);
    } else {
      opname = strdup(opTok.val);
    }
    TokFree(&opTok);
    int pcap = 4, pcount = 0;
    char **pnames = malloc(pcap * sizeof(char *));
    char **ptypes = malloc(pcap * sizeof(char *));
    if (Check(p, TLPAREN)) {
      Token lp = Consume(p); TokFree(&lp);
      while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        Token pt = Expect(p, TIDENT);
        if (pcount == pcap) {
          pcap *= 2;
          pnames = realloc(pnames, pcap * sizeof(char *));
          ptypes = realloc(ptypes, pcap * sizeof(char *));
        }
        pnames[pcount] = strdup(pt.val);
        ptypes[pcount] = strdup("any");
        pcount++;
        TokFree(&pt);
        if (Check(p, TCOLON)) {
          Token co = Consume(p); TokFree(&co);
          free(ptypes[pcount-1]);
          ptypes[pcount-1] = ParseType(p);
        }
        if (Check(p, TCOMMA)) {
          Token cm = Consume(p); TokFree(&cm);
        } else {
          break;
        }
      }
      Token rp = Expect(p, TRPAREN); TokFree(&rp);
    }
    if (Check(p, TFATARROW) || Check(p, TARROW)) {
      Token fa = Consume(p); TokFree(&fa);
    }
    Node *hbody = NULL;
    if (Check(p, TLBRACE)) {
      hbody = ParseBlock(p);
    } else {
      hbody = ParserExpr(p);
    }
    static int anonHCount = 0;
    char anonHName[64];
    snprintf(anonHName, sizeof(anonHName), "__anon_handler_%d", anonHCount++);
    Node *handlerfn = NodeNew(NFNDECL, line);
    handlerfn->fn.name = strdup(anonHName);
    handlerfn->fn.pnames = pnames;
    handlerfn->fn.ptypes = ptypes;
    handlerfn->fn.pc = pcount;
    handlerfn->fn.body = hbody;
    handlerfn->fn.isasync = 0;
    handlerfn->fn.ret = NULL;
    handlerfn->fn.effect = NULL;
    handlerfn->fn.pdefaults = NULL;
    handlerfn->fn.requires = NULL;
    handlerfn->fn.req_count = 0;
    handlerfn->fn.ensures = NULL;
    handlerfn->fn.ens_count = 0;
    handlerfn->fn.tpc = 0;
    handlerfn->fn.tparams = NULL;
    
    if (hcount == hcap) {
      hcap *= 2;
      opnames = realloc(opnames, hcap * sizeof(char *));
      handlers = realloc(handlers, hcap * sizeof(Node *));
    }
    opnames[hcount] = opname;
    handlers[hcount] = handlerfn;
    hcount++;
    if (Check(p, TCOMMA)) {
      Token cm = Consume(p); TokFree(&cm);
    }
  }
  Token rb = Expect(p, TRBRACE); TokFree(&rb);
  Node *node = NodeNew(NHANDLE, line);
  node->handlen.body = body;
  node->handlen.opnames = opnames;
  node->handlen.handlers = handlers;
  node->handlen.count = hcount;
  return node;
}

static Node *ParseResume(Parser *p) {
  int line = p->cur.line;
  Token rkw = Expect(p, TRESUME);
  TokFree(&rkw);
  Node *expr = NULL;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p); TokFree(&lp);
    if (!Check(p, TRPAREN)) {
      expr = ParserExpr(p);
    }
    Token rp = Expect(p, TRPAREN); TokFree(&rp);
  } else if (!Check(p, TSEMI) && !Check(p, TRBRACE) && !Check(p, TEOF)) {
    expr = ParserExpr(p);
  }
  Node *node = NodeNew(NRESUME, line);
  node->resumen.expr = expr;
  return node;
}

static Node *ParseAtomic(Parser *p) {
  int line = p->cur.line;
  Token akw = Expect(p, TATOMIC);
  TokFree(&akw);
  Node *body = NULL;
  if (Check(p, TLBRACE)) {
    body = ParseBlock(p);
  } else if (Check(p, TLPAREN)) {
    Token lp = Consume(p); TokFree(&lp);
    body = ParserExpr(p);
    Token rp = Expect(p, TRPAREN); TokFree(&rp);
  } else {
    body = ParseStmt(p);
  }
  Node *node = NodeNew(NATOMIC, line);
  node->atomicn.body = body;
  return node;
}

static Node *ParsePattern(Parser *p);

static Node *ParsePatternPrimary(Parser *p) {
  int line = p->cur.line;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p);
    TokFree(&lp);
    Node *first = ParsePattern(p);
    if (Check(p, TCOMMA)) {
      int cap = 4, count = 0;
      Node **patterns = malloc(cap * sizeof(Node *));
      patterns[count++] = first;
      while (Check(p, TCOMMA)) {
        Token cm = Consume(p);
        TokFree(&cm);
        if (Check(p, TRPAREN))
          break;
        if (count == cap) {
          cap *= 2;
          patterns = realloc(patterns, cap * sizeof(Node *));
        }
        patterns[count++] = ParsePattern(p);
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      Node *n = NodeNew(NTUPLEPAT, line);
      n->tuplepat.patterns = patterns;
      n->tuplepat.count = count;
      return n;
    }
    Token rp = Expect(p, TRPAREN);
    TokFree(&rp);
    return first;
  }

  if (Check(p, TASM) || (Check(p, TIDENT) && strcmp(p->cur.val, "asm") == 0)) {
    Token tok = Consume(p);
    TokFree(&tok);
    Token lb = Expect(p, TLBRACE);
    TokFree(&lb);
    Token str = Expect(p, TSTRLIT);
    char *code = strdup(str.val);
    TokFree(&str);
    Token rb = Expect(p, TRBRACE);
    TokFree(&rb);
    Node *n = NodeNew(NASM, line);
    n->asmstmt.code = code;
    return n;
  }

  if (Check(p, TINT) || Check(p, TINTLIT)) {
    Token t = Consume(p);
    Node *n = NodeNew(NINT, line);
    n->ival = atoll(t.val);
    TokFree(&t);
    return n;
  }
  if (Check(p, TFLOAT) || Check(p, TFLOATLIT)) {
    Token t = Consume(p);
    Node *n = NodeNew(NFLOAT, line);
    n->fval = atof(t.val);
    TokFree(&t);
    return n;
  }
  if (Check(p, TBOOL) || Check(p, TBOOLLIT)) {
    Token t = Consume(p);
    Node *n = NodeNew(NBOOL, line);
    n->sval = strdup(t.val);
    TokFree(&t);
    return n;
  }
  if (Check(p, TSTR) || Check(p, TSTRLIT)) {
    Token t = Consume(p);
    Node *n = NodeNew(NSTR, line);
    n->sval = strdup(t.val);
    TokFree(&t);
    return n;
  }
  if (Check(p, TNULL)) {
    Token t = Consume(p);
    TokFree(&t);
    return NodeNew(NNULL, line);
  }
  if (Check(p, TACTOR)) {
    Token t = Consume(p);
    Node *n = NodeNew(NIDENT, line);
    n->sval = strdup("actor");
    TokFree(&t);
    return n;
  }
  if (Check(p, TGPU)) {
    Token t = Consume(p);
    Node *n = NodeNew(NIDENT, line);
    n->sval = strdup("gpu");
    TokFree(&t);
    return n;
  }
  if (Check(p, TPERFORM)) {
    return ParsePerform(p);
  }
  if (Check(p, THANDLE)) {
    return ParseHandle(p);
  }
  if (Check(p, TRESUME)) {
    return ParseResume(p);
  }
  if (Check(p, TLBRACKET)) {
    Token lb = Consume(p);
    TokFree(&lb);
    int cap = 4, count = 0, hasrest = 0, restidx = -1;
    Node **pats = malloc(cap * sizeof(Node *));
    while (!Check(p, TRBRACKET) && !Check(p, TEOF)) {
      if (count == cap) {
        cap *= 2;
        pats = realloc(pats, cap * sizeof(Node *));
      }
      if (Check(p, TDOTDOT) || Check(p, TDOTDOTDOT)) {
        Token dd = Consume(p);
        TokFree(&dd);
        hasrest = 1;
        restidx = count;
        if (Check(p, TIDENT)) {
          Token rt = Consume(p);
          Node *rn = NodeNew(NIDENT, line);
          rn->sval = strdup(rt.val);
          TokFree(&rt);
          pats[count++] = rn;
        } else {
          Node *rn = NodeNew(NIDENT, line);
          rn->sval = strdup("_");
          pats[count++] = rn;
        }
      } else {
        pats[count++] = ParsePattern(p);
      }
      if (!Check(p, TRBRACKET)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token rb = Expect(p, TRBRACKET);
    TokFree(&rb);
    Node *n = NodeNew(NARRPAT, line);
    n->arrpat.patterns = pats;
    n->arrpat.count = count;
    n->arrpat.hasrest = hasrest;
    n->arrpat.restidx = restidx;
    return n;
  }
  if (Check(p, TMOVE)) {
    Token mv = Consume(p);
    TokFree(&mv);
    return ParsePrimary(p);
  }

  if (Check(p, TIDENT)) {
    Token firsttok = Consume(p);
    char *name = strdup(firsttok.val);
    TokFree(&firsttok);

    /* Consume :: path segments: ident::ident::... or ident::* */
    while (Check(p, TCOLONCOLON)) {
      Token cctok = Consume(p); /* consume :: */
      TokFree(&cctok);
      if (Check(p, TIDENT)) {
        Token seg = Consume(p);
        int newlen = (int)(strlen(name) + 2 + strlen(seg.val) + 1);
        char *newname = malloc(newlen);
        snprintf(newname, newlen, "%s::%s", name, seg.val);
        free(name);
        name = newname;
        TokFree(&seg);
      } else if (Check(p, TSTAR)) {
        /* wildcard: math::* */
        Token star = Consume(p);
        TokFree(&star);
        int newlen = (int)(strlen(name) + 4);
        char *newname = malloc(newlen);
        snprintf(newname, newlen, "%s::*", name);
        free(name);
        name = newname;
        break; /* * must be last segment */
      } else {
        /* bare :: at end, treat as-is */
        break;
      }
    }

    if (Check(p, TDOT)) {
      Token dt = Consume(p);
      TokFree(&dt);
      Token fieldtok = Expect(p, TIDENT);
      char *fieldname = strdup(fieldtok.val);
      TokFree(&fieldtok);

      if (Check(p, TLPAREN)) {
        Token lp = Consume(p);
        TokFree(&lp);
        int cap = 4, count = 0;
        Node **args = malloc(cap * sizeof(Node *));
        while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
          if (count == cap) {
            cap *= 2;
            args = realloc(args, cap * sizeof(Node *));
          }
          args[count++] = ParsePattern(p);
          if (!Check(p, TRPAREN)) {
            Token cm = Expect(p, TCOMMA);
            TokFree(&cm);
          }
        }
        Token rp = Expect(p, TRPAREN);
        TokFree(&rp);

        Node *member = NodeNew(NMEMBER, line);
        member->member.obj = NodeNew(NIDENT, line);
        member->member.obj->sval = name;
        member->member.field = fieldname;

        Node *call = NodeNew(NCALL, line);
        call->call.callee = member;
        call->call.args = args;
        call->call.argc = count;
        return call;
      }

      Node *member = NodeNew(NMEMBER, line);
      member->member.obj = NodeNew(NIDENT, line);
      member->member.obj->sval = name;
      member->member.field = fieldname;
      return member;
    }

    if (Check(p, TLBRACE)) {
      Token lb = Consume(p);
      TokFree(&lb);
      int cap = 4, count = 0;
      char **fields = malloc(cap * sizeof(char *));
      Node **patterns = malloc(cap * sizeof(Node *));
      while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
        if (count == cap) {
          cap *= 2;
          fields = realloc(fields, cap * sizeof(char *));
          patterns = realloc(patterns, cap * sizeof(Node *));
        }
        char *fname = NULL;
        if (p->cur.val) {
          Token fld = Consume(p);
          fname = strdup(fld.val);
          TokFree(&fld);
        } else {
          Token fld = Expect(p, TIDENT);
          fname = strdup(fld.val);
          TokFree(&fld);
        }
        fields[count] = fname;

        if (Check(p, TCOLON)) {
          Token cl = Consume(p);
          TokFree(&cl);
          patterns[count] = ParsePattern(p);
        } else {
          patterns[count] = NodeNew(NIDENT, line);
          patterns[count]->sval = strdup(fields[count]);
        }
        count++;
        if (!Check(p, TRBRACE)) {
          Token cm = Expect(p, TCOMMA);
          TokFree(&cm);
        }
      }
      Token rb = Expect(p, TRBRACE);
      TokFree(&rb);

      Node *n = NodeNew(NSTRUCTPAT, line);
      n->structpat.name = name;
      n->structpat.fields = fields;
      n->structpat.patterns = patterns;
      n->structpat.count = count;
      return n;
    }

    Node *n = NodeNew(NIDENT, line);
    n->sval = name;
    return n;
  }

  Token bad = Consume(p);
  Node *n = NodeNew(NIDENT, line);
  n->sval = strdup(bad.val);
  TokFree(&bad);
  return n;
}

static Node *ParsePattern(Parser *p) {
  Node *left = ParsePatternPrimary(p);
  while (Check(p, TPIPE)) {
    Token pipetok = Consume(p);
    TokFree(&pipetok);
    Node *right = ParsePatternPrimary(p);
    Node *parent = NodeNew(NORPAT, left->line);
    parent->orpat.left = left;
    parent->orpat.right = right;
    left = parent;
  }
  return left;
}

static Node *ParseMatchBody(Parser *p);

static Node *ParsePatternMatch(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  Node *expr = NULL;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p); TokFree(&lp);
    expr = ParserExpr(p);
    Token rp = Expect(p, TRPAREN); TokFree(&rp);
  } else {
    expr = ParserExpr(p);
  }
  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);

  int cap = 4;
  Node **patterns = malloc(cap * sizeof(Node *));
  Node **bodies = malloc(cap * sizeof(Node *));
  Node **guards = malloc(cap * sizeof(Node *));
  int count = 0;

  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (count == cap) {
      cap *= 2;
      patterns = realloc(patterns, cap * sizeof(Node *));
      bodies = realloc(bodies, cap * sizeof(Node *));
      guards = realloc(guards, cap * sizeof(Node *));
    }

    int isdefault = 0;
    if (Match(p, TCASE)) {
      // consumed case
    }
    if (Match(p, TDEFAULT) || Match(p, TELSE)) {
      isdefault = 1;
    }

    Node *pat = NULL;
    if (isdefault) {
      pat = NodeNew(NIDENT, p->cur.line);
      pat->sval = strdup("_");
    } else {
      pat = ParsePattern(p);
    }

    Node *guard = NULL;
    if (Match(p, TIF)) {
      guard = ParserExpr(p);
    }

    Node *body = NULL;
    if (Check(p, TFATARROW)) {
      Token arrow = Consume(p);
      TokFree(&arrow);
      body = ParseMatchBody(p);
    } else {
      Token co = Expect(p, TCOLON);
      TokFree(&co);
      body = NodeNew(NBLOCK, p->cur.line);
      int bcap = 4;
      body->block.stmts = malloc(bcap * sizeof(Node *));
      body->block.count = 0;
      while (!Check(p, TCASE) && !Check(p, TDEFAULT) && !Check(p, TRBRACE) &&
             !Check(p, TEOF)) {
        Node *s = ParseStmt(p);
        if (!s)
          continue;
        if (body->block.count == bcap) {
          bcap *= 2;
          body->block.stmts = realloc(body->block.stmts, bcap * sizeof(Node *));
        }
        body->block.stmts[body->block.count++] = s;
      }
    }

    patterns[count] = pat;
    guards[count] = guard;
    bodies[count] = body;
    count++;

    if (Check(p, TCOMMA)) {
      Token cm = Consume(p);
      TokFree(&cm);
    }
  }
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);

  Node *n = NodeNew(NMATCH, line);
  n->match.expr = expr;
  n->match.patterns = patterns;
  n->match.bodies = bodies;
  n->match.guards = guards;
  n->match.count = count;
  return n;
}

static Node *ParseSwitch(Parser *p) { return ParsePatternMatch(p); }

static Node *ParseEnum(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  Token nm = Expect(p, TIDENT);
  char *ename = strdup(nm.val);
  TokFree(&nm);
  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);
  int cap = 8, count = 0;
  char **members = malloc(cap * sizeof(char *));
  int64_t *values = malloc(cap * sizeof(int64_t));
  char ***membertypes = calloc(cap, sizeof(char **));
  int *membertypecounts = calloc(cap, sizeof(int));
  int isadt = 0;
  int64_t nextval = 0;
  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (count == cap) {
      cap *= 2;
      members = realloc(members, cap * sizeof(char *));
      values = realloc(values, cap * sizeof(int64_t));
      membertypes = realloc(membertypes, cap * sizeof(char **));
      membertypecounts = realloc(membertypecounts, cap * sizeof(int));
      for (int i = count; i < cap; i++) {
        membertypes[i] = NULL;
        membertypecounts[i] = 0;
      }
    }
    Token mt = Expect(p, TIDENT);
    members[count] = strdup(mt.val);
    TokFree(&mt);

    if (Check(p, TLPAREN)) {
      isadt = 1;
      Token lp = Consume(p);
      TokFree(&lp);
      int mcap = 4;
      char **mtypes = malloc(mcap * sizeof(char *));
      int mcount = 0;
      while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        char *t = ParseType(p);
        if (mcount == mcap) {
          mcap *= 2;
          mtypes = realloc(mtypes, mcap * sizeof(char *));
        }
        mtypes[mcount++] = t;
        if (!Check(p, TRPAREN)) {
          Token cm = Expect(p, TCOMMA);
          TokFree(&cm);
        }
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      membertypes[count] = mtypes;
      membertypecounts[count] = mcount;
    } else {
      membertypes[count] = NULL;
      membertypecounts[count] = 0;
    }

    if (Match(p, TEQ)) {
      Token vt = Expect(p, TINTLIT);
      nextval = atoll(vt.val);
      TokFree(&vt);
    }
    values[count++] = nextval++;
    if (!Check(p, TRBRACE)) {
      Token cm = Expect(p, TCOMMA);
      TokFree(&cm);
    }
  }
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);
  Token sc = Expect(p, TSEMI);
  TokFree(&sc);
  Node *n = NodeNew(NENUMDECL, line);
  n->enumdecl.name = ename;
  n->enumdecl.members = members;
  n->enumdecl.values = values;
  n->enumdecl.count = count;
  n->enumdecl.membertypes = membertypes;
  n->enumdecl.membertypecounts = membertypecounts;
  n->enumdecl.isadt = isadt;
  return n;
}

static Node *ParseMatchBody(Parser *p) {
  int line = p->cur.line;
  if (Check(p, TLBRACE)) {
    return ParseBlock(p);
  }
  if (Check(p, TPRINT)) {
    Token kw = Consume(p);
    TokFree(&kw);
    Token lp = Expect(p, TLPAREN);
    TokFree(&lp);
    Node *e = ParserExpr(p);
    Token rp = Expect(p, TRPAREN);
    TokFree(&rp);
    Node *n = NodeNew(NPRINT, line);
    n->single.expr = e;
    return n;
  }
  if (Check(p, TRETURN)) {
    Token kw = Consume(p);
    TokFree(&kw);
    Node *e = NULL;
    if (!Check(p, TCOMMA) && !Check(p, TRBRACE)) {
      e = ParserExpr(p);
    }
    Node *n = NodeNew(NRETURN, line);
    n->single.expr = e;
    return n;
  }
  if (Check(p, TBREAK)) {
    Token kw = Consume(p);
    TokFree(&kw);
    return NodeNew(NBREAK, line);
  }
  if (Check(p, TCONTINUE)) {
    Token kw = Consume(p);
    TokFree(&kw);
    return NodeNew(NCONTINUE, line);
  }
  if (Check(p, TTHROW)) {
    Token kw = Consume(p);
    TokFree(&kw);
    Node *e = ParserExpr(p);
    Node *n = NodeNew(NTHROW, line);
    n->throwstmt.expr = e;
    return n;
  }
  if (Check(p, TYIELD)) {
    Token kw = Consume(p);
    TokFree(&kw);
    Node *e = NULL;
    if (!Check(p, TCOMMA) && !Check(p, TRBRACE)) {
      e = ParserExpr(p);
    }
    Node *n = NodeNew(NYIELD, line);
    n->yieldstmt.expr = e;
    return n;
  }
  Node *e = ParserExpr(p);
  Node *n = NodeNew(NEXPRSTMT, line);
  n->single.expr = e;
  return n;
}

static Node *ParseMatch(Parser *p) { return ParsePatternMatch(p); }

static Node *ParseInterface(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw); // interface
  Token nm = Expect(p, TIDENT);
  char *iname = strdup(nm.val);
  TokFree(&nm);
  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);

  int cap = 4;
  char **fnames = malloc(cap * sizeof(char *));
  char **rettypes = malloc(cap * sizeof(char *));
  char ***paramtypes = malloc(cap * sizeof(char **));
  char ***paramnames = malloc(cap * sizeof(char **));
  int *paramcounts = malloc(cap * sizeof(int));
  int *hasdefault = malloc(cap * sizeof(int));
  Node **defaultbodies = malloc(cap * sizeof(Node *));
  int count = 0;

  int assoccap = 4;
  char **assoctypes = malloc(assoccap * sizeof(char *));
  int assoccount = 0;

  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (Check(p, TTYPE) || (Check(p, TIDENT) && strcmp(p->cur.val, "type") == 0)) {
      Token ty = Consume(p);
      TokFree(&ty);
      Token nmtok = Expect(p, TIDENT);
      if (assoccount == assoccap) {
        assoccap *= 2;
        assoctypes = realloc(assoctypes, assoccap * sizeof(char *));
      }
      assoctypes[assoccount++] = strdup(nmtok.val);
      TokFree(&nmtok);
      Token sc = Expect(p, TSEMI);
      TokFree(&sc);
      continue;
    }

    if (count == cap) {
      cap *= 2;
      fnames = realloc(fnames, cap * sizeof(char *));
      rettypes = realloc(rettypes, cap * sizeof(char *));
      paramtypes = realloc(paramtypes, cap * sizeof(char **));
      paramnames = realloc(paramnames, cap * sizeof(char **));
      paramcounts = realloc(paramcounts, cap * sizeof(int));
      hasdefault = realloc(hasdefault, cap * sizeof(int));
      defaultbodies = realloc(defaultbodies, cap * sizeof(Node *));
    }

    Expect(p, TFN);
    Token fn = Expect(p, TIDENT);
    char *fname = fn.val ? strdup(fn.val) : strdup("");
    TokFree(&fn);
    Token lp = Expect(p, TLPAREN);
    TokFree(&lp);

    int pcap = 4;
    char **ptypes = malloc(pcap * sizeof(char *));
    char **pnames = malloc(pcap * sizeof(char *));
    int pcount = 0;

    while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
      Token pn = Expect(p, TIDENT);
      Token co = Expect(p, TCOLON);
      TokFree(&co);
      char *pt = ParseType(p);

      if (pcount == pcap) {
        pcap *= 2;
        ptypes = realloc(ptypes, pcap * sizeof(char *));
        pnames = realloc(pnames, pcap * sizeof(char *));
      }
      pnames[pcount] = strdup(pn.val);
      ptypes[pcount] = pt;
      pcount++;
      TokFree(&pn);

      if (!Check(p, TRPAREN)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token rp = Expect(p, TRPAREN);
    TokFree(&rp);

    char *ret = NULL;
    if (Match(p, TARROW)) {
      ret = ParseType(p);
    } else {
      ret = strdup("void");
    }

    Node *defbody = NULL;
    int hasdef = 0;
    if (Check(p, TLBRACE)) {
      defbody = ParseBlock(p);
      hasdef = 1;
    } else {
      Token sc = Expect(p, TSEMI);
      TokFree(&sc);
    }

    fnames[count] = fname;
    rettypes[count] = ret;
    paramtypes[count] = ptypes;
    paramnames[count] = pnames;
    paramcounts[count] = pcount;
    hasdefault[count] = hasdef;
    defaultbodies[count] = defbody;
    count++;
  }
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);

  Node *n = NodeNew(NINTERFACEDECL, line);
  n->interfacedecl.name = iname;
  n->interfacedecl.fnames = fnames;
  n->interfacedecl.rettypes = rettypes;
  n->interfacedecl.paramtypes = paramtypes;
  n->interfacedecl.paramnames = paramnames;
  n->interfacedecl.paramcounts = paramcounts;
  n->interfacedecl.hasdefault = hasdefault;
  n->interfacedecl.defaultbodies = defaultbodies;
  n->interfacedecl.count = count;
  n->interfacedecl.assoctypes = assoctypes;
  n->interfacedecl.assoccount = assoccount;
  return n;
}

/*
 * Decorator desugaring strategy for functions:
 *
 *   @deco fn f(x) { ... }
 *
 * desugars to:
 *
 *   fn origfnf(x) { ... }      // registered under mangled name
 *   let f = deco(origfnf);     // public 'f' is a let-variable, not a compiled
 * fn
 *
 * Multiple decorators (@a @b fn f)  -  applied bottom-up:
 *   fn origfnf(x) { ... }
 *   let f = b(origfnf);
 *   f = a(f);
 *
 * For struct/interface decorators, we emit annotation calls:
 *   struct S { ... }
 *   deco(S);    // side-effect annotation
 */
static Node *ParseDecoratedStmt(Parser *p) {
  int line = p->cur.line;

  /* Collect decorator expressions in order */
  int dcap = 4, dcount = 0;
  Node **decos = malloc(dcap * sizeof(Node *));

  while (p->cur.type == TAT) {
    Token at = Consume(p);
    TokFree(&at);
    /* parse the decorator: @name or @name(args) */
    Node *decoexpr = ParsePrimary(p);
    /* handle @deco(args)  -  turns into a partial-application call */
    while (p->cur.type == TLPAREN) {
      Token lp = Consume(p);
      TokFree(&lp);
      int argc = 0, acap = 4;
      Node **args = malloc(acap * sizeof(Node *));
      int *spreads = malloc(acap * sizeof(int));
      while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        if (argc == acap) {
          acap *= 2;
          args = realloc(args, acap * sizeof(Node *));
          spreads = realloc(spreads, acap * sizeof(int));
        }
        int isspread = 0;
        if (Check(p, TDOTDOTDOT)) {
          Token sp = Consume(p);
          TokFree(&sp);
          isspread = 1;
        }
        args[argc] = ParserExpr(p);
        spreads[argc] = isspread;
        argc++;
        if (!Check(p, TRPAREN)) {
          Token cm = Expect(p, TCOMMA);
          TokFree(&cm);
        }
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      Node *call = NodeNew(NCALL, decoexpr->line);
      call->call.callee = decoexpr;
      call->call.args = args;
      call->call.argc = argc;
      call->call.isspread = spreads;
      call->call.targs = NULL;
      call->call.tc = 0;
      decoexpr = call;
    }
    if (dcount == dcap) {
      dcap *= 2;
      decos = realloc(decos, dcap * sizeof(Node *));
    }
    decos[dcount++] = decoexpr;
  }

  /* Parse the decorated declaration */
  Node *decl = NULL;
  char *declname = NULL;
  int isfn = 0;

  if (p->cur.type == TFN) {
    decl = ParseFn(p);
    if (decl) {
      declname = decl->fn.name;
      isfn = 1;
    }
  } else if (p->cur.type == TASYNC) {
    Token asynctok = Consume(p);
    TokFree(&asynctok);
    if (!Check(p, TFN)) {
      fprintf(stderr, "Expected 'fn' after 'async'\n");
      free(decos);
      return NULL;
    }
    Token kw2 = Consume(p);
    TokFree(&kw2);
    Token nm = Expect(p, TIDENT);
    char *fname;
    if (Check(p, TDOT)) {
      Token dot = Consume(p);
      TokFree(&dot);
      Token sub = Expect(p, TIDENT);
      size_t len = strlen(nm.val) + 1 + strlen(sub.val) + 1;
      fname = malloc(len);
      snprintf(fname, len, "%s.%s", nm.val, sub.val);
      TokFree(&sub);
    } else {
      fname = strdup(nm.val);
    }
    TokFree(&nm);
    decl = ParseFnBody(p, line, fname);
    decl->fn.isasync = 1;
    declname = decl->fn.name;
    isfn = 1;
  } else if (p->cur.type == TSTRUCT) {
    decl = ParseStruct(p);
    if (decl)
      declname = decl->strct.name;
  } else if (p->cur.type == TINTERFACE) {
    decl = ParseInterface(p);
    if (decl)
      declname = decl->interfacedecl.name;
  } else {
    fprintf(stderr,
            "[line %d] @decorator must precede fn, struct, or interface\n",
            line);
    for (int i = 0; i < dcount; i++)
      NodeFree(decos[i]);
    free(decos);
    return NULL;
  }

  if (!decl || !declname) {
    for (int i = 0; i < dcount; i++)
      NodeFree(decos[i]);
    free(decos);
    return decl;
  }

  if (isfn) {
    /*
     * Strategy: rename the function to origfn<name> internally, then
     * emit `let <name> = deco[n-1](origfn<name>); <name> = deco[n-2](<name>);
     * ...` This makes <name> a let-variable that always resolves via
     * OPLOADGLOBAL rather than OPPUSHFN, so callers pick up the decorated
     * wrapper.
     */
    char origname[256];
    snprintf(origname, sizeof(origname), "origfn%s", declname);

    /* Rename the fn node to use the mangled name */
    free(decl->fn.name);
    decl->fn.name = strdup(origname);

    /*
     * Build statements:
     *   [0]  fn origfnf(...) { ... }   (already renamed decl)
     *   [1]  let f = deco[last](origfnf)     (NVARDECL)
     *   [2..] f = deco[i](f)                    (NEXPRSTMT(NASSIGN))
     */
    int total =
        1 + 1 + (dcount - 1); /* decl + let + (dcount-1) reassignments */
    if (dcount == 0)
      total = 1; /* no decorators: just the (renamed) fn  -  shouldn't happen */
    Node **stmts = malloc(total * sizeof(Node *));
    stmts[0] = decl;

    if (dcount > 0) {
      /* First: let f = deco[last](origfnf) */
      int last = dcount - 1;
      Node *origref = NodeNew(NIDENT, line);
      origref->sval = strdup(origname);

      Node **c0args = malloc(sizeof(Node *));
      int *c0sp = malloc(sizeof(int));
      c0args[0] = origref;
      c0sp[0] = 0;
      Node *c0 = NodeNew(NCALL, line);
      c0->call.callee = decos[last];
      c0->call.args = c0args;
      c0->call.argc = 1;
      c0->call.isspread = c0sp;
      c0->call.targs = NULL;
      c0->call.tc = 0;

      /* let f = c0  ->  NVARDECL */
      Node *letdecl = NodeNew(NVARDECL, line);
      letdecl->decl.name = strdup(declname);
      letdecl->decl.tname = NULL;
      letdecl->decl.init = c0;
      letdecl->decl.isconst = NULL;
      letdecl->decl.isconstflag = 0;
      stmts[1] = letdecl;

      /* Remaining decorators (bottom-up): f = deco[i](f) */
      for (int i = last - 1; i >= 0; i--) {
        int si = 2 + (last - 1 - i);
        Node *nameref = NodeNew(NIDENT, line);
        nameref->sval = strdup(declname);

        Node **ciargs = malloc(sizeof(Node *));
        int *cisp = malloc(sizeof(int));
        ciargs[0] = nameref;
        cisp[0] = 0;
        Node *ci = NodeNew(NCALL, line);
        ci->call.callee = decos[i];
        ci->call.args = ciargs;
        ci->call.argc = 1;
        ci->call.isspread = cisp;
        ci->call.targs = NULL;
        ci->call.tc = 0;

        Node *lhs = NodeNew(NIDENT, line);
        lhs->sval = strdup(declname);
        Node *asgn = NodeNew(NASSIGN, line);
        asgn->assign.target = lhs;
        asgn->assign.value = ci;

        Node *estmt = NodeNew(NEXPRSTMT, line);
        estmt->single.expr = asgn;
        stmts[si] = estmt;
      }
    }

    free(decos);
    Node *blk = NodeNew(NPROGRAM, line);
    blk->block.stmts = stmts;
    blk->block.count = total;
    return blk;
  } else {
    /*
     * Struct / interface decorator: emit annotation side-effect calls.
     *   struct S { ... }
     *   deco(S);
     * (decorators applied in reverse order so outermost runs last)
     */
    int total = 1 + dcount;
    Node **stmts = malloc(total * sizeof(Node *));
    stmts[0] = decl;

    for (int i = dcount - 1; i >= 0; i--) {
      int si = 1 + (dcount - 1 - i);
      Node *nameref = NodeNew(NIDENT, line);
      nameref->sval = strdup(declname);

      Node **ciargs = malloc(sizeof(Node *));
      int *cisp = malloc(sizeof(int));
      ciargs[0] = nameref;
      cisp[0] = 0;
      Node *ci = NodeNew(NCALL, line);
      ci->call.callee = decos[i];
      ci->call.args = ciargs;
      ci->call.argc = 1;
      ci->call.isspread = cisp;
      ci->call.targs = NULL;
      ci->call.tc = 0;

      Node *estmt = NodeNew(NEXPRSTMT, line);
      estmt->single.expr = ci;
      stmts[si] = estmt;
    }

    free(decos);
    Node *blk = NodeNew(NPROGRAM, line);
    blk->block.stmts = stmts;
    blk->block.count = total;
    return blk;
  }
}

static Node *ParseTypeAlias(Parser *p, int isopaque) {
  int line = p->cur.line;
  Token kw = Expect(p, TTYPE);
  TokFree(&kw);
  Token name = Expect(p, TIDENT);
  char *aliasname = strdup(name.val);
  TokFree(&name);
  char **tparams = NULL;
  int tpc = 0;
  if (Check(p, TLT)) {
    Token lt = Consume(p);
    TokFree(&lt);
    int cap = 4;
    tparams = malloc(cap * sizeof(char *));
    while (!Check(p, TGT) && !Check(p, TEOF)) {
      if (tpc == cap) {
        cap *= 2;
        tparams = realloc(tparams, cap * sizeof(char *));
      }
      Token tp = (Check(p, TLIFETIME) || Check(p, TIDENT)) ? Consume(p) : Expect(p, TIDENT);
      tparams[tpc++] = strdup(tp.val);
      TokFree(&tp);
      if (!Check(p, TGT)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token gt = Expect(p, TGT);
    TokFree(&gt);
  }
  Token eq = Expect(p, TEQ);
  TokFree(&eq);
  char *target = ParseType(p);
  if (Check(p, TWHERE)) {
    Token wh = Consume(p);
    TokFree(&wh);
    Token var = Expect(p, TIDENT);
    char *varname = strdup(var.val);
    TokFree(&var);
    size_t cap = 256;
    char *clause = malloc(cap);
    clause[0] = '\0';
    size_t len = 0;
    int first = 1;
    while (!Check(p, TSEMI) && !Check(p, TEOF)) {
      Token tok = Consume(p);
      size_t vlen = strlen(tok.val);
      if (len + vlen + 4 >= cap) {
        cap *= 2;
        clause = realloc(clause, cap);
      }
      if (!first) {
        strcat(clause, " ");
        len++;
      }
      strcat(clause, tok.val);
      len += vlen;
      TokFree(&tok);
      first = 0;
    }
    size_t newlen = strlen(target) + 1 + strlen(varname) + 2 + strlen(varname) + 1 + strlen(clause) + 2;
    char *newtarget = malloc(newlen);
    snprintf(newtarget, newlen, "%s{%s: %s %s}", target, varname, varname, clause);
    free(target);
    free(clause);
    free(varname);
    target = newtarget;
  }
  Token sc = Expect(p, TSEMI);
  TokFree(&sc);

  Node *n = NodeNew(NTYPEALIAS, line);
  n->typealias.name = aliasname;
  n->typealias.target = target;
  n->typealias.isopaque = isopaque;
  n->typealias.tparams = tparams;
  n->typealias.tpc = tpc;
  return n;
}

static Node *ParseSelect(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p); // Tselect
  TokFree(&kw);
  Token lb = Expect(p, TLBRACE);
  TokFree(&lb);
  
  int cap = 4;
  Node **cases = malloc(cap * sizeof(Node*));
  Node **bodies = malloc(cap * sizeof(Node*));
  int count = 0;
  Node *defaultbody = NULL;
  
  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (count == cap) {
      cap *= 2;
      cases = realloc(cases, cap * sizeof(Node*));
      bodies = realloc(bodies, cap * sizeof(Node*));
    }
    
    if (Match(p, TDEFAULT)) {
      Expect(p, TFATARROW);
      defaultbody = ParseBlock(p);
      continue;
    }
    
    Expect(p, TCASE);
    Node *cnode = NULL;
    if (Check(p, TLET)) {
      int cline = p->cur.line;
      Token lt = Consume(p);
      TokFree(&lt);
      Token nm = Expect(p, TIDENT);
      char *name = strdup(nm.val);
      TokFree(&nm);
      Token eq = Expect(p, TEQ);
      TokFree(&eq);
      Node *init = ParserExpr(p);
      cnode = NodeNew(NVARDECL, cline);
      cnode->decl.name = name;
      cnode->decl.tname = strdup("any");
      cnode->decl.init = init;
      cnode->decl.isconstflag = 0;
    } else {
      cnode = ParserExpr(p);
    }
    
    Expect(p, TFATARROW);
    Node *bnode = ParseBlock(p);
    
    cases[count] = cnode;
    bodies[count] = bnode;
    count++;
  }
  
  Token rb = Expect(p, TRBRACE);
  TokFree(&rb);
  
  Node *n = NodeNew(NSELECT, line);
  n->selectn.cases = cases;
  n->selectn.bodies = bodies;
  n->selectn.count = count;
  n->selectn.defaultbody = defaultbody;
  return n;
}

static Node *ParseImpl(Parser *p) {
  int line = p->cur.line;
  Token implkw = Consume(p); TokFree(&implkw);
  Token target = Expect(p, TIDENT);
  char *target_type = strdup(target.val);
  TokFree(&target);
  Token lb = Expect(p, TLBRACE); TokFree(&lb);

  int cap = 4, count = 0;
  Node **stmts = malloc(cap * sizeof(Node *));

  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (Check(p, TFN)) {
      Token fnkw = Consume(p); TokFree(&fnkw);
      Token mname;
      if (Check(p, TIDENT) || Check(p, TNEW) || (p->cur.val && !Check(p, TLPAREN) && !Check(p, TRBRACE) && !Check(p, TEOF))) {
        mname = Consume(p);
      } else {
        mname = Expect(p, TIDENT);
      }
      size_t len = strlen(target_type) + 1 + strlen(mname.val) + 1;
      char *fullname = malloc(len);
      snprintf(fullname, len, "%s.%s", target_type, mname.val);
      TokFree(&mname);
      Node *fndecl = ParseFnBody(p, p->cur.line, fullname);
      if (count == cap) {
        cap *= 2;
        stmts = realloc(stmts, cap * sizeof(Node *));
      }
      stmts[count++] = fndecl;
    } else {
      Token unk = Consume(p); TokFree(&unk);
    }
  }
  Token rb = Expect(p, TRBRACE); TokFree(&rb);
  free(target_type);

  Node *block = NodeNew(NBLOCK, line);
  block->block.stmts = stmts;
  block->block.count = count;
  return block;
}

static Node *ParseNursery(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  char *nursery_name = NULL;
  int has_paren = 0;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p);
    TokFree(&lp);
    has_paren = 1;
  }
  if (Check(p, TIDENT)) {
    Token nt = Consume(p);
    nursery_name = strdup(nt.val);
    TokFree(&nt);
  } else {
    nursery_name = strdup("n");
  }
  if (has_paren && Check(p, TRPAREN)) {
    Token rp = Consume(p);
    TokFree(&rp);
  }
  Node *body = ParseBlock(p);
  Node *n = NodeNew(NNURSERY, line);
  n->nursery.name = nursery_name;
  n->nursery.body = body;
  return n;
}

static Node *ParseRegion(Parser *p) {
  int line = p->cur.line;
  Token kw = Consume(p);
  TokFree(&kw);
  char *region_name = NULL;
  int has_paren = 0;
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p);
    TokFree(&lp);
    has_paren = 1;
  }
  if (Check(p, TIDENT)) {
    Token rt = Consume(p);
    region_name = strdup(rt.val);
    TokFree(&rt);
  } else {
    region_name = strdup("region");
  }
  if (has_paren && Check(p, TRPAREN)) {
    Token rp = Consume(p);
    TokFree(&rp);
  }
  Node *body = ParseBlock(p);
  Node *n = NodeNew(NREGION, line);
  n->region.name = region_name;
  n->region.body = body;
  return n;
}

static Node *ParseStmtInternal(Parser *p);

static Node *ParseStmt(Parser *p) {
  if (++p->recursion_depth > PARSER_MAX_RECURSION_DEPTH) {
    if (!p->suppresserrors) {
      fprintf(stderr,
              "Syntax error on line %d: maximum parser recursion depth exceeded (%d)\n",
              p->cur.line, PARSER_MAX_RECURSION_DEPTH);
    }
    p->err = 1;
    p->recursion_depth--;
    return NULL;
  }
  Node *res = ParseStmtInternal(p);
  p->recursion_depth--;
  return res;
}

static Node *ParseStmtInternal(Parser *p) {
  if (Check(p, TIDENT) && strcmp(p->cur.val, "opaque") == 0 && Check2(p, TTYPE)) {
    Token opq = Consume(p);
    TokFree(&opq);
    return ParseTypeAlias(p, 1);
  }

  /* lazy import "file.aks"   -  deferred module loading */
  if (Check(p, TIDENT) && strcmp(p->cur.val, "lazy") == 0 && Check2(p, TIMPORT)) {
    Token lk = Consume(p);  /* consume 'lazy' */
    TokFree(&lk);
    int line = p->cur.line;
    Token kw = Consume(p);  /* consume 'import' */
    TokFree(&kw);
    Token pathtok = Expect(p, TSTRLIT);
    char *path = strdup(pathtok.val);
    TokFree(&pathtok);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NIMPORT, line);
    n->single.var = path;
    n->decl.isconstflag = 1;  /* islazy flag */
    return n;
  }

  /* Visibility modifier prefix: pub / private / internal */
  int visibility = VIS_DEFAULT;
  if (Check(p, TPUB) || Check(p, TPRIVATE) || Check(p, TINTERNAL)) {
    Token vt = Consume(p);
    if (vt.type == TPUB)      visibility = VIS_PUB;
    else if (vt.type == TPRIVATE)  visibility = VIS_PRIVATE;
    else if (vt.type == TINTERNAL) visibility = VIS_INTERNAL;
    TokFree(&vt);

    /* module keyword after visibility  -  handled below */
    if (Check(p, TMODULE)) { goto parse_module; }
    /* use keyword after visibility */
    if (Check(p, TUSE))    { goto parse_use; }
  }

  /* module / use without visibility prefix */
  if (Check(p, TMODULE)) {
parse_module:;
    int mline = p->cur.line;
    Token mk = Consume(p);
    TokFree(&mk);
    Token nt = Expect(p, TIDENT);
    char *modname = strdup(nt.val);
    TokFree(&nt);
    Node *body = ParseBlock(p);
    Node *mod = NodeNew(NMODULE, mline);
    mod->single.var = modname;
    mod->single.expr = body;
    mod->decl.visibility = visibility;
    return mod;
  }
  if (Check(p, TUSE)) {
parse_use:;
    int uline = p->cur.line;
    Token uk = Consume(p);
    TokFree(&uk);
    /* Parse the path: ident (:: ident)* (:: *)? */
    Token first = Expect(p, TIDENT);
    char *path = strdup(first.val);
    TokFree(&first);
    while (Check(p, TCOLONCOLON)) {
      Token cc = Consume(p);
      TokFree(&cc);
      if (Check(p, TIDENT)) {
        Token seg = Consume(p);
        int newlen = (int)(strlen(path) + 2 + strlen(seg.val) + 1);
        char *newpath = malloc(newlen);
        snprintf(newpath, newlen, "%s::%s", path, seg.val);
        free(path);
        path = newpath;
        TokFree(&seg);
      } else if (Check(p, TSTAR)) {
        Token star = Consume(p);
        TokFree(&star);
        int newlen = (int)(strlen(path) + 4);
        char *newpath = malloc(newlen);
        snprintf(newpath, newlen, "%s::*", path);
        free(path);
        path = newpath;
        break;
      } else { break; }
    }
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *un = NodeNew(NUSE, uline);
    un->decl.tname = path;   /* target path */
    un->decl.name = NULL;    /* alias extracted at compile time */
    un->decl.isconstflag = visibility; /* reuse as visibility store */
    un->decl.visibility = visibility;
    return un;
  }

  switch (p->cur.type) {
  case TAT:
    return ParseDecoratedStmt(p);
  case TMATCH:
    return ParseMatch(p);
  case TINTERFACE:
    return ParseInterface(p);
  case TLET: {
    Node *Vn = ParseVar(p, 0);
    if (Vn) Vn->decl.visibility = visibility;
    return Vn;
  }
  case TCONST: {
    Node *Vn = ParseVar(p, 1);
    if (Vn) Vn->decl.visibility = visibility;
    return Vn;
  }
  case TASYNC: {
    int line = p->cur.line;
    if (p->peek.type == TFOR) {
      Token asynctok = Consume(p);
      TokFree(&asynctok);
      Node *fornode = ParseFor(p);
      if (fornode && fornode->type == NFORIN) {
        fornode->forin.isasync = 1;
      }
      return fornode;
    }
    Token asynctok = Consume(p);
    TokFree(&asynctok);
    Expect(p, TFN);
    Token nm = Expect(p, TIDENT);
    char *fname;
    if (Check(p, TDOT)) {
      Token dot = Consume(p);
      TokFree(&dot);
      Token sub = Expect(p, TIDENT);
      size_t len = strlen(nm.val) + 1 + strlen(sub.val) + 1;
      fname = malloc(len);
      snprintf(fname, len, "%s.%s", nm.val, sub.val);
      TokFree(&sub);
    } else {
      fname = strdup(nm.val);
    }
    TokFree(&nm);
    Node *fndecl = ParseFnBody(p, line, fname);
    fndecl->fn.isasync = 1;
    return fndecl;
  }
  case TFN: {
    Node *fn = ParseFn(p);
    if (fn) fn->fn.visibility = visibility;
    return fn;
  }
  case TIF:
    return ParseIf(p);
  case TWHILE:
    return ParseWhile(p);
  case TFOR:
    return ParseFor(p);
  case TSELECT:
    return ParseSelect(p);
  case TIMPL:
    return ParseImpl(p);
  case TSTRUCT: {
    Node *sn = ParseStruct(p);
    if (sn) sn->strct.visibility = visibility;
    return sn;
  }
  case TACTOR: {
    Node *an = ParseActor(p);
    if (an) an->actordecl.visibility = visibility;
    return an;
  }
  case TEFFECT: {
    return ParseEffect(p);
  }
  case THANDLE: {
    return ParseHandle(p);
  }
  case TPERFORM: {
    Node *n = ParsePerform(p);
    if (Check(p, TSEMI)) { Token sc = Consume(p); TokFree(&sc); }
    return n;
  }
  case TRESUME: {
    Node *n = ParseResume(p);
    if (Check(p, TSEMI)) { Token sc = Consume(p); TokFree(&sc); }
    return n;
  }
  case TGPU: {
    Token g = Consume(p); TokFree(&g);
    if (Check(p, TFN)) {
      return ParseFn(p);
    }
    if (Check(p, TLBRACE)) {
      return ParseBlock(p);
    }
    return ParseStmt(p);
  }
  case TATOMIC: {
    return ParseAtomic(p);
  }
  case TTYPE:
    return ParseTypeAlias(p, 0);
  case TSWITCH:
    return ParseSwitch(p);
  case TENUM: {
    Node *en = ParseEnum(p);
    if (en) en->enumdecl.visibility = visibility;
    return en;
  }
  case TRETURN: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    if (Check(p, TSEMI)) {
      Token sc = Consume(p);
      TokFree(&sc);
      return NodeNew(NRETURN, line);
    }
    // multi-return: return a, b;
    Node *first = ParserExpr(p);
    if (Check(p, TCOMMA)) {
      int cap = 4, count = 1;
      Node **exprs = malloc(cap * sizeof(Node *));
      exprs[0] = first;
      while (Check(p, TCOMMA)) {
        Token cm = Consume(p);
        TokFree(&cm);
        if (count == cap) {
          cap *= 2;
          exprs = realloc(exprs, cap * sizeof(Node *));
        }
        exprs[count++] = ParserExpr(p);
      }
      Token sc = Expect(p, TSEMI);
      TokFree(&sc);
      Node *mr = NodeNew(NMULTIRETURN, line);
      mr->multiret.exprs = exprs;
      mr->multiret.count = count;
      Node *n = NodeNew(NRETURN, line);
      n->single.expr = mr;
      return n;
    }
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NRETURN, line);
    n->single.expr = first;
    return n;
  }
  case TBREAK: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    return NodeNew(NBREAK, line);
  }
  case TCONTINUE: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    return NodeNew(NCONTINUE, line);
  }
  case TPRINT: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Token lp = Expect(p, TLPAREN);
    TokFree(&lp);
    Node *e = ParserExpr(p);
    Token rp = Expect(p, TRPAREN);
    TokFree(&rp);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NPRINT, line);
    n->single.expr = e;
    return n;
  }
  case TINPUT: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    char *varname = NULL;
    Node *arg = NULL;
    if (Check(p, TLPAREN)) {
      Token lp = Consume(p);
      TokFree(&lp);
      if (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        if (Check(p, TIDENT) && (Check2(p, TRPAREN) || Check2(p, TSEMI))) {
          Token Vn = Consume(p);
          varname = strdup(Vn.val);
          TokFree(&Vn);
        } else {
          arg = ParserExpr(p);
        }
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
    }
    if (Check(p, TSEMI)) {
      Token sc = Consume(p);
      TokFree(&sc);
    }
    Node *n = NodeNew(NINPUT, line);
    n->single.var = varname;
    n->single.expr = arg;
    return n;
  }
  case TIMPORT: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Token pathtok = Expect(p, TSTRLIT);
    char *path = strdup(pathtok.val);
    TokFree(&pathtok);
    // optional: as namespace
    char *alias = NULL;
    if (Match(p, TAS)) {
      Token at = Expect(p, TIDENT);
      alias = strdup(at.val);
      TokFree(&at);
    }
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NIMPORT, line);
    n->single.var = path;
    n->single.expr = alias ? (Node *)(void *)alias : NULL;
    // store alias in single.var (path) and we'll re-use single.var; alias
    // stored separately: simple approach: store alias in n->sval via a hack -
    // better: just use single.var for path we'll ignore alias for now, import
    // still works flat
    (void)alias;
    return n;
  }
  case TTRY: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Node *tryblk = ParseBlock(p);
    Node *catchblk = NULL;
    char *errvar = NULL;
    if (Check(p, TCATCH)) {
      Token ck = Consume(p);
      TokFree(&ck);
      Token lp = Expect(p, TLPAREN);
      TokFree(&lp);
      Token ev = Expect(p, TIDENT);
      errvar = strdup(ev.val);
      TokFree(&ev);
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      catchblk = ParseBlock(p);
    }
    Node *finallyblk = NULL;
    // optional finally keyword (store as a new keyword would be nice, but reuse
    // Tident "finally")
    if (Check(p, TIDENT) && !strcmp(p->cur.val, "finally")) {
      Token fk = Consume(p);
      TokFree(&fk);
      finallyblk = ParseBlock(p);
    }
    Node *n = NodeNew(NTRYCATCH, line);
    n->trycatch.tryblock = tryblk;
    n->trycatch.errvar = errvar;
    n->trycatch.catchblock = catchblk;
    n->trycatch.finallyblock = finallyblk;
    return n;
  }
  case TTHROW: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Node *expr = ParserExpr(p);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NTHROW, line);
    n->throwstmt.expr = expr;
    return n;
  }
  case TYIELD: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Node *expr = NULL;
    if (!Check(p, TSEMI))
      expr = ParserExpr(p);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NYIELD, line);
    n->yieldstmt.expr = expr;
    return n;
  }
  case TLBRACE:
    return ParseBlock(p);
  case TASM: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    char *asm_str = NULL;
    if (Check(p, TLPAREN)) {
      Token lp = Consume(p);
      TokFree(&lp);
      if (Check(p, TSTR) || Check(p, TSTRLIT)) {
        Token st = Consume(p);
        asm_str = strdup(st.val);
        TokFree(&st);
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      if (Check(p, TSEMI)) {
        Token sc = Consume(p);
        TokFree(&sc);
      }
    } else if (Check(p, TLBRACE)) {
      Token lb = Consume(p);
      TokFree(&lb);
      TypeStr ts;
      TypeStrInit(&ts);
      while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
        Token t = Consume(p);
        TypeStrAppend(&ts, t.val);
        TypeStrAppend(&ts, " ");
        TokFree(&t);
      }
      Token rb = Expect(p, TRBRACE);
      TokFree(&rb);
      asm_str = ts.buf;
    } else if (Check(p, TSTR) || Check(p, TSTRLIT)) {
      Token st = Consume(p);
      asm_str = strdup(st.val);
      TokFree(&st);
      if (Check(p, TSEMI)) {
        Token sc = Consume(p);
        TokFree(&sc);
      }
    }
    Node *n = NodeNew(NASM, line);
    n->asmstmt.code = asm_str ? asm_str : strdup("");
    return n;
  }
  case TCOMPTIME: {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Node *body = NULL;
    if (Check(p, TLBRACE)) {
      body = ParseBlock(p);
    } else {
      body = ParserExpr(p);
      if (Check(p, TSEMI)) {
        Token sc = Consume(p);
        TokFree(&sc);
      }
    }
    Node *n = NodeNew(NCONST_EVAL, line);
    n->single.expr = body;
    return n;
  }
  case TNURSERY:
    return ParseNursery(p);
  case TREGION:
    return ParseRegion(p);
  case TSEMI: {
    Token sc = Consume(p);
    TokFree(&sc);
    return NULL;
  }
  default: {
    if (Check(p, TIDENT) && strcmp(p->cur.val, "macro_rules") == 0) {
      int line = p->cur.line;
      Token kw = Consume(p);
      TokFree(&kw);
      Token nott = Expect(p, TNOT);
      TokFree(&nott);
      Token nm = Expect(p, TIDENT);
      char *mname = strdup(nm.val);
      TokFree(&nm);
      Token lb = Expect(p, TLBRACE);
      TokFree(&lb);
      
      int cap = 4, count = 0;
      Node **decls = malloc(cap * sizeof(Node *));
      
      while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
        Token lp = Expect(p, TLPAREN);
        TokFree(&lp);
        int pc = 0, pcap = 4;
        char **pnames = malloc(pcap * sizeof(char *));
        while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
          if (pc == pcap) {
            pcap *= 2;
            pnames = realloc(pnames, pcap * sizeof(char *));
          }
          Token pn = Expect(p, TIDENT);
          pnames[pc++] = strdup(pn.val);
          TokFree(&pn);
          
          if (Check(p, TCOLON)) {
            Token col = Consume(p);
            TokFree(&col);
            Token ptype = Expect(p, TIDENT);
            TokFree(&ptype);
          }
          
          if (!Check(p, TRPAREN)) {
            Token cm = Expect(p, TCOMMA);
            TokFree(&cm);
          }
        }
        Token rp = Expect(p, TRPAREN);
        TokFree(&rp);
        
        Token arrow = Expect(p, TFATARROW);
        TokFree(&arrow);
        
        Node *body = ParseBlock(p);
        
        if (Check(p, TSEMI)) {
          Token semi = Consume(p);
          TokFree(&semi);
        }
        
        Node *rule = NodeNew(NMACRODECL, line);
        rule->fn.name = strdup(mname);
        rule->fn.pnames = pnames;
        rule->fn.pc = pc;
        rule->fn.body = body;
        rule->fn.ptypes = NULL;
        rule->fn.pdefaults = NULL;
        rule->fn.tparams = NULL;
        rule->fn.tpc = 0;
        
        if (count == cap) {
          cap *= 2;
          decls = realloc(decls, cap * sizeof(Node *));
        }
        decls[count++] = rule;
      }
      Token rb = Expect(p, TRBRACE);
      TokFree(&rb);
      free(mname);
      
      Node *n = NodeNew(NPROGRAM, line);
      n->block.stmts = decls;
      n->block.count = count;
      return n;
    }
    if (Check(p, TIDENT) && strcmp(p->cur.val, "macro") == 0) {
      int line = p->cur.line;
      Token kw = Consume(p);
      TokFree(&kw);
      Token nm = Expect(p, TIDENT);
      char *mname = strdup(nm.val);
      TokFree(&nm);
      Token lp = Expect(p, TLPAREN);
      TokFree(&lp);
      int pc = 0, pcap = 4;
      char **pnames = malloc(pcap * sizeof(char *));
      while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        if (pc == pcap) {
          pcap *= 2;
          pnames = realloc(pnames, pcap * sizeof(char *));
        }
        Token pn = Expect(p, TIDENT);
        pnames[pc++] = strdup(pn.val);
        TokFree(&pn);
        if (!Check(p, TRPAREN)) {
          Token cm = Expect(p, TCOMMA);
          TokFree(&cm);
        }
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      Node *body = ParseBlock(p);
      Node *n = NodeNew(NMACRODECL, line);
      n->fn.name = mname;
      n->fn.pnames = pnames;
      n->fn.pc = pc;
      n->fn.body = body;
      n->fn.ptypes = NULL;
      n->fn.pdefaults = NULL;
      n->fn.tparams = NULL;
      n->fn.tpc = 0;
      return n;
    }
    int line = p->cur.line;
    Node *e = ParserExpr(p);
    Token sc = Expect(p, TSEMI);
    TokFree(&sc);
    Node *n = NodeNew(NEXPRSTMT, line);
    n->single.expr = e;
    return n;
  }
  }
  return NULL;
}

// expression parsing (Pratt)
static Node *ParseAssign(Parser *p);
static Node *ParseTernary(Parser *p);
static Node *ParseCoalesce(Parser *p);
static Node *ParseOr(Parser *p);
static Node *ParseAnd(Parser *p);
static Node *ParseBitOr(Parser *p);
static Node *ParseBitXor(Parser *p);
static Node *ParseBitAnd(Parser *p);
static Node *ParseEq(Parser *p);
static Node *ParseCmp(Parser *p);
static Node *ParseShift(Parser *p);
static Node *ParseRange(Parser *p);
static Node *ParseAdd(Parser *p);
static Node *ParseMul(Parser *p);
static Node *ParseUnary(Parser *p);
static Node *ParsePostfix(Parser *p);
static Node *ParsePrimary(Parser *p);

static Node *ParserExpr(Parser *p) {
  if (++p->recursion_depth > PARSER_MAX_RECURSION_DEPTH) {
    if (!p->suppresserrors) {
      fprintf(stderr,
              "Syntax error on line %d: maximum parser recursion depth exceeded (%d)\n",
              p->cur.line, PARSER_MAX_RECURSION_DEPTH);
    }
    p->err = 1;
    p->recursion_depth--;
    return NULL;
  }
  Node *res = ParseAssign(p);
  p->recursion_depth--;
  return res;
}

static Node *ParseAssign(Parser *p) {
  Node *left = ParseTernary(p);
  TType tt = p->cur.type;
  if (tt == TEQ || tt == TPLUSEQ || tt == TMINUSEQ || tt == TSTAREQ ||
      tt == TSLASHEQ || tt == TPERCENTEQ) {
    int line = p->cur.line;
    char opchar = 0;
    if (tt == TPLUSEQ)
      opchar = '+';
    else if (tt == TMINUSEQ)
      opchar = '-';
    else if (tt == TSTAREQ)
      opchar = '*';
    else if (tt == TSLASHEQ)
      opchar = '/';
    else if (tt == TPERCENTEQ)
      opchar = '%';
    Token eq = Consume(p);
    TokFree(&eq);
    Node *right = ParseAssign(p);
    if (opchar) {
      // desugar compound assign: make a copy of left as rhs operand
      Node *leftcopy = NodeNew(left->type, left->line);
      if (left->type == NIDENT)
        leftcopy->sval = strdup(left->sval);
      else if (left->type == NINDEX) {
        // shallow copy index node
        leftcopy->index.arr = left->index.arr;
        leftcopy->index.idx = left->index.idx;
      } else if (left->type == NMEMBER) {
        leftcopy->member.obj = left->member.obj;
        leftcopy->member.field =
            left->member.field ? strdup(left->member.field) : NULL;
      }
      Node *binop = NodeNew(NBINOP, line);
      char ops[2] = {opchar, 0};
      binop->binop.op = strdup(ops);
      binop->binop.left = leftcopy;
      binop->binop.right = right;
      right = binop;
    }
    Node *n = NodeNew(NASSIGN, line);
    n->assign.target = left;
    n->assign.value = right;
    return n;
  }
  return left;
}

static Node *ParseTernary(Parser *p) {
  Node *cond = ParseCoalesce(p);
  if (Check(p, TQUESTION)) {
    int line = p->cur.line;
    Token qt = Consume(p);
    TokFree(&qt);
    Node *tval = ParserExpr(p);
    Token co = Expect(p, TCOLON);
    TokFree(&co);
    Node *fval = ParserExpr(p);
    Node *n = NodeNew(NTERNARY, line);
    n->ternary.cond = cond;
    n->ternary.tval = tval;
    n->ternary.fval = fval;
    return n;
  }
  return cond;
}

static Node *ParsePipeline(Parser *p);

static Node *ParseCoalesce(Parser *p) {
  Node *left = ParsePipeline(p);
  while (Check(p, TQUESTIONQUESTION)) {
    int line = p->cur.line;
    Token qq = Consume(p);
    TokFree(&qq);
    Node *right = ParsePipeline(p);
    Node *n = NodeNew(NCOALESCE, line);
    n->coalesce.left = left;
    n->coalesce.right = right;
    left = n;
  }
  return left;
}

static Node *ParsePipeline(Parser *p) {
  Node *left = ParseOr(p);
  while (Check(p, TPIPEOP)) {
    int line = p->cur.line;
    Token op = Consume(p);
    TokFree(&op);
    Node *right = ParseOr(p);
    if (right->type == NCALL) {
      right->call.args =
          realloc(right->call.args, (right->call.argc + 1) * sizeof(Node *));
      right->call.isspread =
          realloc(right->call.isspread, (right->call.argc + 1) * sizeof(int));
      memmove(right->call.args + 1, right->call.args,
              right->call.argc * sizeof(Node *));
      memmove(right->call.isspread + 1, right->call.isspread,
              right->call.argc * sizeof(int));
      right->call.args[0] = left;
      right->call.isspread[0] = 0;
      right->call.argc++;
      left = right;
    } else {
      Node *callnode = NodeNew(NCALL, line);
      callnode->call.callee = right;
      callnode->call.argc = 1;
      callnode->call.args = malloc(sizeof(Node *));
      callnode->call.args[0] = left;
      callnode->call.isspread = calloc(1, sizeof(int));
      callnode->call.tc = 0;
      callnode->call.targs = NULL;
      left = callnode;
    }
  }
  return left;
}

#define BINLEVEL(name, next, ...)                                              \
  static Node *name(Parser *p) {                                               \
    Node *l = next(p);                                                         \
    for (;;) {                                                                 \
      TType tt = p->cur.type;                                                  \
      const char *op = NULL;                                                   \
      __VA_ARGS__                                                              \
      if (!op)                                                                 \
        break;                                                                 \
      int line = p->cur.line;                                                  \
      Token ot = Consume(p);                                                   \
      TokFree(&ot);                                                            \
      Node *r = next(p);                                                       \
      Node *n = NodeNew(NBINOP, line);                                         \
      n->binop.op = strdup(op);                                                \
      n->binop.left = l;                                                       \
      n->binop.right = r;                                                      \
      l = n;                                                                   \
    }                                                                          \
    return l;                                                                  \
  }

BINLEVEL(ParseOr, ParseAnd, if (tt == TOR) op = "||";)
BINLEVEL(ParseAnd, ParseBitOr, if (tt == TAND) op = "&&";)
BINLEVEL(ParseBitOr, ParseBitXor, if (tt == TPIPE) op = "|";)
BINLEVEL(ParseBitXor, ParseBitAnd, if (tt == TCARET) op = "^";)
BINLEVEL(ParseBitAnd, ParseEq, if (tt == TAMPERSAND) op = "&";)
BINLEVEL(ParseEq, ParseCmp, if (tt == TEQEQ) op = "==";
         else if (tt == TNEQ) op = "!=";)
static Node *ParseCmp(Parser *p) {
  Node *left = ParseShift(p);
  TType tt = p->cur.type;
  if (tt != TLT && tt != TGT && tt != TLTE && tt != TGTE)
    return left;
  /* collect a chain: expr op expr op expr ... */
  int cap = 4, count = 0;
  Node **exprs = malloc(cap * sizeof(Node *));
  char **ops   = malloc(cap * sizeof(char *));
  exprs[count++] = left;
  for (;;) {
    tt = p->cur.type;
    const char *op = NULL;
    if      (tt == TLT)  op = "<";
    else if (tt == TGT)  op = ">";
    else if (tt == TLTE) op = "<=";
    else if (tt == TGTE) op = ">=";
    else break;
    int line = p->cur.line;
    Token ot = Consume(p); TokFree(&ot);
    Node *right = ParseShift(p);
    if (count + 1 >= cap) {
      cap *= 2;
      exprs = realloc(exprs, cap * sizeof(Node *));
      ops   = realloc(ops,   cap * sizeof(char *));
    }
    ops[count - 1] = strdup(op);
    exprs[count++] = right;
    (void)line;
  }
  if (count == 2) {
    /* single comparison - produce ordinary NBINOP to stay compatible */
    Node *n = NodeNew(NBINOP, exprs[0]->line);
    n->binop.op    = ops[0];
    n->binop.left  = exprs[0];
    n->binop.right = exprs[1];
    free(exprs);
    free(ops);
    return n;
  }
  /* chained comparison: a < b < c */
  Node *n = NodeNew(NCHAINEDCMP, exprs[0]->line);
  n->chainedcmp.exprs = exprs;
  n->chainedcmp.ops   = ops;
  n->chainedcmp.count = count;
  return n;
}
BINLEVEL(ParseShift, ParseRange, if (tt == TSHL) op = "<<";
         else if (tt == TSHR) op = ">>";)

static Node *ParseRange(Parser *p) {
  Node *left = ParseAdd(p);
  if (Check(p, TDOTDOT)) {
    int line = p->cur.line;
    Token dd = Consume(p);
    TokFree(&dd);
    Node *right = ParseAdd(p);
    Node *n = NodeNew(NRANGE, line);
    n->range.start = left;
    n->range.end = right;
    return n;
  }
  return left;
}

BINLEVEL(ParseAdd, ParseMul, if (tt == TPLUS) op = "+";
         else if (tt == TMINUS) op = "-";)
BINLEVEL(ParseMul, ParseUnary, if (tt == TSTAR) op = "*";
         else if (tt == TSLASH) op = "/"; else if (tt == TPERCENT) op = "%"; else if (tt == TAT) op = "@";)

static int IsTernary(Parser *p) {
  Parser pc = *p;
  int level = 0;
  int count = 0;
  while (1) {
    Token t = Consume(&pc);
    if (t.type == TEOF) {
      if (count >= 2) {
        TokFree(&t);
      }
      break;
    }
    if (t.type == TLPAREN || t.type == TLBRACKET || t.type == TLBRACE) {
      level++;
    } else if (t.type == TRPAREN || t.type == TRBRACKET || t.type == TRBRACE) {
      level--;
      if (level < 0) {
        if (count >= 2) {
          TokFree(&t);
        }
        break;
      }
    } else if (t.type == TCOLON && level == 0) {
      if (count >= 2) {
        TokFree(&t);
      }
      return 1;
    } else if (t.type == TSEMI && level == 0) {
      if (count >= 2) {
        TokFree(&t);
      }
      break;
    }
    if (count >= 2) {
      TokFree(&t);
    }
    count++;
  }
  return 0;
}

static Node *ParsePostfix(Parser *p);

static Node *ParseAs(Parser *p) {
  Node *left = ParsePostfix(p);
  while (Check(p, TAS)) {
    int line = p->cur.line;
    Token op = Consume(p);
    TokFree(&op);
    char *target = ParseType(p);
    Node *n = NodeNew(NCAST, line);
    n->cast.expr = left;
    n->cast.targettype = target;
    left = n;
  }
  return left;
}

static Node *ParseUnary(Parser *p) {
  if (Check(p, TAMPERSAND)) {
    int line = p->cur.line;
    Token amp = Consume(p);
    TokFree(&amp);
    int is_mut = 0;
    if (Check(p, TLIFETIME)) {
      Token life = Consume(p);
      TokFree(&life);
    }
    if (Check(p, TMUT)) {
      Token mt = Consume(p);
      TokFree(&mt);
      is_mut = 1;
    }
    Node *operand = ParseUnary(p);
    Node *n = NodeNew(is_mut ? NMUTREF : NREF, line);
    n->refnode.expr = operand;
    n->refnode.is_mutable = is_mut;
    return n;
  }
  if (Check(p, TNOT) || Check(p, TMINUS) || Check(p, TTILDE)) {
    int line = p->cur.line;
    Token op = Consume(p);
    char *ops = strdup(op.val);
    TokFree(&op);
    Node *operand = ParseUnary(p);
    Node *n = NodeNew(NUNOP, line);
    n->binop.op = ops;
    n->binop.left = operand;
    n->binop.right = NULL;
    return n;
  }
  if (Check(p, TAWAIT)) {
    int line = p->cur.line;
    Token op = Consume(p);
    TokFree(&op);
    Node *operand = ParseUnary(p);
    Node *timeout = NULL;
    if (Check(p, TIDENT) && !strcmp(p->cur.val, "with_timeout")) {
      Token wt = Consume(p);
      TokFree(&wt);
      timeout = ParserExpr(p);
      if (Check(p, TIDENT) && (!strcmp(p->cur.val, "s") || !strcmp(p->cur.val, "ms"))) {
        Token unit = Consume(p);
        if (!strcmp(unit.val, "s")) {
          Node *mul = NodeNew(NBINOP, timeout->line);
          mul->binop.op = strdup("*");
          mul->binop.left = timeout;
          mul->binop.right = NodeNew(NINT, timeout->line);
          mul->binop.right->ival = 1000;
          timeout = mul;
        }
        TokFree(&unit);
      }
    }
    Node *n = NodeNew(NAWAIT, line);
    n->awaitn.expr = operand;
    n->awaitn.timeout = timeout;
    return n;
  }
  if (Check(p, TSPAWN)) {
    int line = p->cur.line;
    Token op = Consume(p);
    TokFree(&op);
    Node *operand = ParseUnary(p);
    Node *n = NodeNew(NSPAWN, line);
    n->spawnn.expr = operand;
    return n;
  }
  return ParseAs(p);
}


/* Lookahead: does the '{' starting at p->cur begin a trailing lambda?
   A trailing lambda looks like: { IDENT => ... } or { IDENT, IDENT => ... }
   We speculatively scan: { id (,id)* => */
static int CheckIsTrailingLambda(Parser *p) {
  if (!Check(p, TLBRACE)) return 0;
  /* Use a saved copy of the parser state for speculative scanning */
  Parser saved = *p;
  if (saved.cur.val) saved.cur.val = strdup(saved.cur.val);
  if (saved.peek.val) saved.peek.val = strdup(saved.peek.val);
  int savedsuppress = p->suppresserrors;
  p->suppresserrors = 1;
  int savederr = p->err;
  Token lb = Consume(p); TokFree(&lb);
  int ok = 0;
  /* expect at least one identifier */
  if (Check(p, TIDENT) || Check(p, TVOID) || Check(p, TINT) || Check(p, TSTR) || Check(p, TBOOL)) {
    Token id = Consume(p); TokFree(&id);
    /* optional further params: , id */
    while (Check(p, TCOMMA)) {
      Token cm = Consume(p); TokFree(&cm);
      if (Check(p, TIDENT) || Check(p, TVOID) || Check(p, TINT) || Check(p, TSTR) || Check(p, TBOOL)) {
        Token id2 = Consume(p); TokFree(&id2);
      } else break;
    }
    if (Check(p, TFATARROW)) ok = 1;
  }
  /* restore parser */
  p->suppresserrors = savedsuppress;
  p->err = savederr;
  TokFree(&p->cur); TokFree(&p->peek);
  *p = saved;
  return ok;
}

/* Parse a trailing lambda: { x => expr } or { x, y => expr }
   Produces an NFNDECL node (anonymous function). */
static Node *ParseTrailingLambda(Parser *p) {
  static int lambdacounter = 0;
  char lname[64];
  snprintf(lname, sizeof(lname), "__lambda_%d", lambdacounter++);
  int line = p->cur.line;
  Token lb = Expect(p, TLBRACE); TokFree(&lb);
  /* parse parameter list until => */
  int pc = 0, pcap = 4;
  char **pnames = malloc(pcap * sizeof(char *));
  char **ptypes = malloc(pcap * sizeof(char *));
  Node **pdefaults = malloc(pcap * sizeof(Node *));
  while (!Check(p, TFATARROW) && !Check(p, TEOF)) {
    if (pc >= pcap) {
      pcap *= 2;
      pnames   = realloc(pnames,   pcap * sizeof(char *));
      ptypes   = realloc(ptypes,   pcap * sizeof(char *));
      pdefaults= realloc(pdefaults,pcap * sizeof(Node *));
    }
    Token pn = Expect(p, TIDENT);
    pnames[pc]    = strdup(pn.val);
    ptypes[pc]    = NULL;
    pdefaults[pc] = NULL;
    TokFree(&pn);
    pc++;
    if (!Check(p, TFATARROW)) {
      Token cm = Expect(p, TCOMMA); TokFree(&cm);
    }
  }
  Token arr = Expect(p, TFATARROW); TokFree(&arr);
  /* body: either a block { ... } or a single expression */
  Node *body = NULL;
  if (Check(p, TLBRACE)) {
    body = ParseBlock(p);
  } else {
    /* single expression: wrap in implicit return block */
    Node *retexpr = ParserExpr(p);
    Node *retstmt = NodeNew(NRETURN, line);
    retstmt->single.expr = retexpr;
    retstmt->single.var  = NULL;
    body = NodeNew(NBLOCK, line);
    body->block.count = 1;
    body->block.stmts = malloc(sizeof(Node *));
    body->block.stmts[0] = retstmt;
  }
  Token rb = Expect(p, TRBRACE); TokFree(&rb);
  Node *fn = NodeNew(NFNDECL, line);
  fn->fn.name      = strdup(lname);
  fn->fn.ret       = NULL;
  fn->fn.pnames    = pnames;
  fn->fn.ptypes    = ptypes;
  fn->fn.pdefaults = pdefaults;
  fn->fn.pc        = pc;
  fn->fn.isvariadic= 0;
  fn->fn.requires  = NULL; fn->fn.req_count = 0;
  fn->fn.ensures   = NULL; fn->fn.ens_count = 0;
  fn->fn.body      = body;
  fn->fn.effect    = NULL;
  return fn;
}

/* Lookahead: does the '{' starting at p->cur begin a struct initializer?
   Struct init looks like: { ...expr, field: val } or { field: val }
   We distinguish from a map literal / block by requiring spread or ident:colon. */
static int CheckIsStructInit(Parser *p) {
  if (!Check(p, TLBRACE)) return 0;
  /* { ...  => struct update (spread) */
  if (Check2(p, TDOTDOTDOT)) return 1;
  /* { IDENT : => struct field init (but NOT string: that's a map) */
  /* peek one token ahead */
  if (p->peek.type == TIDENT || p->peek.type == TACTOR || (p->peek.val && p->peek.type >= 0)) {
    /* We need 3 tokens: '{', IDENT, ':' - but we only have 2 lookahead.
       We'll use a saved-state scan. */
    Parser saved = *p;
    if (saved.cur.val) saved.cur.val = strdup(saved.cur.val);
    if (saved.peek.val) saved.peek.val = strdup(saved.peek.val);
    int savedsuppress = p->suppresserrors;
    p->suppresserrors = 1;
    int savederr = p->err;
    Token lb = Consume(p); TokFree(&lb);
    int ok = 0;
    if (Check(p, TIDENT) || Check(p, TACTOR) || (p->cur.val && p->cur.type >= 0)) {
      Token id = Consume(p); TokFree(&id);
      if (Check(p, TCOLON)) ok = 1;
    }
    p->suppresserrors = savedsuppress;
    p->err = savederr;
    TokFree(&p->cur); TokFree(&p->peek);
    *p = saved;
    return ok;
  }
  return 0;
}
static char *ReConstructTypeName(Node *n) {
  if (!n) return NULL;
  if (n->type == NIDENT) {
    return strdup(n->sval);
  }
  if (n->type == NMEMBER) {
    char *obj_name = ReConstructTypeName(n->member.obj);
    if (!obj_name) return NULL;
    char *buf = malloc(strlen(obj_name) + 1 + strlen(n->member.field) + 1);
    sprintf(buf, "%s.%s", obj_name, n->member.field);
    free(obj_name);
    return buf;
  }
  return NULL;
}

/* Parse a struct initializer/updater expression.
   typename may be NULL for anonymous (spread-based) form.
   Syntax: { ...spread_expr, field1: val1, field2: val2 }
   or:     { field1: val1, field2: val2 } */
static Node *ParseStructinit(Parser *p, char *typename_str) {
  int line = p->cur.line;
  Token lb = Expect(p, TLBRACE); TokFree(&lb);
  Node *spread = NULL;
  int fc = 0, fcap = 4;
  char **fnames = malloc(fcap * sizeof(char *));
  Node **fvals  = malloc(fcap * sizeof(Node *));
  int first = 1;
  while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
    if (first && Check(p, TDOTDOTDOT)) {
      Token dt = Consume(p); TokFree(&dt);
      spread = ParserExpr(p);
      first = 0;
      if (!Check(p, TRBRACE)) { Token cm = Expect(p, TCOMMA); TokFree(&cm); }
      continue;
    }
    first = 0;
    if (fc >= fcap) {
      fcap *= 2;
      fnames = realloc(fnames, fcap * sizeof(char *));
      fvals  = realloc(fvals,  fcap * sizeof(Node *));
    }
    Token kn;
    if (Check(p, TIDENT) || Check(p, TACTOR) || (p->cur.val && p->cur.type >= 0)) {
      kn = Consume(p);
    } else {
      kn = Expect(p, TIDENT);
    }
    fnames[fc] = strdup(kn.val);
    TokFree(&kn);
    Token co = Expect(p, TCOLON); TokFree(&co);
    fvals[fc] = ParserExpr(p);
    fc++;
    if (!Check(p, TRBRACE)) { Token cm = Expect(p, TCOMMA); TokFree(&cm); }
  }
  Token rb = Expect(p, TRBRACE); TokFree(&rb);
  Node *n = NodeNew(NSTRUCTINIT, line);
  n->structinit.typename = typename_str ? strdup(typename_str) : NULL;
  n->structinit.spread   = spread;
  n->structinit.fnames   = fnames;
  n->structinit.fvals    = fvals;
  n->structinit.fc       = fc;
  return n;
}

static Node *ParsePostfix(Parser *p) {

  Node *expr = ParsePrimary(p);
  for (;;) {
    int line = p->cur.line;
    if (Check(p, TLPAREN)) {
      Token lp = Consume(p);
      TokFree(&lp);
      int ac = 0, acap = 4;
      Node **args = malloc(acap * sizeof(Node *));
      int *isspread = calloc(acap, sizeof(int));
      while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        if (ac == acap) {
          acap *= 2;
          args = realloc(args, acap * sizeof(Node *));
          isspread = realloc(isspread, acap * sizeof(int));
        }
        isspread[ac] = 0;
        if (Check(p, TDOTDOTDOT)) {
          Token dt = Consume(p);
          TokFree(&dt);
          isspread[ac] = 1;
        }
        args[ac++] = ParserExpr(p);
        if (!Check(p, TRPAREN)) {
          Token cm = Expect(p, TCOMMA);
          TokFree(&cm);
        }
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      Node *n = NodeNew(NCALL, line);
      n->call.callee = expr;
      n->call.args = args;
      n->call.argc = ac;
      n->call.isspread = isspread;
      n->call.targs = NULL;
      n->call.tc = 0;
      expr = n;
      /* trailing lambda: fn(args) { x => body } */
      while (CheckIsTrailingLambda(p)) {
        Node *lam = ParseTrailingLambda(p);
        /* append lambda as extra argument */
        Node *call = expr; /* expr is now the NCALL */
        call->call.argc++;
        call->call.args = realloc(call->call.args, call->call.argc * sizeof(Node *));
        call->call.isspread = realloc(call->call.isspread, call->call.argc * sizeof(int));
        call->call.args[call->call.argc - 1] = lam;
        call->call.isspread[call->call.argc - 1] = 0;
      }
    } else if (Check(p, TNOT) && expr->type == NIDENT && Check2(p, TLPAREN)) {
      Token nott = Consume(p);
      TokFree(&nott);
      Token lp = Consume(p);
      TokFree(&lp);
      int ac = 0, acap = 4;
      Node **args = malloc(acap * sizeof(Node *));
      int *isspread = calloc(acap, sizeof(int));
      while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        if (ac == acap) {
          acap *= 2;
          args = realloc(args, acap * sizeof(Node *));
          isspread = realloc(isspread, acap * sizeof(int));
        }
        isspread[ac] = 0;
        if (Check(p, TDOTDOTDOT)) {
          Token dt = Consume(p);
          TokFree(&dt);
          isspread[ac] = 1;
        }
        args[ac++] = ParserExpr(p);
        if (!Check(p, TRPAREN)) {
          Token cm = Expect(p, TCOMMA);
          TokFree(&cm);
        }
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      Node *n = NodeNew(NMACROCALL, line);
      n->call.callee = expr;
      n->call.args = args;
      n->call.argc = ac;
      n->call.isspread = isspread;
      n->call.targs = NULL;
      n->call.tc = 0;
      expr = n;
    } else if (Check(p, TLT) && expr->type == NIDENT) {
      // backtracking peek for template call: expr<types>(args)
      Parser saved = *p;
      if (saved.cur.val)
        saved.cur.val = strdup(saved.cur.val);
      if (saved.peek.val)
        saved.peek.val = strdup(saved.peek.val);
      int savedsuppress = p->suppresserrors;
      p->suppresserrors = 1; // suppress errors during speculative parse
      int savederr = p->err;
      int istemplate = 0;
      Token lt = Consume(p);
      TokFree(&lt);
      int tc = 0, tcap = 4;
      char **targs = malloc(tcap * sizeof(char *));
      while (!Check(p, TGT) && !Check(p, TEOF)) {
        char *tname = ParseType(p);
        if (tname) {
          if (tc == tcap) {
            tcap *= 2;
            targs = realloc(targs, tcap * sizeof(char *));
          }
          targs[tc++] = tname;
        } else {
          break;
        }
        if (!Check(p, TGT)) {
          if (!Match(p, TCOMMA))
            break;
        }
      }
      if (Check(p, TGT)) {
        Token gt = Consume(p);
        TokFree(&gt);
        if (Check(p, TLPAREN)) {
          istemplate = 1;
        }
      }
      p->suppresserrors = savedsuppress; // restore error reporting
      if (istemplate) {
        p->suppresserrors = 0; // restore error reporting
        Token lp = Expect(p, TLPAREN);
        TokFree(&lp);
        int ac = 0, acap = 4;
        Node **args = malloc(acap * sizeof(Node *));
        int *isspread = calloc(acap, sizeof(int));
        while (!Check(p, TRPAREN) && !Check(p, TEOF)) {
          if (ac == acap) {
            acap *= 2;
            args = realloc(args, acap * sizeof(Node *));
            isspread = realloc(isspread, acap * sizeof(int));
          }
          isspread[ac] = 0;
          if (Check(p, TDOTDOTDOT)) {
            Token dt = Consume(p);
            TokFree(&dt);
            isspread[ac] = 1;
          }
          args[ac++] = ParserExpr(p);
          if (!Check(p, TRPAREN)) {
            Token cm = Expect(p, TCOMMA);
            TokFree(&cm);
          }
        }
        Token rp = Expect(p, TRPAREN);
        TokFree(&rp);
        Node *n = NodeNew(NTEMPLATECALL, line);
        n->call.callee = expr;
        n->call.args = args;
        n->call.argc = ac;
        n->call.isspread = isspread;
        n->call.targs = targs;
        n->call.tc = tc;
        expr = n;
      } else {
        p->err = savederr; // restore error state from before speculation
        for (int i = 0; i < tc; i++)
          free(targs[i]);
        free(targs);
        TokFree(&p->cur);
        TokFree(&p->peek);
        *p = saved;
        break; // continue parsing binary operations
      }
    } else if (Check(p, TLBRACKET)) {
      Token lb = Consume(p);
      TokFree(&lb);
      Node *idx = ParserExpr(p);
      Token rb = Expect(p, TRBRACKET);
      TokFree(&rb);
      Node *n = NodeNew(NINDEX, line);
      n->index.arr = expr;
      n->index.idx = idx;
      expr = n;
    } else if (Check(p, TDOT)) {
      Token dt = Consume(p);
      TokFree(&dt);
      Token fn;
      if (Check(p, TIDENT) || Check(p, TTYPE) || (p->cur.val && p->cur.type >= 0)) {
        fn = Consume(p);
      } else {
        fn = Expect(p, TIDENT);
      }
      char *field = fn.val ? strdup(fn.val) : strdup("");
      TokFree(&fn);
      Node *n = NodeNew(NMEMBER, line);
      n->member.obj = expr;
      n->member.field = field;
      expr = n;
    } else if (Check(p, TPLUSPLUS)) {
      Token t = Consume(p);
      TokFree(&t);
      Node *n = NodeNew(NINC, line);
      n->incdec.expr = expr;
      expr = n;
    } else if (Check(p, TMINUSMINUS)) {
      Token t = Consume(p);
      TokFree(&t);
      Node *n = NodeNew(NDEC, line);
      n->incdec.expr = expr;
      expr = n;
    } else if (Check(p, TQUESTIONDOT)) {
      Token qd = Consume(p);
      TokFree(&qd);
      if (Check(p, TLBRACKET)) {
        Token lb = Consume(p);
        TokFree(&lb);
        Node *idx = ParserExpr(p);
        Token rb = Expect(p, TRBRACKET);
        TokFree(&rb);
        Node *n = NodeNew(NOPTCHAIN, line);
        n->optchain.obj = expr;
        n->optchain.field = NULL;
        n->optchain.idx = idx;
        expr = n;
      } else {
        Token fn;
        if (Check(p, TIDENT) || Check(p, TTYPE) || (p->cur.val && p->cur.type >= 0)) {
          fn = Consume(p);
        } else {
          fn = Expect(p, TIDENT);
        }
        char *field = fn.val ? strdup(fn.val) : strdup("");
        TokFree(&fn);
        Node *n = NodeNew(NOPTCHAIN, line);
        n->optchain.obj = expr;
        n->optchain.field = field;
        n->optchain.idx = NULL;
        expr = n;
      }
    } else if (Check(p, TQUESTION)) {
      if (!IsTernary(p)) {
        Token qt = Consume(p);
        TokFree(&qt);
        Node *n = NodeNew(NTRYPROPAGATE, line);
        n->single.expr = expr;
        expr = n;
      } else {
        break;
      }
    } else if (CheckIsStructInit(p)) {
      char *tname = ReConstructTypeName(expr);
      if (tname) {
        Node *si = ParseStructinit(p, tname);
        free(tname);
        NodeFree(expr);
        expr = si;
      } else {
        break;
      }
    } else if (CheckIsTrailingLambda(p)) {
      Node *lam = ParseTrailingLambda(p);
      Node *n = NodeNew(NCALL, line);
      n->call.callee = expr;
      n->call.args = malloc(sizeof(Node *));
      n->call.args[0] = lam;
      n->call.argc = 1;
      n->call.isspread = calloc(1, sizeof(int));
      n->call.targs = NULL;
      n->call.tc = 0;
      expr = n;
    } else if (Check(p, TAS)) {
      Token astok = Consume(p);
      TokFree(&astok);
      char *targettype = ParseType(p);
      Node *n = NodeNew(NCAST, line);
      n->cast.expr = expr;
      n->cast.targettype = targettype;
      expr = n;
    } else
      break;
  }
  return expr;
}

static Node *ParsePrimary(Parser *p) {
  int line = p->cur.line;
  if (Check(p, TINT) || Check(p, TINTLIT)) {
    Token t = Consume(p);
    Node *n = NodeNew(NINT, line);
    n->ival = atoll(t.val);
    TokFree(&t);
    return n;
  }
  if (Check(p, TFLOAT) || Check(p, TFLOATLIT)) {
    Token t = Consume(p);
    Node *n = NodeNew(NFLOAT, line);
    n->fval = atof(t.val);
    TokFree(&t);
    return n;
  }
  if (Check(p, TSTR) || Check(p, TSTRLIT)) {
    Token t = Consume(p);
    Node *n = ParseInterpolatedStr(t.val, line);
    TokFree(&t);
    return n;
  }
  if (Check(p, TTRUE) || Check(p, TFALSE)) {
    Token t = Consume(p);
    Node *n = NodeNew(NBOOL, line);
    n->sval = strdup(t.val);
    TokFree(&t);
    return n;
  }
  if (Check(p, TNULL)) {
    Token t = Consume(p);
    TokFree(&t);
    return NodeNew(NNULL, line);
  }
  if (Check(p, TINPUT)) {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    Node *promptnode = NULL;
    if (Check(p, TLPAREN)) {
      Token lp = Consume(p);
      TokFree(&lp);
      if (!Check(p, TRPAREN) && !Check(p, TEOF)) {
        promptnode = ParserExpr(p);
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
    }
    Node *n = NodeNew(NINPUT, line);
    n->single.var = NULL;
    n->single.expr = promptnode;
    return n;
  }
  if (Check(p, TMATCH)) {
    return ParsePatternMatch(p);
  }
  if (Check(p, TNURSERY)) {
    return ParseNursery(p);
  }
  if (Check(p, TREGION)) {
    return ParseRegion(p);
  }
  if (Check(p, TACTOR)) {
    int line = p->cur.line;
    Token t = Consume(p);
    Node *n = NodeNew(NIDENT, line);
    n->sval = strdup("actor");
    TokFree(&t);
    return n;
  }
  if (Check(p, TGPU)) {
    int line = p->cur.line;
    Token t = Consume(p);
    TokFree(&t);
    Node *callee = NodeNew(NIDENT, line);
    callee->sval = strdup("GpuGetGlobalState");
    Node *call = NodeNew(NCALL, line);
    call->call.callee = callee;
    call->call.argc = 0;
    call->call.args = NULL;
    call->call.isspread = NULL;
    call->call.tc = 0;
    call->call.targs = NULL;
    return call;
  }
  if (Check(p, TPERFORM)) {
    return ParsePerform(p);
  }
  if (Check(p, THANDLE)) {
    return ParseHandle(p);
  }
  if (Check(p, TRESUME)) {
    return ParseResume(p);
  }
  if (Check(p, TATOMIC)) {
    return ParseAtomic(p);
  }
  if (Check(p, TFN)) {
    static int anoncounter = 0;
    char anonname[64];
    snprintf(anonname, sizeof(anonname), "__anon_%d", anoncounter++);
    int fnline = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    return ParseFnBody(p, fnline, strdup(anonname));
  }
  if (Check(p, TASM)) {
    int line = p->cur.line;
    Token kw = Consume(p);
    TokFree(&kw);
    char *asm_str = NULL;
    if (Check(p, TLPAREN)) {
      Token lp = Consume(p);
      TokFree(&lp);
      if (Check(p, TSTR) || Check(p, TSTRLIT)) {
        Token st = Consume(p);
        asm_str = strdup(st.val);
        TokFree(&st);
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
    } else if (Check(p, TLBRACE)) {
      Token lb = Consume(p);
      TokFree(&lb);
      TypeStr ts;
      TypeStrInit(&ts);
      while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
        Token t = Consume(p);
        TypeStrAppend(&ts, t.val);
        TypeStrAppend(&ts, " ");
        TokFree(&t);
      }
      Token rb = Expect(p, TRBRACE);
      TokFree(&rb);
      asm_str = ts.buf;
    } else if (Check(p, TSTR) || Check(p, TSTRLIT)) {
      Token st = Consume(p);
      asm_str = strdup(st.val);
      TokFree(&st);
    }
    Node *n = NodeNew(NASM, line);
    n->asmstmt.code = asm_str ? asm_str : strdup("");
    return n;
  }
  if (Check(p, TCOMPTIME) || (Check(p, TIDENT) && strcmp(p->cur.val, "comptime") == 0)) {
    int line = p->cur.line;
    Token ct = Consume(p);
    TokFree(&ct);
    Node *expr = NULL;
    if (Check(p, TLBRACE)) {
      expr = ParseBlock(p);
    } else {
      expr = ParsePostfix(p);
    }
    Node *n = NodeNew(NCONST_EVAL, line);
    n->single.expr = expr;
    return n;
  }
  if (Check(p, TIDENT) || Check(p, TACTOR) || Check(p, TINT) || Check(p, TFLOAT) ||
      Check(p, TSTR) || Check(p, TBOOL) || Check(p, TVOID) || Check(p, TTYPE)) {
    Token t = Consume(p);
    char *name = strdup(t.val);
    TokFree(&t);

    /* Consume :: path segments: ident::ident::... or ident::* */
    while (Check(p, TCOLONCOLON)) {
      Token cctok = Consume(p);
      TokFree(&cctok);
      if (Check(p, TIDENT)) {
        Token seg = Consume(p);
        int newlen = (int)(strlen(name) + 2 + strlen(seg.val) + 1);
        char *newname = malloc(newlen);
        snprintf(newname, newlen, "%s::%s", name, seg.val);
        free(name);
        name = newname;
        TokFree(&seg);
      } else if (Check(p, TSTAR)) {
        Token star = Consume(p);
        TokFree(&star);
        int newlen = (int)(strlen(name) + 4);
        char *newname = malloc(newlen);
        snprintf(newname, newlen, "%s::*", name);
        free(name);
        name = newname;
        break;
      } else {
        break;
      }
    }

    if (Check(p, TLBRACE) && CheckIsStructInit(p)) {
      return ParseStructinit(p, name);
    }
    Node *n = NodeNew(NIDENT, line);
    n->sval = name;
    return n;
  }
  if (Check(p, TLPAREN)) {
    Token lp = Consume(p);
    TokFree(&lp);
    Node *first = ParserExpr(p);
    if (Check(p, TCOMMA)) {
      int cap = 4;
      int count = 0;
      Node **elems = malloc(cap * sizeof(Node *));
      elems[count++] = first;
      while (Check(p, TCOMMA)) {
        Token cm = Consume(p);
        TokFree(&cm);
        if (Check(p, TRPAREN))
          break;
        if (count == cap) {
          cap *= 2;
          elems = realloc(elems, cap * sizeof(Node *));
        }
        elems[count++] = ParserExpr(p);
      }
      Token rp = Expect(p, TRPAREN);
      TokFree(&rp);
      Node *n = NodeNew(NTUPLE, line);
      n->tuple.elems = elems;
      n->tuple.count = count;
      return n;
    }
    Token rp = Expect(p, TRPAREN);
    TokFree(&rp);
    return first;
  }
  if (Check(p, TLBRACKET)) {
    Token lb = Consume(p);
    TokFree(&lb);
    int ec = 0, ecap = 4;
    Node **elems = malloc(ecap * sizeof(Node *));
    int *isspread = calloc(ecap, sizeof(int));
    int first = 1;
    while (!Check(p, TRBRACKET) && !Check(p, TEOF)) {
      if (ec == ecap) {
        ecap *= 2;
        elems = realloc(elems, ecap * sizeof(Node *));
        isspread = realloc(isspread, ecap * sizeof(int));
      }
      isspread[ec] = 0;
      if (Check(p, TDOTDOTDOT)) {
        Token dt = Consume(p);
        TokFree(&dt);
        isspread[ec] = 1;
      }
      Node *expr = ParserExpr(p);
      if (first && Check(p, TSEMI)) {
        Token sc = Consume(p); TokFree(&sc);
        Node *c_expr = ParserExpr(p);
        Token rb = Expect(p, TRBRACKET); TokFree(&rb);
        if (c_expr->type == NINT) {
          int rcount = (int)c_expr->ival;
          Node **repeats = malloc(rcount * sizeof(Node *));
          repeats[0] = expr;
          for (int i = 1; i < rcount; i++) {
            repeats[i] = NodeCopy(expr);
          }
          Node *n = NodeNew(NARRLIT, line);
          n->arrlit.elems = repeats;
          n->arrlit.count = rcount;
          n->arrlit.isspread = calloc(rcount, sizeof(int));
          return n;
        }
      }
      if (first && Check(p, TFOR)) {
        Token fortok = Consume(p);
        TokFree(&fortok);
        Token vartok = Expect(p, TIDENT);
        char *varname = strdup(vartok.val);
        TokFree(&vartok);
        Token intok = Expect(p, TIN);
        TokFree(&intok);
        Node *iterexpr = ParserExpr(p);
        Node *condexpr = NULL;
        if (Match(p, TIF)) {
          condexpr = ParserExpr(p);
        }
        Token rb = Expect(p, TRBRACKET);
        TokFree(&rb);
        static int listcompcounter = 0;
        char listname[64];
        sprintf(listname, "__comp_%d", listcompcounter++);
        Node *vardecl = NodeNew(NVARDECL, line);
        vardecl->decl.name = strdup(listname);
        vardecl->decl.tname = NULL;
        vardecl->decl.init = NodeNew(NARRLIT, line);
        vardecl->decl.init->arrlit.elems = NULL;
        vardecl->decl.init->arrlit.count = 0;
        vardecl->decl.init->arrlit.isspread = NULL;
        vardecl->decl.isconstflag = 0;
        Node *appendcall = NodeNew(NCALL, line);
        appendcall->call.callee = NodeNew(NIDENT, line);
        appendcall->call.callee->sval = strdup("append");
        appendcall->call.argc = 2;
        appendcall->call.args = malloc(2 * sizeof(Node *));
        appendcall->call.args[0] = NodeNew(NIDENT, line);
        appendcall->call.args[0]->sval = strdup(listname);
        appendcall->call.args[1] = expr;
        appendcall->call.isspread = calloc(2, sizeof(int));
        appendcall->call.tc = 0;
        appendcall->call.targs = NULL;
        Node *appendstmt = NodeNew(NEXPRSTMT, line);
        appendstmt->single.expr = appendcall;
        appendstmt->single.var = NULL;
        Node *loopbody = NULL;
        if (condexpr) {
          Node *ifnode = NodeNew(NIF, line);
          ifnode->ifn.cond = condexpr;
          ifnode->ifn.then = appendstmt;
          ifnode->ifn.els = NULL;
          loopbody = ifnode;
        } else {
          loopbody = appendstmt;
        }
        Node *blockbody = NodeNew(NBLOCK, line);
        blockbody->block.count = 1;
        blockbody->block.stmts = malloc(sizeof(Node *));
        blockbody->block.stmts[0] = loopbody;
        Node *forin = NodeNew(NFORIN, line);
        forin->forin.var = varname;
        forin->forin.iter = iterexpr;
        forin->forin.body = blockbody;
        Node *retexpr = NodeNew(NIDENT, line);
        retexpr->sval = strdup(listname);
        Node *retstmt = NodeNew(NEXPRSTMT, line);
        retstmt->single.expr = retexpr;
        retstmt->single.var = NULL;
        Node *block = NodeNew(NBLOCK, line);
        block->block.count = 3;
        block->block.stmts = malloc(3 * sizeof(Node *));
        block->block.stmts[0] = vardecl;
        block->block.stmts[1] = forin;
        block->block.stmts[2] = retstmt;
        free(elems);
        free(isspread);
        return block;
      }
      elems[ec] = expr;
      ec++;
      first = 0;
      if (!Check(p, TRBRACKET)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token rb = Expect(p, TRBRACKET);
    TokFree(&rb);
    Node *n = NodeNew(NARRLIT, line);
    n->arrlit.elems = elems;
    n->arrlit.count = ec;
    n->arrlit.isspread = isspread;
    return n;
  }
  // struct initializer: { ...base, field: val } or { field: val }
  if (CheckIsStructInit(p)) {
    return ParseStructinit(p, NULL);
  }
  // map literal: { "key": expr, ... }
  if (Check(p, TLBRACE)) {
    Token lb = Consume(p);
    TokFree(&lb);
    int mc = 0, mcap = 4;
    char **keys = malloc(mcap * sizeof(char *));
    Node **vals = malloc(mcap * sizeof(Node *));
    while (!Check(p, TRBRACE) && !Check(p, TEOF)) {
      if (mc == mcap) {
        mcap *= 2;
        keys = realloc(keys, mcap * sizeof(char *));
        vals = realloc(vals, mcap * sizeof(Node *));
      }
      // key can be string literal or identifier
      char *key = NULL;
      if (Check(p, TSTRLIT)) {
        Token kt = Consume(p);
        key = strdup(kt.val);
        TokFree(&kt);
      } else {
        Token kt;
        if (Check(p, TIDENT) || Check(p, TACTOR) || (p->cur.val && p->cur.type >= 0)) {
          kt = Consume(p);
        } else {
          kt = Expect(p, TIDENT);
        }
        key = strdup(kt.val);
        TokFree(&kt);
      }
      Token co = Expect(p, TCOLON);
      TokFree(&co);
      Node *val = ParserExpr(p);
      keys[mc] = key;
      vals[mc] = val;
      mc++;
      if (!Check(p, TRBRACE)) {
        Token cm = Expect(p, TCOMMA);
        TokFree(&cm);
      }
    }
    Token rb = Expect(p, TRBRACE);
    TokFree(&rb);
    Node *n = NodeNew(NMAPLIT, line);
    n->maplit.keys = keys;
    n->maplit.vals = vals;
    n->maplit.count = mc;
    return n;
  }
  if (Check(p, TNEW)) {
    Token kw = Consume(p);
    TokFree(&kw);
    Token tn = Expect(p, TIDENT);
    Node *n = NodeNew(NNEW, line);
    n->newobj.name = strdup(tn.val);
    TokFree(&tn);

    char **targs = NULL;
    int tc = 0;
    if (Check(p, TLT)) {
      Token lt = Consume(p);
      TokFree(&lt);
      int tcap = 4;
      targs = malloc(tcap * sizeof(char *));
      while (!Check(p, TGT) && !Check(p, TEOF)) {
        char *arg = ParseType(p);
        if (tc == tcap) {
          tcap *= 2;
          targs = realloc(targs, tcap * sizeof(char *));
        }
        targs[tc++] = arg;
        if (!Check(p, TGT)) {
          Token cm = Expect(p, TCOMMA);
          TokFree(&cm);
        }
      }
      Token gt = Expect(p, TGT);
      TokFree(&gt);
    }
    n->newobj.targs = targs;
    n->newobj.tc = tc;

    Token lp = Expect(p, TLPAREN);
    TokFree(&lp);
    Token rp = Expect(p, TRPAREN);
    TokFree(&rp);
    return n;
  }
  if (Check(p, TDOTDOTDOT)) {
    Token dt = Consume(p);
    TokFree(&dt);
    Node *expr = ParsePrimary(p);
    Node *n = NodeNew(NSPREAD, line);
    n->spread.expr = expr;
    return n;
  }
  if (!p->suppresserrors) {
    if (p->error_count < 50) {
      fprintf(stderr, "Parser error on line %d: unexpected token '%s'\n", line,
              p->cur.val);
    } else if (p->error_count == 50) {
      fprintf(stderr, "... (further syntax errors suppressed)\n");
    }
    p->error_count++;
  }
  p->err = 1;
  Token bad = Consume(p);
  TokFree(&bad);
  return NodeNew(NNULL, line);
}

Node *Parse(const char *source, int *haderror) {
  Parser p;
  p.lex = LexNew(source);
  p.err = 0;
  p.suppresserrors = 0;
  p.recursion_depth = 0;
  p.error_count = 0;
  p.cur = LexNext(&p.lex);
  p.peek = LexNext(&p.lex);

  Node *prog = NodeNew(NPROGRAM, 1);
  int cap = 16;
  prog->block.stmts = malloc(cap * sizeof(Node *));
  prog->block.count = 0;
  while (!Check(&p, TEOF)) {
    Node *s = ParseStmt(&p);
    if (!s) {
      ParseSynchronize(&p);
      continue;
    }
    if (prog->block.count == cap) {
      cap *= 2;
      prog->block.stmts = realloc(prog->block.stmts, cap * sizeof(Node *));
    }
    prog->block.stmts[prog->block.count++] = s;
  }
  TokFree(&p.cur);
  TokFree(&p.peek);
  if (haderror)
    *haderror = p.err;
  return prog;
}

Node *ParseExpr(const char *source, int *haderror) {
  Parser p;
  p.lex = LexNew(source);
  p.err = 0;
  p.suppresserrors = 0;
  p.recursion_depth = 0;
  p.error_count = 0;
  p.cur = LexNext(&p.lex);
  p.peek = LexNext(&p.lex);
  Node *expr = ParserExpr(&p);
  if (!Check(&p, TEOF)) {
    p.err = 1;
  }
  TokFree(&p.cur);
  TokFree(&p.peek);
  if (haderror)
    *haderror = p.err;
  return expr;
}

static int hyg_counter = 0;

static void FindAndRenameLocals(Node *node, char **pnames, int pc, char ***old_names, char ***new_names, int *count, int *cap) {
    if (!node) return;
    
    if (node->type == NVARDECL || node->type == NCONSTDECL) {
        char *name = node->decl.name;
        if (name) {
            int is_param = 0;
            for (int i = 0; i < pc; i++) {
                if (strcmp(name, pnames[i]) == 0) { is_param = 1; break; }
            }
            if (!is_param) {
                int idx = -1;
                for (int i = 0; i < *count; i++) {
                    if (strcmp((*old_names)[i], name) == 0) { idx = i; break; }
                }
                if (idx < 0) {
                    char newname[256];
                    snprintf(newname, sizeof(newname), "__hyg_%s_%d", name, ++hyg_counter);
                    if (*count >= *cap) {
                        *cap *= 2;
                        *old_names = realloc(*old_names, *cap * sizeof(char*));
                        *new_names = realloc(*new_names, *cap * sizeof(char*));
                    }
                    (*old_names)[*count] = strdup(name);
                    (*new_names)[*count] = strdup(newname);
                    (*count)++;
                    
                    free(node->decl.name);
                    node->decl.name = strdup(newname);
                } else {
                    free(node->decl.name);
                    node->decl.name = strdup((*new_names)[idx]);
                }
            }
        }
    }
    
    if (node->type == NFNDECL) {
        char *name = node->fn.name;
        if (name && !strchr(name, '.')) {
            int is_param = 0;
            for (int i = 0; i < pc; i++) {
                if (strcmp(name, pnames[i]) == 0) { is_param = 1; break; }
            }
            if (!is_param) {
                int idx = -1;
                for (int i = 0; i < *count; i++) {
                    if (strcmp((*old_names)[i], name) == 0) { idx = i; break; }
                }
                if (idx < 0) {
                    char newname[256];
                    snprintf(newname, sizeof(newname), "__hyg_%s_%d", name, ++hyg_counter);
                    if (*count >= *cap) {
                        *cap *= 2;
                        *old_names = realloc(*old_names, *cap * sizeof(char*));
                        *new_names = realloc(*new_names, *cap * sizeof(char*));
                    }
                    (*old_names)[*count] = strdup(name);
                    (*new_names)[*count] = strdup(newname);
                    (*count)++;
                    
                    free(node->fn.name);
                    node->fn.name = strdup(newname);
                } else {
                    free(node->fn.name);
                    node->fn.name = strdup((*new_names)[idx]);
                }
            }
        }
    }

    switch (node->type) {
        case NBINOP: case NUNOP:
            FindAndRenameLocals(node->binop.left, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->binop.right, pnames, pc, old_names, new_names, count, cap);
            break;
        case NVARDECL: case NCONSTDECL:
            FindAndRenameLocals(node->decl.init, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->decl.isconst, pnames, pc, old_names, new_names, count, cap);
            break;
        case NFNDECL:
        case NMACRODECL:
            if (node->fn.pdefaults) {
                for (int i = 0; i < node->fn.pc; i++) {
                    FindAndRenameLocals(node->fn.pdefaults[i], pnames, pc, old_names, new_names, count, cap);
                }
            }
            FindAndRenameLocals(node->fn.body, pnames, pc, old_names, new_names, count, cap);
            break;
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < node->block.count; i++) {
                FindAndRenameLocals(node->block.stmts[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        case NIF:
            FindAndRenameLocals(node->ifn.cond, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->ifn.then, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->ifn.els, pnames, pc, old_names, new_names, count, cap);
            break;
        case NTERNARY:
            FindAndRenameLocals(node->ternary.cond, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->ternary.tval, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->ternary.fval, pnames, pc, old_names, new_names, count, cap);
            break;
        case NWHILE:
            FindAndRenameLocals(node->whilen.cond, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->whilen.body, pnames, pc, old_names, new_names, count, cap);
            break;
        case NFOR:
            if (node->forn.init) FindAndRenameLocals(node->forn.init, pnames, pc, old_names, new_names, count, cap);
            if (node->forn.cond) FindAndRenameLocals(node->forn.cond, pnames, pc, old_names, new_names, count, cap);
            if (node->forn.post) FindAndRenameLocals(node->forn.post, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->forn.body, pnames, pc, old_names, new_names, count, cap);
            break;
        case NFORIN:
            FindAndRenameLocals(node->forin.iter, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->forin.body, pnames, pc, old_names, new_names, count, cap);
            break;
        case NEXPRSTMT:
        case NRETURN:
            FindAndRenameLocals(node->single.expr, pnames, pc, old_names, new_names, count, cap);
            break;
        case NTRYCATCH:
            FindAndRenameLocals(node->trycatch.tryblock, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->trycatch.catchblock, pnames, pc, old_names, new_names, count, cap);
            if (node->trycatch.finallyblock) FindAndRenameLocals(node->trycatch.finallyblock, pnames, pc, old_names, new_names, count, cap);
            break;
        case NTHROW:
            FindAndRenameLocals(node->throwstmt.expr, pnames, pc, old_names, new_names, count, cap);
            break;
        case NCOALESCE:
            FindAndRenameLocals(node->coalesce.left, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->coalesce.right, pnames, pc, old_names, new_names, count, cap);
            break;
        case NOPTCHAIN:
            FindAndRenameLocals(node->optchain.obj, pnames, pc, old_names, new_names, count, cap);
            if (node->optchain.idx) FindAndRenameLocals(node->optchain.idx, pnames, pc, old_names, new_names, count, cap);
            break;
        case NSTRUCTINIT:
            if (node->structinit.spread) FindAndRenameLocals(node->structinit.spread, pnames, pc, old_names, new_names, count, cap);
            for (int i = 0; i < node->structinit.fc; i++) {
                FindAndRenameLocals(node->structinit.fvals[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < node->chainedcmp.count + 1; i++) {
                FindAndRenameLocals(node->chainedcmp.exprs[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        case NRANGE:
            FindAndRenameLocals(node->range.start, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->range.end, pnames, pc, old_names, new_names, count, cap);
            break;
        case NASSIGN:
            FindAndRenameLocals(node->assign.target, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->assign.value, pnames, pc, old_names, new_names, count, cap);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            FindAndRenameLocals(node->call.callee, pnames, pc, old_names, new_names, count, cap);
            for (int i = 0; i < node->call.argc; i++) {
                FindAndRenameLocals(node->call.args[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        case NINDEX:
            FindAndRenameLocals(node->index.arr, pnames, pc, old_names, new_names, count, cap);
            FindAndRenameLocals(node->index.idx, pnames, pc, old_names, new_names, count, cap);
            break;
        case NMEMBER:
            FindAndRenameLocals(node->member.obj, pnames, pc, old_names, new_names, count, cap);
            break;
        case NARRLIT:
            for (int i = 0; i < node->arrlit.count; i++) {
                FindAndRenameLocals(node->arrlit.elems[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        case NTUPLE:
            for (int i = 0; i < node->tuple.count; i++) {
                FindAndRenameLocals(node->tuple.elems[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < node->maplit.count; i++) {
                FindAndRenameLocals(node->maplit.vals[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        case NINC: case NDEC:
            FindAndRenameLocals(node->incdec.expr, pnames, pc, old_names, new_names, count, cap);
            break;
        case NAWAIT:
            FindAndRenameLocals(node->awaitn.expr, pnames, pc, old_names, new_names, count, cap);
            break;
        case NSPAWN:
            FindAndRenameLocals(node->spawnn.expr, pnames, pc, old_names, new_names, count, cap);
            break;
        case NSELECT:
            for (int i = 0; i < node->selectn.count; i++) {
                FindAndRenameLocals(node->selectn.bodies[i], pnames, pc, old_names, new_names, count, cap);
            }
            if (node->selectn.defaultbody) FindAndRenameLocals(node->selectn.defaultbody, pnames, pc, old_names, new_names, count, cap);
            break;
        case NMATCH:
            FindAndRenameLocals(node->match.expr, pnames, pc, old_names, new_names, count, cap);
            for (int i = 0; i < node->match.count; i++) {
                FindAndRenameLocals(node->match.bodies[i], pnames, pc, old_names, new_names, count, cap);
            }
            break;
        default:
            break;
    }
}

static void SubstituteHygIdents(Node *node, char **old_names, char **new_names, int count) {
    if (!node) return;
    
    if (node->type == NIDENT && node->sval) {
        for (int i = 0; i < count; i++) {
            if (strcmp(node->sval, old_names[i]) == 0) {
                free(node->sval);
                node->sval = strdup(new_names[i]);
                break;
            }
        }
    }
    
    switch (node->type) {
        case NBINOP: case NUNOP:
            SubstituteHygIdents(node->binop.left, old_names, new_names, count);
            SubstituteHygIdents(node->binop.right, old_names, new_names, count);
            break;
        case NREF: case NMUTREF:
            SubstituteHygIdents(node->refnode.expr, old_names, new_names, count);
            break;
        case NVARDECL: case NCONSTDECL:
            SubstituteHygIdents(node->decl.init, old_names, new_names, count);
            SubstituteHygIdents(node->decl.isconst, old_names, new_names, count);
            break;
        case NFNDECL:
        case NMACRODECL:
            if (node->fn.pdefaults) {
                for (int i = 0; i < node->fn.pc; i++) {
                    SubstituteHygIdents(node->fn.pdefaults[i], old_names, new_names, count);
                }
            }
            SubstituteHygIdents(node->fn.body, old_names, new_names, count);
            break;
        case NBLOCK:
        case NPROGRAM:
            for (int i = 0; i < node->block.count; i++) {
                SubstituteHygIdents(node->block.stmts[i], old_names, new_names, count);
            }
            break;
        case NIF:
            SubstituteHygIdents(node->ifn.cond, old_names, new_names, count);
            SubstituteHygIdents(node->ifn.then, old_names, new_names, count);
            SubstituteHygIdents(node->ifn.els, old_names, new_names, count);
            break;
        case NTERNARY:
            SubstituteHygIdents(node->ternary.cond, old_names, new_names, count);
            SubstituteHygIdents(node->ternary.tval, old_names, new_names, count);
            SubstituteHygIdents(node->ternary.fval, old_names, new_names, count);
            break;
        case NWHILE:
            SubstituteHygIdents(node->whilen.cond, old_names, new_names, count);
            SubstituteHygIdents(node->whilen.body, old_names, new_names, count);
            break;
        case NFOR:
            if (node->forn.init) SubstituteHygIdents(node->forn.init, old_names, new_names, count);
            if (node->forn.cond) SubstituteHygIdents(node->forn.cond, old_names, new_names, count);
            if (node->forn.post) SubstituteHygIdents(node->forn.post, old_names, new_names, count);
            SubstituteHygIdents(node->forn.body, old_names, new_names, count);
            break;
        case NFORIN:
            SubstituteHygIdents(node->forin.iter, old_names, new_names, count);
            SubstituteHygIdents(node->forin.body, old_names, new_names, count);
            break;
        case NEXPRSTMT:
        case NRETURN:
            SubstituteHygIdents(node->single.expr, old_names, new_names, count);
            break;
        case NTRYCATCH:
            SubstituteHygIdents(node->trycatch.tryblock, old_names, new_names, count);
            SubstituteHygIdents(node->trycatch.catchblock, old_names, new_names, count);
            if (node->trycatch.finallyblock) SubstituteHygIdents(node->trycatch.finallyblock, old_names, new_names, count);
            break;
        case NTHROW:
            SubstituteHygIdents(node->throwstmt.expr, old_names, new_names, count);
            break;
        case NCOALESCE:
            SubstituteHygIdents(node->coalesce.left, old_names, new_names, count);
            SubstituteHygIdents(node->coalesce.right, old_names, new_names, count);
            break;
        case NOPTCHAIN:
            SubstituteHygIdents(node->optchain.obj, old_names, new_names, count);
            if (node->optchain.idx) SubstituteHygIdents(node->optchain.idx, old_names, new_names, count);
            break;
        case NSTRUCTINIT:
            if (node->structinit.spread) SubstituteHygIdents(node->structinit.spread, old_names, new_names, count);
            for (int i = 0; i < node->structinit.fc; i++) {
                SubstituteHygIdents(node->structinit.fvals[i], old_names, new_names, count);
            }
            break;
        case NCHAINEDCMP:
            for (int i = 0; i < node->chainedcmp.count + 1; i++) {
                SubstituteHygIdents(node->chainedcmp.exprs[i], old_names, new_names, count);
            }
            break;
        case NRANGE:
            SubstituteHygIdents(node->range.start, old_names, new_names, count);
            SubstituteHygIdents(node->range.end, old_names, new_names, count);
            break;
        case NASSIGN:
            SubstituteHygIdents(node->assign.target, old_names, new_names, count);
            SubstituteHygIdents(node->assign.value, old_names, new_names, count);
            break;
        case NCALL:
        case NMACROCALL:
        case NTEMPLATECALL:
            SubstituteHygIdents(node->call.callee, old_names, new_names, count);
            for (int i = 0; i < node->call.argc; i++) {
                SubstituteHygIdents(node->call.args[i], old_names, new_names, count);
            }
            break;
        case NINDEX:
            SubstituteHygIdents(node->index.arr, old_names, new_names, count);
            SubstituteHygIdents(node->index.idx, old_names, new_names, count);
            break;
        case NMEMBER:
            SubstituteHygIdents(node->member.obj, old_names, new_names, count);
            break;
        case NARRLIT:
            for (int i = 0; i < node->arrlit.count; i++) {
                SubstituteHygIdents(node->arrlit.elems[i], old_names, new_names, count);
            }
            break;
        case NTUPLE:
            for (int i = 0; i < node->tuple.count; i++) {
                SubstituteHygIdents(node->tuple.elems[i], old_names, new_names, count);
            }
            break;
        case NMAPLIT:
            for (int i = 0; i < node->maplit.count; i++) {
                SubstituteHygIdents(node->maplit.vals[i], old_names, new_names, count);
            }
            break;
        case NINC: case NDEC:
            SubstituteHygIdents(node->incdec.expr, old_names, new_names, count);
            break;
        case NAWAIT:
            SubstituteHygIdents(node->awaitn.expr, old_names, new_names, count);
            break;
        case NSPAWN:
            SubstituteHygIdents(node->spawnn.expr, old_names, new_names, count);
            break;
        case NSELECT:
            for (int i = 0; i < node->selectn.count; i++) {
                SubstituteHygIdents(node->selectn.bodies[i], old_names, new_names, count);
            }
            if (node->selectn.defaultbody) SubstituteHygIdents(node->selectn.defaultbody, old_names, new_names, count);
            break;
        case NMATCH:
            SubstituteHygIdents(node->match.expr, old_names, new_names, count);
            for (int i = 0; i < node->match.count; i++) {
                SubstituteHygIdents(node->match.bodies[i], old_names, new_names, count);
            }
            break;
        default:
            break;
    }
}

Node *ASTMacroExpand(Node *body, char **pnames, Node **args, int pc) {
  Node *expanded = NodeCopy(body);
  
  int hyg_cap = 8, hyg_count = 0;
  char **old_names = malloc(hyg_cap * sizeof(char*));
  char **new_names = malloc(hyg_cap * sizeof(char*));
  FindAndRenameLocals(expanded, pnames, pc, &old_names, &new_names, &hyg_count, &hyg_cap);
  if (hyg_count > 0) {
      SubstituteHygIdents(expanded, old_names, new_names, hyg_count);
      for (int i = 0; i < hyg_count; i++) {
          free(old_names[i]);
          free(new_names[i]);
      }
  }
  free(old_names);
  free(new_names);
  
  ASTSubstitute(&expanded, pnames, args, pc);
  return expanded;
}
