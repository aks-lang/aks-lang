// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef LEXH
#define LEXH

#include <stddef.h>

typedef enum {
    TEOF = 0,
    TERROR,
    TLET,
    TCONST,
    TFN,
    TRETURN,
    TIF,
    TELSE,
    TWHILE,
    TFOR,
    TPRINT,
    TINPUT,
    TTRUE,
    TFALSE,
    TNULL,
    TSTRUCT,
    TNEW,
    TIMPORT,
    TBREAK,
    TCONTINUE,
    TINT,
    TFLOAT,
    TSTR,
    TBOOL,
    TVOID,
    TIDENT,
    TINTLIT,
    TFLOATLIT,
    TSTRLIT,
    TRAWSTRLIT,
    TBOOLLIT,
    TPLUSPLUS,
    TMINUSMINUS,
    TPLUSEQ,
    TMINUSEQ,
    TEQEQ,
    TNEQ,
    TLTE,
    TGTE,
    TAND,
    TOR,
    TARROW,
    TPLUS,
    TMINUS,
    TSTAR,
    TSLASH,
    TPERCENT,
    TEQ,
    TLT,
    TGT,
    TNOT,
    TDOT,
    TLPAREN,
    TRPAREN,
    TLBRACE,
    TRBRACE,
    TLBRACKET,
    TRBRACKET,
    TSEMI,
    TCOLON,
    TCOMMA,
    TTRY,
    TCATCH,
    TTHROW,
    TQUESTION,
    TQUESTIONQUESTION,
    TQUESTIONDOT,
    TDOTDOT,
    TIN,
    TAS,
    TSWITCH,
    TCASE,
    TDEFAULT,
    TENUM,
    TYIELD,
    TDOTDOTDOT,
    TSHL,
    TSHR,
    TSTAREQ,
    TSLASHEQ,
    TPERCENTEQ,
    TFATARROW,
    TAMPERSAND,
    TPIPE,
    TCARET,
    TTILDE,
    TASYNC,
    TAWAIT,
    TSPAWN,
    TMATCH,
    TIMPLEMENTS,
    TIMPL,
    TINTERFACE,
    TWHERE,
    TPIPEOP,
    TTYPE,
    TLIFETIME,
    TMUT,
    TAT,       /* @  -  decorator prefix */
    TSELECT,
    TREQUIRES,
    TENSURES,
    TINVARIANT,
    TCOLONCOLON,   /* ::  -  namespace path separator */
    TMODULE,       /* module keyword */
    TUSE,          /* use keyword */
    TPUB,          /* pub visibility modifier */
    TPRIVATE,      /* private visibility modifier */
    TINTERNAL,     /* internal visibility modifier */
    TMOVE,         /* move keyword */
    TASM,          /* asm keyword */
    TCOMPTIME,     /* comptime keyword */
    TNURSERY,      /* nursery keyword */
    TREGION,       /* region keyword */
    TACTOR,        /* actor keyword */
    TEFFECT,       /* effect keyword */
    TPERFORM,      /* perform keyword */
    THANDLE,       /* handle keyword */
    TRESUME,       /* resume keyword */
    TGPU,          /* gpu keyword */
    TATOMIC,       /* atomic keyword */
    TDEFER         /* defer keyword */
} TType;

typedef struct {
    TType type;
    char *val;
    int line;
    int col;
} Token;

typedef struct {
    const char *src;
    size_t pos;
    int line;
    int col;
    Token *queued;
    int queued_head;
    int queued_count;
    int queued_capacity;
} Lexer;

Lexer LexNew(const char *src);
Token LexNext(Lexer *l);
void TokFree(Token *t);
const char *ToktypeName(TType t);

#endif // Lexh
