// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

Lexer LexNew(const char *src) {
    Lexer l = { src, 0, 1, 1, NULL, 0, 0, 0 };
    return l;
}

static char Lc(Lexer *l)  { return l->src[l->pos]; }
static char Ln(Lexer *l)  { return l->src[l->pos] ? l->src[l->pos+1] : 0; }
static char Adv(Lexer *l) {
    if (!l->src[l->pos]) return 0;
    char c=l->src[l->pos++];
    if(c=='\n'){l->line++;l->col=1;} else l->col++;
    return c;
}

static void Skip(Lexer *l) {
    for(;;) {
        while(isspace((unsigned char)Lc(l))) Adv(l);
        if(Lc(l)=='/' && Ln(l)=='/') { while(Lc(l) && Lc(l)!='\n') Adv(l); continue; }
        if(Lc(l)=='/' && Ln(l)=='*') {
            Adv(l); Adv(l);
            while(Lc(l)) { if(Lc(l)=='*' && Ln(l)=='/'){Adv(l);Adv(l);break;} Adv(l); }
            continue;
        }
        break;
    }
}

static Token MkTok(TType t, const char *v, int line, int col) {
    Token tok; tok.type=t; tok.val=strdup(v?v:""); tok.line=line; tok.col=col;
    return tok;
}

static TType KeyWord(const char *s) {
    size_t len = strlen(s);
    switch (len) {
        case 2:
            if (s[0] == 'f' && s[1] == 'n') return TFN;
            if (s[0] == 'i' && s[1] == 'f') return TIF;
            if (s[0] == 'i' && s[1] == 'n') return TIN;
            if (s[0] == 'a' && s[1] == 's') return TAS;
            break;
        case 3:
            if (!strcmp(s, "let"))   return TLET;
            if (!strcmp(s, "new"))   return TNEW;
            if (!strcmp(s, "try"))   return TTRY;
            if (!strcmp(s, "int"))   return TINT;
            if (!strcmp(s, "mut"))   return TMUT;
            if (!strcmp(s, "pub"))   return TPUB;
            if (!strcmp(s, "asm"))   return TASM;
            if (!strcmp(s, "for"))   return TFOR;
            if (!strcmp(s, "def"))   return TFN;
            if (!strcmp(s, "use"))   return TUSE;
            if (!strcmp(s, "gpu"))   return TGPU;
            break;
        case 4:
            if (!strcmp(s, "else"))  return TELSE;
            if (!strcmp(s, "true"))  return TTRUE;
            if (!strcmp(s, "null"))  return TNULL;
            if (!strcmp(s, "case"))  return TCASE;
            if (!strcmp(s, "enum"))  return TENUM;
            if (!strcmp(s, "bool"))  return TBOOL;
            if (!strcmp(s, "void"))  return TVOID;
            if (!strcmp(s, "move"))  return TMOVE;
            if (!strcmp(s, "type"))  return TTYPE;
            if (!strcmp(s, "impl"))  return TIMPL;
            break;
        case 5:
            if (!strcmp(s, "const")) return TCONST;
            if (!strcmp(s, "while")) return TWHILE;
            if (!strcmp(s, "print")) return TPRINT;
            if (!strcmp(s, "input")) return TINPUT;
            if (!strcmp(s, "false")) return TFALSE;
            if (!strcmp(s, "break")) return TBREAK;
            if (!strcmp(s, "catch")) return TCATCH;
            if (!strcmp(s, "throw")) return TTHROW;
            if (!strcmp(s, "yield")) return TYIELD;
            if (!strcmp(s, "float")) return TFLOAT;
            if (!strcmp(s, "async")) return TASYNC;
            if (!strcmp(s, "await")) return TAWAIT;
            if (!strcmp(s, "spawn")) return TSPAWN;
            if (!strcmp(s, "match")) return TMATCH;
            if (!strcmp(s, "where")) return TWHERE;
            if (!strcmp(s, "actor")) return TACTOR;
            if (!strcmp(s, "defer")) return TDEFER;
            break;
        case 6:
            if (!strcmp(s, "return")) return TRETURN;
            if (!strcmp(s, "struct")) return TSTRUCT;
            if (!strcmp(s, "import")) return TIMPORT;
            if (!strcmp(s, "switch")) return TSWITCH;
            if (!strcmp(s, "string")) return TSTR;
            if (!strcmp(s, "select")) return TSELECT;
            if (!strcmp(s, "module")) return TMODULE;
            if (!strcmp(s, "region")) return TREGION;
            if (!strcmp(s, "effect")) return TEFFECT;
            if (!strcmp(s, "handle")) return THANDLE;
            if (!strcmp(s, "resume")) return TRESUME;
            if (!strcmp(s, "atomic")) return TATOMIC;
            break;
        case 7:
            if (!strcmp(s, "default")) return TDEFAULT;
            if (!strcmp(s, "ensures")) return TENSURES;
            if (!strcmp(s, "private")) return TPRIVATE;
            if (!strcmp(s, "println")) return TPRINT;
            if (!strcmp(s, "typedef")) return TTYPE;
            if (!strcmp(s, "__asm__")) return TASM;
            if (!strcmp(s, "nursery")) return TNURSERY;
            if (!strcmp(s, "perform")) return TPERFORM;
            break;
        case 8:
            if (!strcmp(s, "continue")) return TCONTINUE;
            if (!strcmp(s, "requires")) return TREQUIRES;
            if (!strcmp(s, "internal")) return TINTERNAL;
            if (!strcmp(s, "comptime")) return TCOMPTIME;
            if (!strcmp(s, "errdefer")) return TERRDEFER;
            break;
        case 9:
            if (!strcmp(s, "interface")) return TINTERFACE;
            if (!strcmp(s, "invariant")) return TINVARIANT;
            break;
        case 10:
            if (!strcmp(s, "implements")) return TIMPLEMENTS;
            break;
    }
    return TIDENT;
}

static void QTok(Lexer *l, Token t) {
    if (l->queued_count == 0) {
        l->queued_head = 0;
    }
    if (l->queued_head + l->queued_count >= l->queued_capacity) {
        if (l->queued_head > 0) {
            memmove(l->queued, l->queued + l->queued_head, l->queued_count * sizeof(Token));
            l->queued_head = 0;
        }
        if (l->queued_count >= l->queued_capacity) {
            l->queued_capacity = l->queued_capacity == 0 ? 8 : l->queued_capacity * 2;
            l->queued = realloc(l->queued, l->queued_capacity * sizeof(Token));
        }
    }
    l->queued[l->queued_head + l->queued_count++] = t;
}

static inline Token QPop(Lexer *l) {
    Token t = l->queued[l->queued_head++];
    l->queued_count--;
    if (l->queued_count == 0) {
        l->queued_head = 0;
    }
    return t;
}

Token LexNext(Lexer *l) {
    if (l->queued_count > 0) {
        return QPop(l);
    }
    Skip(l);
    int line=l->line, col=l->col;
    char c=Lc(l);
    if(!c) return MkTok(TEOF,"",line,col);

    // lifetimes
    if(c=='\'') {
        size_t startpos=l->pos;
        while(isalnum((unsigned char)Lc(l))||Lc(l)=='_') Adv(l);
        size_t len=l->pos-startpos;
        char *buf=malloc(len+2);
        buf[0]='\'';
        memcpy(buf+1,l->src+startpos,len);
        buf[len+1]=0;
        Token t=MkTok(TLIFETIME,buf,line,col);
        free(buf);
        return t;
    }

    // triple-quote strings
    if(c=='"' && l->src[l->pos+1]=='"' && l->src[l->pos+2]=='"') {
        Adv(l); Adv(l); Adv(l);
        size_t start=l->pos;
        while(Lc(l)) {
            if(Lc(l)=='"' && l->src[l->pos+1]=='"' && l->src[l->pos+2]=='"') break;
            Adv(l);
        }
        size_t len=l->pos-start;
        char *buf=malloc(len+1); memcpy(buf,l->src+start,len); buf[len]=0;
        if(Lc(l)=='"') { Adv(l); Adv(l); Adv(l); }
        Token t=MkTok(TSTRLIT,buf,line,col); free(buf);
        return t;
    }

    // regular strings
    if(c=='"') {
        Adv(l);
        size_t start=l->pos;
        while(Lc(l) && Lc(l)!='"') {
            if(Lc(l)=='\\') { Adv(l); if (!Lc(l)) break; }
            Adv(l);
        }
        size_t len=l->pos-start;
        char *buf=malloc(len+1); memcpy(buf,l->src+start,len); buf[len]=0;
        if(Lc(l)=='"') Adv(l);
        Token t=MkTok(TSTRLIT,buf,line,col); free(buf);
        return t;
    }

    // numbers
    if(isdigit((unsigned char)c)) {
        if (c == '0' && (Ln(l) == 'x' || Ln(l) == 'X')) {
            Adv(l); Adv(l); // consume '0x'
            size_t start = l->pos;
            while (isxdigit((unsigned char)Lc(l)) || Lc(l) == '_') Adv(l);
            size_t len = l->pos - start;
            char *buf = malloc(len + 1);
            size_t bi = 0;
            for (size_t i = 0; i < len; i++) {
                char ch = l->src[start + i];
                if (ch != '_') buf[bi++] = ch;
            }
            buf[bi] = 0;
            unsigned long long val = strtoull(buf, NULL, 16);
            free(buf);
            char dec[64];
            snprintf(dec, sizeof(dec), "%llu", val);
            return MkTok(TINTLIT, dec, line, col);
        }
        if (c == '0' && (Ln(l) == 'b' || Ln(l) == 'B')) {
            Adv(l); Adv(l); // consume '0b'
            size_t start = l->pos;
            while (Lc(l) == '0' || Lc(l) == '1' || Lc(l) == '_') Adv(l);
            size_t len = l->pos - start;
            char *buf = malloc(len + 1);
            size_t bi = 0;
            for (size_t i = 0; i < len; i++) {
                char ch = l->src[start + i];
                if (ch != '_') buf[bi++] = ch;
            }
            buf[bi] = 0;
            unsigned long long val = strtoull(buf, NULL, 2);
            free(buf);
            char dec[64];
            snprintf(dec, sizeof(dec), "%llu", val);
            return MkTok(TINTLIT, dec, line, col);
        }
        
        size_t start = l->pos; int isfloat = 0;
        while (isdigit((unsigned char)Lc(l)) || Lc(l) == '_') Adv(l);
        if (Lc(l) == '.' && isdigit((unsigned char)Ln(l))) {
            isfloat = 1;
            Adv(l);
            while (isdigit((unsigned char)Lc(l)) || Lc(l) == '_') Adv(l);
        }
        size_t len = l->pos - start;
        char *buf = malloc(len + 1);
        size_t bi = 0;
        for (size_t i = 0; i < len; i++) {
            char ch = l->src[start + i];
            if (ch != '_') buf[bi++] = ch;
        }
        buf[bi] = 0;
        Token t = MkTok(isfloat ? TFLOATLIT : TINTLIT, buf, line, col);
        free(buf);
        return t;
    }

    // raw strings (e.g. r"pattern", r'pattern', r"""pattern""", or r'''pattern''')
    if ((c == 'r' || c == 'R') && (Ln(l) == '"' || Ln(l) == '\'')) {
        char q = Ln(l);
        int is_triple = (l->src[l->pos+2] == q && l->src[l->pos+3] == q);
        Adv(l); // consume 'r'
        if (is_triple) {
            Adv(l); Adv(l); Adv(l); // consume 3 quotes
        } else {
            Adv(l); // consume quote
        }
        size_t start = l->pos;
        while (Lc(l)) {
            if (is_triple) {
                if (Lc(l) == q && l->src[l->pos+1] == q && l->src[l->pos+2] == q) break;
            } else {
                if (Lc(l) == q) break;
            }
            Adv(l);
        }
        size_t len = l->pos - start;
        size_t bslash_count = 0;
        for (size_t i = 0; i < len; i++) {
            if (l->src[start + i] == '\\') bslash_count++;
        }
        char *buf = malloc(len + bslash_count + 1);
        size_t bi = 0;
        for (size_t i = 0; i < len; i++) {
            char ch = l->src[start + i];
            if (ch == '\\') {
                buf[bi++] = '\\';
                buf[bi++] = '\\';
            } else {
                buf[bi++] = ch;
            }
        }
        buf[bi] = '\0';
        if (is_triple) {
            if (Lc(l) == q) { Adv(l); Adv(l); Adv(l); }
        } else {
            if (Lc(l) == q) Adv(l);
        }
        Token t = MkTok(TRAWSTRLIT, buf, line, col);
        free(buf);
        return t;
    }

    // interpolated strings
    if (c == 'f' && Ln(l) == '"') {
        int is_triple = (l->src[l->pos+2] == '"' && l->src[l->pos+3] == '"');
        Adv(l); // consume 'f'
        if (is_triple) {
            Adv(l); Adv(l); Adv(l); // consume '"""'
        } else {
            Adv(l); // consume '"'
        }
        
        size_t start = l->pos;
        while (Lc(l)) {
            if (is_triple) {
                if (Lc(l) == '"' && l->src[l->pos+1] == '"' && l->src[l->pos+2] == '"') break;
            } else {
                if (Lc(l) == '"') break;
            }
            if (Lc(l) == '\\') {
                Adv(l);
                if (!Lc(l)) break;
            }
            Adv(l);
        }
        size_t len = l->pos - start;
        char *str_body = malloc(len + 1);
        memcpy(str_body, l->src + start, len);
        str_body[len] = '\0';
        
        if (is_triple) {
            if (Lc(l) == '"') { Adv(l); Adv(l); Adv(l); }
        } else {
            if (Lc(l) == '"') Adv(l);
        }
        
        size_t i = 0;
        int first = 1;
        while (str_body[i]) {
            if (str_body[i] == '{') {
                i++; // skip '{'
                size_t expr_start = i;
                int bcount = 1;
                int instring = 0;
                char strquote = 0;
                while (str_body[i]) {
                    char ch = str_body[i];
                    if (instring) {
                        if (ch == '\\' && str_body[i+1]) i++;
                        else if (ch == strquote) instring = 0;
                    } else {
                        if (ch == '"' || ch == '\'') {
                            instring = 1;
                            strquote = ch;
                        } else if (ch == '{') {
                            bcount++;
                        } else if (ch == '}') {
                            bcount--;
                            if (bcount == 0) break;
                        }
                    }
                    i++;
                }
                size_t expr_len = i - expr_start;
                char *expr_str = malloc(expr_len + 1);
                memcpy(expr_str, str_body + expr_start, expr_len);
                expr_str[expr_len] = '\0';
                
                if (str_body[i] == '}') i++; // skip '}'
                
                if (!first) {
                    QTok(l, MkTok(TPLUS, "+", line, col));
                }
                first = 0;
                
                QTok(l, MkTok(TIDENT, "String", line, col));
                QTok(l, MkTok(TLPAREN, "(", line, col));
                
                Lexer sub = LexNew(expr_str);
                while (1) {
                    Token subtok = LexNext(&sub);
                    if (subtok.type == TEOF) {
                        TokFree(&subtok);
                        break;
                    }
                    QTok(l, subtok);
                }
                free(sub.queued);
                QTok(l, MkTok(TRPAREN, ")", line, col));
                free(expr_str);
            } else {
                size_t lit_start = i;
                while (str_body[i] && str_body[i] != '{') {
                    if (str_body[i] == '\\' && str_body[i+1]) i++;
                    i++;
                }
                size_t lit_len = i - lit_start;
                if (lit_len > 0) {
                    char *lit_str = malloc(lit_len + 1);
                    memcpy(lit_str, str_body + lit_start, lit_len);
                    lit_str[lit_len] = '\0';
                    
                    char *unescaped = malloc(lit_len + 1);
                    size_t u = 0, k = 0;
                    while (k < lit_len) {
                        if (lit_str[k] == '\\' && k + 1 < lit_len) {
                            k++;
                            if (lit_str[k] == 'n') unescaped[u++] = '\n';
                            else if (lit_str[k] == 't') unescaped[u++] = '\t';
                            else if (lit_str[k] == 'r') unescaped[u++] = '\r';
                            else unescaped[u++] = lit_str[k];
                        } else {
                            unescaped[u++] = lit_str[k];
                        }
                        k++;
                    }
                    unescaped[u] = '\0';
                    free(lit_str);
                    
                    if (!first) {
                        QTok(l, MkTok(TPLUS, "+", line, col));
                    }
                    first = 0;
                    QTok(l, MkTok(TSTRLIT, unescaped, line, col));
                    free(unescaped);
                }
            }
        }
        
        if (first) {
            free(str_body);
            return MkTok(TSTRLIT, "", line, col);
        }
        free(str_body);
        
        if (l->queued_count > 0) {
            return QPop(l);
        }
        return MkTok(TEOF, "", line, col);
    }

    // identifiers / keywords
    if(isalpha((unsigned char)c)||c=='_'||c=='$') {
        size_t start=l->pos;
        while(isalnum((unsigned char)Lc(l))||Lc(l)=='_'||Lc(l)=='$') Adv(l);
        size_t len=l->pos-start;
        char *buf=malloc(len+1); memcpy(buf,l->src+start,len); buf[len]=0;
        TType kw=KeyWord(buf);
        Token t=MkTok(kw,buf,line,col); free(buf);
        return t;
    }

    Adv(l);
    char n=Lc(l);

    // three-char tokens
    if(c=='.' && n=='.' && l->src[l->pos+1]=='.') { Adv(l); Adv(l); return MkTok(TDOTDOTDOT,"...",line,col); }
    if(c=='<' && n=='<') { Adv(l); return MkTok(TSHL,"<<",line,col); }
    if(c=='>' && n=='>') { Adv(l); return MkTok(TSHR,">>",line,col); }

    // two-char tokens
    if(c=='+' && n=='+'){Adv(l);return MkTok(TPLUSPLUS,"++",line,col);}
    if(c=='-' && n=='-'){Adv(l);return MkTok(TMINUSMINUS,"--",line,col);}
    if(c=='+' && n=='='){Adv(l);return MkTok(TPLUSEQ,"+=",line,col);}
    if(c=='-' && n=='='){Adv(l);return MkTok(TMINUSEQ,"-=",line,col);}
    if(c=='*' && n=='='){Adv(l);return MkTok(TSTAREQ,"*=",line,col);}
    if(c=='/' && n=='='){Adv(l);return MkTok(TSLASHEQ,"/=",line,col);}
    if(c=='%' && n=='='){Adv(l);return MkTok(TPERCENTEQ,"%=",line,col);}
    if(c=='=' && n=='='){Adv(l);return MkTok(TEQEQ,"==",line,col);}
    if(c=='!' && n=='='){Adv(l);return MkTok(TNEQ,"!=",line,col);}
    if(c=='<' && n=='='){Adv(l);return MkTok(TLTE,"<=",line,col);}
    if(c=='>' && n=='='){Adv(l);return MkTok(TGTE,">=",line,col);}
    if(c=='&' && n=='&'){Adv(l);return MkTok(TAND,"&&",line,col);}
    if(c=='|' && n=='|'){Adv(l);return MkTok(TOR,"||",line,col);}
    if(c=='|' && n=='>'){Adv(l);return MkTok(TPIPEOP,"|>",line,col);}
    if(c=='-' && n=='>'){Adv(l);return MkTok(TARROW,"->",line,col);}
    if(c=='=' && n=='>'){Adv(l);return MkTok(TFATARROW,"=>",line,col);}
    if(c=='?' && n=='?'){Adv(l);return MkTok(TQUESTIONQUESTION,"??",line,col);}
    if(c=='?' && n=='.'){Adv(l);return MkTok(TQUESTIONDOT,"?.",line,col);}
    if(c=='.' && n=='.'){Adv(l);return MkTok(TDOTDOT,"..",line,col);}
    if(c==':' && n==':'){Adv(l);return MkTok(TCOLONCOLON,"::",line,col);}

    char buf[2]={c,0};
    switch(c) {
        case '+': return MkTok(TPLUS,buf,line,col);
        case '-': return MkTok(TMINUS,buf,line,col);
        case '*': return MkTok(TSTAR,buf,line,col);
        case '/': return MkTok(TSLASH,buf,line,col);
        case '%': return MkTok(TPERCENT,buf,line,col);
        case '=': return MkTok(TEQ,buf,line,col);
        case '<': return MkTok(TLT,buf,line,col);
        case '>': return MkTok(TGT,buf,line,col);
        case '!': return MkTok(TNOT,buf,line,col);
        case '.': return MkTok(TDOT,buf,line,col);
        case '?': return MkTok(TQUESTION,buf,line,col);
        case '(': return MkTok(TLPAREN,buf,line,col);
        case ')': return MkTok(TRPAREN,buf,line,col);
        case '{': return MkTok(TLBRACE,buf,line,col);
        case '}': return MkTok(TRBRACE,buf,line,col);
        case '[': return MkTok(TLBRACKET,buf,line,col);
        case ']': return MkTok(TRBRACKET,buf,line,col);
        case ';': return MkTok(TSEMI,buf,line,col);
        case ':': return MkTok(TCOLON,buf,line,col);
        case ',': return MkTok(TCOMMA,buf,line,col);
        case '&': return MkTok(TAMPERSAND,buf,line,col);
        case '|': return MkTok(TPIPE,buf,line,col);
        case '^': return MkTok(TCARET,buf,line,col);
        case '~': return MkTok(TTILDE,buf,line,col);
        case '@': return MkTok(TAT,buf,line,col);
    }
    return MkTok(TERROR,buf,line,col);
}

void TokFree(Token *t) {
    if (t && t->val) {
        free(t->val);
        t->val = NULL;
    }
}

const char *ToktypeName(TType t) {
    switch(t) {
        case TINTLIT: return "INT"; case TFLOATLIT: return "FLOAT";
        case TSTRLIT: return "STRING"; case TRAWSTRLIT: return "RAWSTRING"; case TIDENT: return "IDENT";
        case TEOF: return "EOF"; case TLET: return "let";
        case TCONST: return "const"; case TFN: return "fn";
        case TRETURN: return "return"; case TIF: return "if";
        case TELSE: return "else"; case TWHILE: return "while";
        case TFOR: return "for"; case TIN: return "in";
        case TSWITCH: return "switch"; case TCASE: return "case";
        case TDEFAULT: return "default"; case TENUM: return "enum";
        case TBREAK: return "break"; case TCONTINUE: return "continue";
        case TTRY: return "try"; case TCATCH: return "catch";
        case TTHROW: return "throw"; case TYIELD: return "yield";
        case TIMPORT: return "import"; case TAS: return "as";
        case TASYNC: return "async"; case TAWAIT: return "await";
        case TSPAWN: return "spawn";
        case TMATCH: return "match";
        case TIMPLEMENTS: return "implements";
        case TIMPL: return "impl";
        case TINTERFACE: return "interface";
        case TWHERE: return "where";
        case TPIPEOP: return "|>";
        case TTYPE: return "type";
        case TLIFETIME: return "lifetime";
        case TMUT: return "mut";
        case TAT: return "@";
        case TSELECT: return "select";
        case TREQUIRES: return "requires";
        case TENSURES: return "ensures";
        case TINVARIANT: return "invariant";
        case TCOLONCOLON: return "::";
        case TMODULE: return "module";
        case TUSE: return "use";
        case TPUB: return "pub";
        case TPRIVATE: return "private";
        case TINTERNAL: return "internal";
        case TMOVE: return "move";
        case TASM: return "asm";
        case TCOMPTIME: return "comptime";
        case TNURSERY: return "nursery";
        case TREGION: return "region";
        case TACTOR: return "actor";
        case TEFFECT: return "effect";
        case TPERFORM: return "perform";
        case THANDLE: return "handle";
        case TRESUME: return "resume";
        case TGPU: return "gpu";
        case TATOMIC: return "atomic";
        case TDEFER: return "defer";
        case TERRDEFER: return "errdefer";
        default: return "?";
    }
}