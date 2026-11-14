// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "pkg.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#include <winhttp.h>
#define MkDirNative(path) _mkdir(path)
#define AccessNative(path, mode) _access(path, mode)
#define PATHSEP '\\'
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#define MkDirNative(path) mkdir(path, 0777)
#define AccessNative(path, mode) access(path, mode)
#define PATHSEP '/'
#ifndef LPCWSTR
typedef const wchar_t *LPCWSTR;
#endif
#ifndef DWORD
typedef unsigned long DWORD;
#endif
#endif

#ifndef BCRYPT_ECDSA_P256_ALGORITHM
#define BCRYPT_ECDSA_P256_ALGORITHM L"ECDSA_P256"
#endif

static char *ReadFileContent(const char *path);

int ECCGenerateKeys(LPCWSTR alg_id, LPCWSTR curve_name, DWORD magic_priv, DWORD magic_pub, size_t key_bits, char **pub_pem, char **priv_pem);
int ECCSignData(LPCWSTR alg_id, LPCWSTR curve_name, const char *priv_pem, const uint8_t *hash, size_t hash_len, uint8_t *sig, size_t *sig_len);
int ECCVerifyData(LPCWSTR alg_id, LPCWSTR curve_name, const char *pub_pem, const uint8_t *hash, size_t hash_len, const uint8_t *sig, size_t sig_len);

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} PKGSHA256CTX;

#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static void PkgSha256Transform(PKGSHA256CTX *ctx, const uint8_t *data) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    }
    for (; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void PkgSha256Init(PKGSHA256CTX *ctx) {
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85; ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c; ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void PkgSha256Update(PKGSHA256CTX *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i]; ctx->datalen++;
        if (ctx->datalen == 64) {
            PkgSha256Transform(ctx, ctx->data);
            ctx->bitlen += 512; ctx->datalen = 0;
        }
    }
}

static void PkgSha256Final(PKGSHA256CTX *ctx, uint8_t *hash) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        PkgSha256Transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    PkgSha256Transform(ctx, ctx->data);
    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

static int Sha256File(const char *path, char *out_hex) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    PKGSHA256CTX ctx;
    PkgSha256Init(&ctx);
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        PkgSha256Update(&ctx, buf, n);
    }
    fclose(f);
    uint8_t hash[32];
    PkgSha256Final(&ctx, hash);
    for (int i = 0; i < 32; i++) {
        sprintf(out_hex + i * 2, "%02x", hash[i]);
    }
    out_hex[64] = '\0';
    return 1;
}

typedef struct {
    char name[128];
    char version[64];
    char hash[65];
} LockDep;

static LockDep glockdeps[128];
static int glockdepscount = 0;

static void ParseLockfile(void) {
    glockdepscount = 0;
    char *json = ReadFileContent("aks.lock");
    if (!json) return;
    
    const char *pos = strstr(json, "\"resolved\"");
    if (pos) {
        const char *brace = strchr(pos, '{');
        if (brace) {
            brace++;
            const char *p = brace;
            while (*p && *p != '}') {
                if (*p == '"') {
                    p++;
                    char depname[128] = "";
                    int idx = 0;
                    while (*p && *p != '"') depname[idx++] = *p++;
                    depname[idx] = '\0';
                    if (*p == '"') p++;
                    
                    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                    if (*p == '{') {
                        p++;
                        char ver[64] = "";
                        char hash[65] = "";
                        while (*p && *p != '}') {
                            if (strncmp(p, "\"version\"", 9) == 0) {
                                p += 9;
                                while (*p == ' ' || *p == ':' || *p == '\t' || *p == '"') p++;
                                int vidx = 0;
                                while (*p && *p != '"') ver[vidx++] = *p++;
                                ver[vidx] = '\0';
                                if (*p == '"') p++;
                            } else if (strncmp(p, "\"hash\"", 6) == 0) {
                                p += 6;
                                while (*p == ' ' || *p == ':' || *p == '\t' || *p == '"') p++;
                                int hidx = 0;
                                while (*p && *p != '"') hash[hidx++] = *p++;
                                hash[hidx] = '\0';
                                if (*p == '"') p++;
                            } else {
                                p++;
                            }
                        }
                        if (glockdepscount < 128) {
                            strcpy(glockdeps[glockdepscount].name, depname);
                            strcpy(glockdeps[glockdepscount].version, ver);
                            strcpy(glockdeps[glockdepscount].hash, hash);
                            glockdepscount++;
                        }
                    } else if (*p == '"') {
                        p++;
                        char ver[64] = "";
                        int vidx = 0;
                        while (*p && *p != '"') ver[vidx++] = *p++;
                        ver[vidx] = '\0';
                        if (*p == '"') p++;
                        if (glockdepscount < 128) {
                            strcpy(glockdeps[glockdepscount].name, depname);
                            strcpy(glockdeps[glockdepscount].version, ver);
                            strcpy(glockdeps[glockdepscount].hash, "");
                            glockdepscount++;
                        }
                    }
                } else {
                    p++;
                }
            }
        }
    }
    free(json);
}

// Dynamically resolved registry directory (~/.aksregistry)
static char g_registrydir[512] = "";

static const char *GetRegIsTryDir(void) {
    if (g_registrydir[0] != '\0') return g_registrydir;
    const char *home = getenv("USERPROFILE"); // Windows
    if (!home) home = getenv("HOME");          // POSIX
    if (!home) home = ".";
    snprintf(g_registrydir, sizeof(g_registrydir), "%s/.aksregistry", home);
    return g_registrydir;
}

static const char *GetCredentialsPath(void) {
    static char credpath[512] = "";
    if (credpath[0] != '\0') return credpath;
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (!home) home = ".";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.aks", home);
    snprintf(credpath, sizeof(credpath), "%s/.aks/credentials", home);
    return credpath;
}

char *PkgGetStoredToken(const char *registry_url) {
    const char *env_tok = getenv("AKS_AUTH_TOKEN");
    if (env_tok && env_tok[0]) return strdup(env_tok);
    const char *cpath = GetCredentialsPath();
    FILE *f = fopen(cpath, "r");
    if (!f) return NULL;
    char line[512];
    const char *target = registry_url ? registry_url : "default";
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *reg = line;
            char *tok = eq + 1;
            while (*reg == ' ' || *reg == '\t') reg++;
            while (*tok == ' ' || *tok == '\t') tok++;
            size_t l = strlen(tok);
            while (l > 0 && (tok[l - 1] == '\r' || tok[l - 1] == '\n' || tok[l - 1] == ' ')) tok[--l] = '\0';
            if (strcmp(reg, target) == 0 || strcmp(reg, "token") == 0 || strcmp(reg, "default") == 0) {
                fclose(f);
                return strdup(tok);
            }
        }
    }
    fclose(f);
    return NULL;
}

int PkgLogin(const char *token, const char *registry_url) {
    if (!token || !token[0]) {
        fprintf(stderr, "Error: Token cannot be empty.\n");
        return 1;
    }
    const char *target = (registry_url && registry_url[0]) ? registry_url : "default";
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (!home) home = ".";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.aks", home);
#ifdef _WIN32
    CreateDirectoryA(dir, NULL);
#else
    mkdir(dir, 0755);
#endif
    const char *cpath = GetCredentialsPath();
    FILE *f = fopen(cpath, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not open credentials file '%s' for writing.\n", cpath);
        return 1;
    }
    fprintf(f, "# AKS Package Registry Authentication Credentials\n");
    fprintf(f, "%s=%s\n", target, token);
    fclose(f);
    printf("Successfully authenticated with AKS package registry ('%s').\nToken saved to '%s'.\n", target, cpath);
    return 0;
}

int PkgLogout(const char *registry_url) {
    (void)registry_url;
    const char *cpath = GetCredentialsPath();
    remove(cpath);
    printf("Logged out from AKS package registry. Credentials cleared from '%s'.\n", cpath);
    return 0;
}

const char *gcompiletarget = "host";

// helper to create directory recursively
static int MkDirp(const char *path) {
    char temp[512];
    snprintf(temp, sizeof(temp), "%s", path);
    int len = (int)strlen(temp);
    if (len == 0) return 0;
    
    // trim trailing separators
    if (temp[len - 1] == '/' || temp[len - 1] == '\\') {
        temp[len - 1] = '\0';
    }
    
    for (char *p = temp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char val = *p;
            *p = '\0';
            MkDirNative(temp);
            *p = val;
        }
    }
    return MkDirNative(temp) == 0 || errno == EEXIST;
}

// helper to copy file
int PkgCopyFile(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    
    // ensure parent directory exists
    char dstdir[512];
    snprintf(dstdir, sizeof(dstdir), "%s", dst);
    char *lastsep = strrchr(dstdir, '/');
    if (!lastsep) lastsep = strrchr(dstdir, '\\');
    if (lastsep) {
        *lastsep = '\0';
        MkDirp(dstdir);
    }
    
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    
    char buf[4096];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, bytes, out);
    }
    
    fclose(in);
    fclose(out);
    return 1;
}

static int PkgCopyDirRecursive(const char *src, const char *dst) {
    MkDirp(dst);
#ifdef _WIN32
    char pat[512];
    snprintf(pat, sizeof(pat), "%s\\*", src);
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pat, &ffd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        const char *name = ffd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (strcmp(name, ".git") == 0 || strcmp(name, ".akspackages") == 0) continue;
        char ssub[512], dsub[512];
        snprintf(ssub, sizeof(ssub), "%s/%s", src, name);
        snprintf(dsub, sizeof(dsub), "%s/%s", dst, name);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            PkgCopyDirRecursive(ssub, dsub);
        } else {
            PkgCopyFile(ssub, dsub);
        }
    } while (FindNextFileA(h, &ffd));
    FindClose(h);
    return 1;
#else
    DIR *d = opendir(src);
    if (!d) return 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (strcmp(name, ".git") == 0 || strcmp(name, ".akspackages") == 0) continue;
        char ssub[512], dsub[512];
        snprintf(ssub, sizeof(ssub), "%s/%s", src, name);
        snprintf(dsub, sizeof(dsub), "%s/%s", dst, name);
        struct stat st;
        if (stat(ssub, &st) == 0 && S_ISDIR(st.st_mode)) {
            PkgCopyDirRecursive(ssub, dsub);
        } else {
            PkgCopyFile(ssub, dsub);
        }
    }
    closedir(d);
    return 1;
#endif
}

// simple JSON key-value extraction helper (extracts string or nested objects)
static int JSONGetVal(const char *json, const char *key, char *outval, int maxlen) {
    if (!json || !key || !outval) return 0;
    outval[0] = '\0';
    
    char searchkey[128];
    snprintf(searchkey, sizeof(searchkey), "\"%s\"", key);
    const char *pos = strstr(json, searchkey);
    if (!pos) return 0;
    
    pos += strlen(searchkey);
    // skip colon and spaces
    while (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\r' || *pos == '\n') pos++;
    
    if (*pos == '"') {
        pos++; // skip opening quote
        int idx = 0;
        while (*pos && *pos != '"' && idx < maxlen - 1) {
            outval[idx++] = *pos++;
        }
        outval[idx] = '\0';
        return 1;
    } else if (*pos == '{') {
        // parse nested object brace by brace
        int braces = 0;
        int idx = 0;
        while (*pos && idx < maxlen - 1) {
            if (*pos == '{') braces++;
            else if (*pos == '}') braces--;
            outval[idx++] = *pos++;
            if (braces == 0) break;
        }
        outval[idx] = '\0';
        return 1;
    } else {
        // parse raw integer or boolean
        int idx = 0;
        while (*pos && *pos != ',' && *pos != '}' && *pos != ']' && *pos != '\r' && *pos != '\n' && *pos != ' ' && idx < maxlen - 1) {
            outval[idx++] = *pos++;
        }
        outval[idx] = '\0';
        return 1;
    }
}

// semVer parser & constraint solver
typedef struct {
    int major;
    int minor;
    int patch;
    int parts;
} SemVer;

static int ParseSemver(const char *str, SemVer *ver) {
    ver->major = ver->minor = ver->patch = 0;
    ver->parts = sscanf(str, "%d.%d.%d", &ver->major, &ver->minor, &ver->patch);
    return ver->parts >= 1;
}

static int CmpSemVer(SemVer a, SemVer b) {
    if (a.major != b.major) return (a.major > b.major) ? 1 : -1;
    if (a.minor != b.minor) return (a.minor > b.minor) ? 1 : -1;
    if (a.patch != b.patch) return (a.patch > b.patch) ? 1 : -1;
    return 0;
}

static int VersionMatchClause(SemVer val, const char *clause) {
    if (!clause || clause[0] == '\0' || strcmp(clause, "*") == 0) return 1;
    const char *p = clause;
    char op[8] = "";
    int opidx = 0;
    while (*p && (*p == '=' || *p == '!' || *p == '>' || *p == '<' || *p == '^' || *p == '~')) {
        if (opidx < 7) op[opidx++] = *p;
        p++;
    }
    op[opidx] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (strcmp(p, "*") == 0 || strcmp(p, "x") == 0 || strcmp(p, "X") == 0) return 1;

    SemVer target;
    if (!ParseSemver(p, &target)) return 0;
    int has_x = (strstr(p, ".x") != NULL || strstr(p, ".X") != NULL || strstr(p, ".*") != NULL);
    if (op[0] == '\0' && has_x) {
        if (target.parts == 1) return val.major == target.major;
        if (target.parts == 2) return val.major == target.major && val.minor == target.minor;
    }

    int cmp = CmpSemVer(val, target);
    if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 || strcmp(op, "") == 0) return cmp == 0;
    if (strcmp(op, "!=") == 0) return cmp != 0;
    if (strcmp(op, ">=") == 0) return cmp >= 0;
    if (strcmp(op, "<=") == 0) return cmp <= 0;
    if (strcmp(op, ">") == 0)  return cmp > 0;
    if (strcmp(op, "<") == 0)  return cmp < 0;
    if (strcmp(op, "^") == 0) {
        if (cmp < 0) return 0;
        if (target.major > 0) return val.major == target.major;
        if (target.minor > 0 || target.parts < 3) return val.major == 0 && val.minor == target.minor;
        return cmp == 0;
    }
    if (strcmp(op, "~") == 0) {
        if (cmp < 0 || val.major != target.major) return 0;
        if (target.parts >= 2) return val.minor == target.minor;
        return 1;
    }
    return 0;
}

// version match check (supports comma/space-separated compound SemVer clauses)
static int VersionMatch(SemVer val, const char *constraint) {
    if (!constraint || strlen(constraint) == 0 || strcmp(constraint, "*") == 0) return 1;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", constraint);
    char *p = buf;
    int any_clause = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        /* Keep operator glued to version token even if space separates ">= 1.0.0" */
        char clause[64] = "";
        int ci = 0;
        while (*p && (*p == '=' || *p == '!' || *p == '>' || *p == '<' || *p == '^' || *p == '~')) {
            if (ci < 62) clause[ci++] = *p;
            p++;
        }
        while (*p == ' ' || *p == '\t') p++;
        while (*p && *p != ' ' && *p != '\t' && *p != ',') {
            if (ci < 63) clause[ci++] = *p;
            p++;
        }
        clause[ci] = '\0';
        if (clause[0]) {
            any_clause = 1;
            if (!VersionMatchClause(val, clause)) return 0;
        }
    }
    return any_clause ? 1 : 1;
}

int PkgVersionSatisfies(const char *version, const char *constraint) {
    SemVer v;
    if (!version || !ParseSemver(version, &v)) return 0;
    return VersionMatch(v, constraint);
}

// file reading helper
static char *ReadFileContent(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t readbytes = fread(buf, 1, (size_t)sz, f);
    buf[readbytes] = '\0';
    fclose(f);
    return buf;
}

static void PkgSha256Hex(const uint8_t *data, size_t len, char *out_hex) {
    PKGSHA256CTX ctx;
    PkgSha256Init(&ctx);
    if (len > 0) PkgSha256Update(&ctx, data, len);
    uint8_t digest[32];
    PkgSha256Final(&ctx, digest);
    for (int i = 0; i < 32; i++) sprintf(out_hex + i * 2, "%02x", digest[i]);
    out_hex[64] = '\0';
}

static int PkgPackWriteEntry(FILE *out, PKGSHA256CTX *sha, const char *relpath, const char *filepath) {
    FILE *in = fopen(filepath, "rb");
    if (!in) return 0;
    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    rewind(in);
    if (sz < 0) { fclose(in); return 0; }
    uint8_t *buf = sz > 0 ? (uint8_t *)malloc((size_t)sz) : NULL;
    size_t nr = (sz > 0 && buf) ? fread(buf, 1, (size_t)sz, in) : 0;
    fclose(in);
    if (sz > 0 && !buf) return 0;

    char hdr[600];
    int hlen = snprintf(hdr, sizeof(hdr), "FILE %s\n%zu\n", relpath, nr);
    fwrite(hdr, 1, (size_t)hlen, out);
    PkgSha256Update(sha, (const uint8_t *)hdr, (size_t)hlen);
    if (nr > 0) {
        fwrite(buf, 1, nr, out);
        PkgSha256Update(sha, buf, nr);
    }
    fputc('\n', out);
    uint8_t nl = '\n';
    PkgSha256Update(sha, &nl, 1);
    free(buf);
    return 1;
}

static void PkgPackDirRecursive(FILE *out, PKGSHA256CTX *sha, const char *dir_path, const char *rel_prefix) {
#ifdef _WIN32
    char pat[512];
    snprintf(pat, sizeof(pat), "%s\\*", dir_path);
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pat, &ffd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = ffd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (strcmp(name, ".git") == 0 || strcmp(name, ".akspackages") == 0) continue;
        char fpath[512], rpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, name);
        snprintf(rpath, sizeof(rpath), "%s/%s", rel_prefix, name);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            PkgPackDirRecursive(out, sha, fpath, rpath);
        } else {
            PkgPackWriteEntry(out, sha, rpath, fpath);
        }
    } while (FindNextFileA(h, &ffd));
    FindClose(h);
#else
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (strcmp(name, ".git") == 0 || strcmp(name, ".akspackages") == 0) continue;
        char fpath[512], rpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, name);
        snprintf(rpath, sizeof(rpath), "%s/%s", rel_prefix, name);
        struct stat st;
        if (stat(fpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            PkgPackDirRecursive(out, sha, fpath, rpath);
        } else {
            PkgPackWriteEntry(out, sha, rpath, fpath);
        }
    }
    closedir(d);
#endif
}

int PkgCreateBundle(const char *base_dir, const char *entrypoint, const char *out_akspkg, char *out_sha256) {
    if (!base_dir || !out_akspkg) return 0;
    FILE *out = fopen(out_akspkg, "wb");
    if (!out) return 0;
    PKGSHA256CTX sha;
    PkgSha256Init(&sha);
    const char *magic = "AKSPKG1\n";
    fwrite(magic, 1, 8, out);
    PkgSha256Update(&sha, (const uint8_t *)magic, 8);

    char pbuf[512];
    snprintf(pbuf, sizeof(pbuf), "%s/aks.json", base_dir);
    PkgPackWriteEntry(out, &sha, "aks.json", pbuf);

    if (entrypoint && entrypoint[0] &&
        strncmp(entrypoint, "src/", 4) != 0 && strncmp(entrypoint, "src\\", 4) != 0 &&
        strcmp(entrypoint, "aks.json") != 0) {
        char norm_entry[256];
        snprintf(norm_entry, sizeof(norm_entry), "%s", entrypoint);
        for (char *p = norm_entry; *p; p++) if (*p == '\\') *p = '/';
        snprintf(pbuf, sizeof(pbuf), "%s/%s", base_dir, norm_entry);
        PkgPackWriteEntry(out, &sha, norm_entry, pbuf);
    }

    snprintf(pbuf, sizeof(pbuf), "%s/src", base_dir);
    PkgPackDirRecursive(out, &sha, pbuf, "src");

    uint8_t digest[32];
    PkgSha256Final(&sha, digest);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
    hex[64] = '\0';
    fprintf(out, "SHA256 %s\n", hex);
    fclose(out);
    if (out_sha256) strcpy(out_sha256, hex);
    return 1;
}

static int PkgIsSafeRelPath(const char *rel) {
    if (!rel || !rel[0]) return 0;
    if (rel[0] == '/' || rel[0] == '\\' || (rel[1] == ':')) return 0;
    if (strstr(rel, "..") != NULL) return 0;
    return 1;
}

int PkgExtractBundle(const char *bundle_path, const char *dest_dir, char *out_sha256) {
    FILE *f = fopen(bundle_path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 80) { fclose(f); return 0; }
    uint8_t *data = (uint8_t *)malloc((size_t)sz + 1);
    if (!data) { fclose(f); return 0; }
    size_t nr = fread(data, 1, (size_t)sz, f);
    fclose(f);
    data[nr] = '\0';
    if (nr < 80 || memcmp(data, "AKSPKG1\n", 8) != 0) {
        free(data);
        return 0;
    }
    size_t pos = 8;
    while (pos < nr) {
        if (pos + 7 <= nr && memcmp(data + pos, "SHA256 ", 7) == 0) break;
        if (pos + 5 > nr || memcmp(data + pos, "FILE ", 5) != 0) {
            free(data); return 0;
        }
        const char *nl1 = (const char *)memchr(data + pos + 5, '\n', nr - (pos + 5));
        if (!nl1) { free(data); return 0; }
        size_t next_pos = (size_t)(nl1 - (const char *)data) + 1;
        const char *nl2 = (const char *)memchr(data + next_pos, '\n', nr - next_pos);
        if (!nl2) { free(data); return 0; }
        size_t flen = (size_t)strtoull((const char *)(data + next_pos), NULL, 10);
        size_t data_start = (size_t)(nl2 - (const char *)data) + 1;
        if (data_start + flen + 1 > nr) { free(data); return 0; }
        pos = data_start + flen + 1;
    }
    if (pos + 7 + 64 > nr || memcmp(data + pos, "SHA256 ", 7) != 0) {
        free(data);
        return 0;
    }
    char expected_hex[65];
    memcpy(expected_hex, data + pos + 7, 64);
    expected_hex[64] = '\0';
    char actual_hex[65];
    PkgSha256Hex(data, pos, actual_hex);
    if (strcmp(expected_hex, actual_hex) != 0) {
        fprintf(stderr, "Security Alert: .akspkg SHA-256 verification failed! Expected %s, got %s\n",
                expected_hex, actual_hex);
        free(data);
        return 0;
    }
    if (out_sha256) strcpy(out_sha256, actual_hex);
    if (!dest_dir) {
        free(data);
        return 1;
    }

    MkDirp(dest_dir);
    pos = 8;
    while (pos < nr) {
        if (pos + 7 <= nr && memcmp(data + pos, "SHA256 ", 7) == 0) break;
        const char *nl1 = (const char *)memchr(data + pos + 5, '\n', nr - (pos + 5));
        size_t rlen = (size_t)(nl1 - (const char *)(data + pos + 5));
        char rel[512];
        if (rlen >= sizeof(rel)) rlen = sizeof(rel) - 1;
        memcpy(rel, data + pos + 5, rlen);
        rel[rlen] = '\0';
        size_t next_pos = (size_t)(nl1 - (const char *)data) + 1;
        const char *nl2 = (const char *)memchr(data + next_pos, '\n', nr - next_pos);
        size_t flen = (size_t)strtoull((const char *)(data + next_pos), NULL, 10);
        size_t data_start = (size_t)(nl2 - (const char *)data) + 1;
        if (PkgIsSafeRelPath(rel)) {
            char outpath[1024];
            snprintf(outpath, sizeof(outpath), "%s/%s", dest_dir, rel);
            char pdir[1024];
            snprintf(pdir, sizeof(pdir), "%s", outpath);
            char *sep = strrchr(pdir, '/');
            if (sep) { *sep = '\0'; MkDirp(pdir); }
            FILE *wf = fopen(outpath, "wb");
            if (wf) {
                if (flen > 0) fwrite(data + data_start, 1, flen, wf);
                fclose(wf);
            }
        }
        pos = data_start + flen + 1;
    }
    free(data);
    return 1;
}

static int PkgParseHttpUrl(const char *url, int *is_https, char *host, size_t host_sz, int *port, char *path, size_t path_sz) {
    if (!url) return 0;
    const char *p = NULL;
    if (strncmp(url, "http://", 7) == 0) { *is_https = 0; p = url + 7; *port = 80; }
    else if (strncmp(url, "https://", 8) == 0) { *is_https = 1; p = url + 8; *port = 443; }
    else return 0;
    const char *slash = strchr(p, '/');
    size_t hp_len = slash ? (size_t)(slash - p) : strlen(p);
    char hostport[256];
    if (hp_len >= sizeof(hostport)) hp_len = sizeof(hostport) - 1;
    memcpy(hostport, p, hp_len);
    hostport[hp_len] = '\0';
    char *colon = strchr(hostport, ':');
    if (colon) {
        *colon = '\0';
        int parsed_p = atoi(colon + 1);
        if (parsed_p > 0) *port = parsed_p;
    }
    snprintf(host, host_sz, "%s", hostport);
    snprintf(path, path_sz, "%s", slash ? slash : "/");
    return host[0] != '\0';
}

static int PkgHttpRequest(const char *method, const char *url, const char *bearer_token,
                          const char *extra_headers, const uint8_t *body, size_t body_len,
                          uint8_t **out_resp, size_t *out_len, int *out_status) {
    if (out_resp) *out_resp = NULL;
    if (out_len) *out_len = 0;
    if (out_status) *out_status = 0;
    int is_https = 0, port = 80;
    char host[256], path[1024];
    if (!PkgParseHttpUrl(url, &is_https, host, sizeof(host), &port, path, sizeof(path))) return 0;

#ifdef _WIN32
    wchar_t whost[256], wpath[1024], wmethod[16];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 16);

    HINTERNET hSession = WinHttpOpen(L"AKS-Pkg/" AKS_VERSION_WIDE, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return 0;
    HINTERNET hConnect = WinHttpConnect(hSession, whost, (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return 0; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod, wpath, NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return 0; }

    char hdr_buf[2048] = "";
    if (bearer_token && bearer_token[0]) {
        snprintf(hdr_buf, sizeof(hdr_buf), "Authorization: Bearer %s\r\n", bearer_token);
    }
    if (extra_headers && extra_headers[0]) {
        strncat(hdr_buf, extra_headers, sizeof(hdr_buf) - strlen(hdr_buf) - 1);
    }
    if (hdr_buf[0]) {
        wchar_t whdrs[2048];
        MultiByteToWideChar(CP_UTF8, 0, hdr_buf, -1, whdrs, 2048);
        WinHttpAddRequestHeaders(hRequest, whdrs, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);
    if (!ok) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return 0;
    }
    DWORD status = 0, st_sz = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &st_sz, WINHTTP_NO_HEADER_INDEX);
    if (out_status) *out_status = (int)status;

    size_t cap = 4096, len = 0;
    uint8_t *resp = (uint8_t *)malloc(cap);
    DWORD avail = 0;
    while (resp && WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
        if (len + avail + 1 > cap) {
            while (len + avail + 1 > cap) cap *= 2;
            uint8_t *nr = (uint8_t *)realloc(resp, cap);
            if (!nr) break;
            resp = nr;
        }
        DWORD read_now = 0;
        if (!WinHttpReadData(hRequest, resp + len, avail, &read_now) || read_now == 0) break;
        len += read_now;
    }
    if (resp) {
        resp[len] = '\0';
        if (out_resp) *out_resp = resp; else free(resp);
        if (out_len) *out_len = len;
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return 1;
#else
    if (is_https) {
        char cmd[2048];
        char tmpin[128], tmpout[128];
        snprintf(tmpin, sizeof(tmpin), "/tmp/aks_http_in_%lu", (unsigned long)getpid());
        snprintf(tmpout, sizeof(tmpout), "/tmp/aks_http_out_%lu", (unsigned long)getpid());
        if (body && body_len > 0) {
            FILE *bf = fopen(tmpin, "wb");
            if (bf) { fwrite(body, 1, body_len, bf); fclose(bf); }
        }
        snprintf(cmd, sizeof(cmd),
                 "curl -sS -w '\\n%%{http_code}' -X %s %s%s %s%s -o %s '%s'",
                 method,
                 bearer_token && bearer_token[0] ? "-H 'Authorization: Bearer " : "",
                 bearer_token && bearer_token[0] ? bearer_token : "",
                 (body && body_len > 0) ? "--data-binary @" : "",
                 (body && body_len > 0) ? tmpin : "",
                 tmpout, url);
        /* Avoid complex quoting bugs when no token */
        if (bearer_token && bearer_token[0]) {
            snprintf(cmd, sizeof(cmd),
                     "curl -sS -w '%%{http_code}' -X %s -H 'Authorization: Bearer %s' %s%s -o '%s' '%s'",
                     method, bearer_token,
                     (body && body_len > 0) ? "--data-binary @" : "",
                     (body && body_len > 0) ? tmpin : "", tmpout, url);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "curl -sS -w '%%{http_code}' -X %s %s%s -o '%s' '%s'",
                     method,
                     (body && body_len > 0) ? "--data-binary @" : "",
                     (body && body_len > 0) ? tmpin : "", tmpout, url);
        }
        FILE *pp = popen(cmd, "r");
        int code = 0;
        if (pp) {
            if (fscanf(pp, "%d", &code) != 1) code = 0;
            pclose(pp);
        }
        if (body && body_len > 0) remove(tmpin);
        if (out_status) *out_status = code;
        char *fdata = ReadFileContent(tmpout);
        if (fdata) {
            size_t fl = strlen(fdata);
            if (out_resp) *out_resp = (uint8_t *)fdata; else free(fdata);
            if (out_len) *out_len = fl;
        }
        remove(tmpout);
        return code > 0;
    }

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return 0;

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0 || connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        if (sock >= 0) close(sock);
        freeaddrinfo(res);
        return 0;
    }
    freeaddrinfo(res);

    char req_hdr[2048];
    int hlen = snprintf(req_hdr, sizeof(req_hdr),
                        "%s %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: AKS-Pkg/" AKS_VERSION_STRING "\r\n"
                        "Content-Length: %zu\r\nConnection: close\r\n"
                        "%s%s%s%s\r\n",
                        method, path, host, port, body_len,
                        (bearer_token && bearer_token[0]) ? "Authorization: Bearer " : "",
                        (bearer_token && bearer_token[0]) ? bearer_token : "",
                        (bearer_token && bearer_token[0]) ? "\r\n" : "",
                        (extra_headers && extra_headers[0]) ? extra_headers : "");
    size_t sent = 0;
    while (sent < (size_t)hlen) {
        ssize_t w = send(sock, req_hdr + sent, (size_t)hlen - sent, 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    sent = 0;
    while (sent < body_len) {
        ssize_t w = send(sock, body + sent, body_len - sent, 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }

    size_t cap = 8192, rlen = 0;
    uint8_t *raw = (uint8_t *)malloc(cap);
    while (raw) {
        if (rlen + 4096 + 1 > cap) {
            cap *= 2;
            uint8_t *nr = (uint8_t *)realloc(raw, cap);
            if (!nr) break;
            raw = nr;
        }
        ssize_t n = recv(sock, raw + rlen, 4096, 0);
        if (n <= 0) break;
        rlen += (size_t)n;
    }
    close(sock);
    if (!raw || rlen == 0) { free(raw); return 0; }
    raw[rlen] = '\0';

    int status = 0;
    const char *sp = (const char *)memchr(raw, ' ', rlen);
    if (sp) status = atoi(sp + 1);
    if (out_status) *out_status = status;

    const char *hdr_end = strstr((const char *)raw, "\r\n\r\n");
    size_t sep_len = 4;
    if (!hdr_end) { hdr_end = strstr((const char *)raw, "\n\n"); sep_len = 2; }
    if (hdr_end) {
        size_t blen = rlen - (size_t)(hdr_end + sep_len - (const char *)raw);
        if (out_resp) {
            uint8_t *rbody = (uint8_t *)malloc(blen + 1);
            if (rbody) {
                if (blen > 0) memcpy(rbody, hdr_end + sep_len, blen);
                rbody[blen] = '\0';
            }
            *out_resp = rbody;
        }
        if (out_len) *out_len = blen;
    }
    free(raw);
    return status > 0;
#endif
}

// package manager commands
int PkgInit(const char *name) {
    char proj_name[256];
    char target_dir[512] = "";
    int is_current_dir = 0;

    if (!name || strcmp(name, ".") == 0 || strlen(name) == 0) {
        is_current_dir = 1;
        char cwd[512];
#ifdef _WIN32
        if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
            char *last_slash = strrchr(cwd, '\\');
            if (!last_slash) last_slash = strrchr(cwd, '/');
            if (last_slash && *(last_slash + 1)) {
                snprintf(proj_name, sizeof(proj_name), "%s", last_slash + 1);
            } else {
                snprintf(proj_name, sizeof(proj_name), "aks_app");
            }
        } else {
            snprintf(proj_name, sizeof(proj_name), "aks_app");
        }
#else
        if (getcwd(cwd, sizeof(cwd))) {
            char *last_slash = strrchr(cwd, '/');
            if (!last_slash) last_slash = strrchr(cwd, '/');
            if (last_slash && *(last_slash + 1)) {
                snprintf(proj_name, sizeof(proj_name), "%s", last_slash + 1);
            } else {
                snprintf(proj_name, sizeof(proj_name), "aks_app");
            }
        } else {
            snprintf(proj_name, sizeof(proj_name), "aks_app");
        }
#endif
    } else {
        snprintf(proj_name, sizeof(proj_name), "%s", name);
        snprintf(target_dir, sizeof(target_dir), "%s", name);
        MkDirp(target_dir);
    }

    printf("[init] Scaffolding AKS project '%s'...\n", proj_name);

    char srcdir[512];
    char testdir[512];
    if (is_current_dir) {
        snprintf(srcdir, sizeof(srcdir), "src");
        snprintf(testdir, sizeof(testdir), "tests");
    } else {
        snprintf(srcdir, sizeof(srcdir), "%s/src", target_dir);
        snprintf(testdir, sizeof(testdir), "%s/tests", target_dir);
    }
    MkDirp(srcdir);
    MkDirp(testdir);

    char manifestpath[512];
    if (is_current_dir) {
        snprintf(manifestpath, sizeof(manifestpath), "aks.json");
    } else {
        snprintf(manifestpath, sizeof(manifestpath), "%s/aks.json", target_dir);
    }

    FILE *f = fopen(manifestpath, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not create %s\n", manifestpath);
        return 1;
    }
    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", proj_name);
    fprintf(f, "  \"version\": \"1.0.0\",\n");
    fprintf(f, "  \"description\": \"A new AKS application\",\n");
    fprintf(f, "  \"entrypoint\": \"src/main.aks\",\n");
    fprintf(f, "  \"dependencies\": {}\n");
    fprintf(f, "}\n");
    fclose(f);

    char mainpath[512];
    if (is_current_dir) {
        snprintf(mainpath, sizeof(mainpath), "src/main.aks");
    } else {
        snprintf(mainpath, sizeof(mainpath), "%s/src/main.aks", target_dir);
    }
    FILE *mainf = fopen(mainpath, "w");
    if (mainf) {
        fprintf(mainf, "// Main entrypoint for %s\n", proj_name);
        fprintf(mainf, "fn main() {\n");
        fprintf(mainf, "    print(\"Hello from %s!\");\n", proj_name);
        fprintf(mainf, "}\n\n");
        fprintf(mainf, "main();\n");
        fclose(mainf);
    }

    char testpath[512];
    if (is_current_dir) {
        snprintf(testpath, sizeof(testpath), "tests/main_test.aks");
    } else {
        snprintf(testpath, sizeof(testpath), "%s/tests/main_test.aks", target_dir);
    }
    FILE *testf = fopen(testpath, "w");
    if (testf) {
        fprintf(testf, "// Unit tests for %s\n", proj_name);
        fprintf(testf, "fn test_basic() {\n");
        fprintf(testf, "    assert_eq(1 + 1, 2);\n");
        fprintf(testf, "}\n\n");
        fprintf(testf, "test_basic();\n");
        fprintf(testf, "print(\"All tests passed!\");\n");
        fclose(testf);
    }

    char gitignorepath[512];
    if (is_current_dir) {
        snprintf(gitignorepath, sizeof(gitignorepath), ".gitignore");
    } else {
        snprintf(gitignorepath, sizeof(gitignorepath), "%s/.gitignore", target_dir);
    }
    FILE *check_gi = fopen(gitignorepath, "r");
    if (check_gi) {
        fclose(check_gi);
    } else {
        FILE *gif = fopen(gitignorepath, "w");
        if (gif) {
            fprintf(gif, "# AKS build artifacts and binaries\n");
            fprintf(gif, "build/\n");
            fprintf(gif, "dist/\n");
            fprintf(gif, "*.exe\n");
            fprintf(gif, "*.wasm\n");
            fprintf(gif, "*.wat\n");
            fprintf(gif, "*.aksb\n");
            fprintf(gif, "*.o\n");
            fprintf(gif, "*.obj\n");
            fprintf(gif, "*.pdb\n");
            fprintf(gif, "*.db\n");
            fclose(gif);
        }
    }

    printf("[init] Project '%s' scaffolded successfully!\n", proj_name);
    if (is_current_dir) {
        printf("  Created:\n");
        printf("    - aks.json\n");
        printf("    - src/main.aks\n");
        printf("    - tests/main_test.aks\n");
        printf("    - .gitignore\n");
        printf("  Next:\n");
        printf("    aks run src/main.aks\n");
        printf("    aks test tests/main_test.aks\n");
    } else {
        printf("  Created in %s/:\n", target_dir);
        printf("    - %s/aks.json\n", target_dir);
        printf("    - %s/src/main.aks\n", target_dir);
        printf("    - %s/tests/main_test.aks\n", target_dir);
        printf("    - %s/.gitignore\n", target_dir);
        printf("  Next:\n");
        printf("    cd %s\n", target_dir);
        printf("    aks run src/main.aks\n");
        printf("    aks test tests/main_test.aks\n");
    }
    return 0;
}

int PkgPublish(void) {
    return PkgPublishTo(NULL);
}

int PkgPublishTo(const char *registry_url) {
    char *json = ReadFileContent("aks.json");
    if (!json) {
        fprintf(stderr, "Error: Missing aks.json manifest\n");
        return 1;
    }
    
    char name[128];
    char version[64];
    char entrypoint[256];
    char manifest_reg[256] = "";
    
    if (!JSONGetVal(json, "name", name, sizeof(name)) ||
        !JSONGetVal(json, "version", version, sizeof(version)) ||
        !JSONGetVal(json, "entrypoint", entrypoint, sizeof(entrypoint))) {
        fprintf(stderr, "Error: Invalid aks.json manifest structure\n");
        free(json);
        return 1;
    }
    JSONGetVal(json, "registry", manifest_reg, sizeof(manifest_reg));
    free(json);
    
    printf("Publishing package '%s@%s' to local registry\n", name, version);
    
    char destdir[1024];
    snprintf(destdir, sizeof(destdir), "%s/%s/%s", GetRegIsTryDir(), name, version);
    MkDirp(destdir);
    
    // copy manifest
    char dstmanifest[1536];
    snprintf(dstmanifest, sizeof(dstmanifest), "%s/aks.json", destdir);
    PkgCopyFile("aks.json", dstmanifest);
    
    // copy entrypoint and src/ recursively
    char dstentry[1536];
    snprintf(dstentry, sizeof(dstentry), "%s/%s", destdir, entrypoint);
    PkgCopyFile(entrypoint, dstentry);
    char dstsrc[1536];
    snprintf(dstsrc, sizeof(dstsrc), "%s/src", destdir);
    PkgCopyDirRecursive("src", dstsrc);

    char reg_bundle[1536];
    snprintf(reg_bundle, sizeof(reg_bundle), "%s/%s-%s.akspkg", destdir, name, version);
    char sha_hex[65] = "";
    PkgCreateBundle(".", entrypoint, reg_bundle, sha_hex);

    printf("Local registry: %s (SHA-256: %s)\n", destdir, sha_hex);

    const char *env_reg = getenv("AKS_REGISTRY_URL");
    const char *target_url = registry_url ? registry_url :
                             (manifest_reg[0] != '\0' ? manifest_reg :
                             ((env_reg && env_reg[0]) ? env_reg : NULL));
    if (target_url) {
        char *auth_tok = PkgGetStoredToken(target_url);
        printf("Publishing package to remote registry: %s\n", target_url);
        if (auth_tok) {
            printf("Authenticated with bearer token: %.8s...\n", auth_tok);
        } else {
            printf("Notice: Publishing anonymously. Run 'aks login <token>' to use credentials.\n");
        }
        char local_bundle[512];
        snprintf(local_bundle, sizeof(local_bundle), "%s-%s.akspkg", name, version);
        if (!PkgCreateBundle(".", entrypoint, local_bundle, sha_hex)) {
            fprintf(stderr, "Error: Failed to create package bundle '%s'\n", local_bundle);
            if (auth_tok) free(auth_tok);
            return 1;
        }
        printf("Packaging bundle: %s (SHA-256: %s)\n", local_bundle, sha_hex);

        FILE *bf = fopen(local_bundle, "rb");
        long bsz = 0;
        uint8_t *bdata = NULL;
        if (bf) {
            fseek(bf, 0, SEEK_END);
            bsz = ftell(bf);
            rewind(bf);
            if (bsz > 0) {
                bdata = (uint8_t *)malloc((size_t)bsz);
                if (bdata && fread(bdata, 1, (size_t)bsz, bf) != (size_t)bsz) {
                    free(bdata);
                    bdata = NULL;
                }
            }
            fclose(bf);
        }
        char extra_hdrs[512];
        snprintf(extra_hdrs, sizeof(extra_hdrs),
                 "Content-Type: application/octet-stream\r\n"
                 "X-Package-Name: %s\r\n"
                 "X-Package-Version: %s\r\n"
                 "X-Package-Sha256: %s\r\n",
                 name, version, sha_hex);
        uint8_t *resp = NULL;
        size_t rlen = 0;
        int status = 0;
        int ok = bdata && PkgHttpRequest("POST", target_url, auth_tok, extra_hdrs,
                                         bdata, (size_t)bsz, &resp, &rlen, &status);
        free(bdata);
        if (auth_tok) free(auth_tok);
        if (resp) free(resp);
        if (!ok || status < 200 || status >= 300) {
            fprintf(stderr, "Error: Remote registry upload failed (HTTP %d) to %s\n", status, target_url);
            return 1;
        }
        printf("Remote upload complete (HTTP %d). Package '%s@%s' is live on %s\n",
               status, name, version, target_url);
    }
    
    printf("Package published successfully.\n");
    return 0;
}

int PkgSearch(const char *query) {
    printf("Searching AKS package registry for '%s'...\n\n", query ? query : "*");
    const char *regdir = GetRegIsTryDir();
    int found_count = 0;

    printf("  %-24s  %-12s  %s\n", "Package", "Version", "Status");
    printf("  %-24s  %-12s  %s\n", "------------------------", "------------", "---------");

#ifdef _WIN32
    char searchpat[512];
    snprintf(searchpat, sizeof(searchpat), "%s\\*", regdir);
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(searchpat, &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const char *pkgname = ffd.cFileName;
#else
    DIR *d = opendir(regdir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            const char *pkgname = de->d_name;
#endif
            if (strcmp(pkgname, ".") == 0 || strcmp(pkgname, "..") == 0) continue;
            if (!query || strlen(query) == 0 || strstr(pkgname, query) != NULL) {
                char latest_ver[64] = "1.0.0";
#ifdef _WIN32
                char verpat[512];
                snprintf(verpat, sizeof(verpat), "%s\\%s\\*", regdir, pkgname);
                WIN32_FIND_DATAA vfd;
                HANDLE hVer = FindFirstFileA(verpat, &vfd);
                if (hVer != INVALID_HANDLE_VALUE) {
                    do {
                        if ((vfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                            strcmp(vfd.cFileName, ".") != 0 && strcmp(vfd.cFileName, "..") != 0) {
                            snprintf(latest_ver, sizeof(latest_ver), "%s", vfd.cFileName);
                        }
                    } while (FindNextFileA(hVer, &vfd));
                    FindClose(hVer);
                }
#else
                char pkgsub[1024];
                snprintf(pkgsub, sizeof(pkgsub), "%s/%s", regdir, pkgname);
                struct stat pst;
                if (stat(pkgsub, &pst) != 0 || !S_ISDIR(pst.st_mode)) continue;
                DIR *vd = opendir(pkgsub);
                if (vd) {
                    struct dirent *vde;
                    while ((vde = readdir(vd)) != NULL) {
                        if (strcmp(vde->d_name, ".") != 0 && strcmp(vde->d_name, "..") != 0) {
                            snprintf(latest_ver, sizeof(latest_ver), "%.63s", vde->d_name);
                        }
                    }
                    closedir(vd);
                }
#endif
                printf("  %-24s  %-12s  [installed]\n", pkgname, latest_ver);
                found_count++;
            }
#ifdef _WIN32
        } while (FindNextFileA(hFind, &ffd));
        FindClose(hFind);
    }
#else
        }
        closedir(d);
    }
#endif

    const char *env_reg = getenv("AKS_REGISTRY_URL");
    if (env_reg && env_reg[0]) {
        char qurl[768];
        snprintf(qurl, sizeof(qurl), "%s/search?q=%s", env_reg, query ? query : "");
        uint8_t *resp = NULL;
        size_t rlen = 0;
        int status = 0;
        if (PkgHttpRequest("GET", qurl, NULL, NULL, NULL, 0, &resp, &rlen, &status) &&
            resp && status >= 200 && status < 300) {
            printf("  Remote registry (%s): %s\n", env_reg, (const char *)resp);
            found_count++;
        }
        if (resp) free(resp);
    }

    if (found_count == 0) {
        printf("  (no local packages matching '%s' found in %s)\n", query ? query : "*", regdir);
    }
    printf("\nTotal matching packages: %d\n", found_count);
    return 0;
}

int PkgAdd(const char *pkgspec) {
    char pkgname[128] = "";
    char pkgversion[512] = "*";
    
    const char *at = strchr(pkgspec, '@');
    const char *proto = strstr(pkgspec, "://");
    if (at && (!proto || at < proto)) {
        int namelen = (int)(at - pkgspec);
        if (namelen > 127) namelen = 127;
        strncpy(pkgname, pkgspec, namelen);
        pkgname[namelen] = '\0';
        snprintf(pkgversion, sizeof(pkgversion), "%.511s", at + 1);
    } else if (strncmp(pkgspec, "path:", 5) == 0 ||
               strncmp(pkgspec, "./", 2) == 0 || strncmp(pkgspec, ".\\", 2) == 0 ||
               strncmp(pkgspec, "../", 3) == 0 || strncmp(pkgspec, "..\\", 3) == 0) {
        const char *src = pkgspec;
        if (strncmp(src, "path:", 5) == 0) src += 5;
        char cleanpath[512];
        snprintf(cleanpath, sizeof(cleanpath), "%.511s", src);
        size_t plen = strlen(cleanpath);
        while (plen > 0 && (cleanpath[plen - 1] == '/' || cleanpath[plen - 1] == '\\')) {
            cleanpath[--plen] = '\0';
        }
        const char *slash = cleanpath + plen;
        while (slash > cleanpath && *(slash - 1) != '/' && *(slash - 1) != '\\') slash--;
        if (strlen(slash) > 0) {
            snprintf(pkgname, sizeof(pkgname), "%.127s", slash);
        } else {
            snprintf(pkgname, sizeof(pkgname), "pathpkg");
        }
        snprintf(pkgversion, sizeof(pkgversion), "path:%.500s", cleanpath);
    } else if (strncmp(pkgspec, "git+", 4) == 0 ||
               strncmp(pkgspec, "git@", 4) == 0 ||
               strncmp(pkgspec, "http://", 7) == 0 ||
               strncmp(pkgspec, "https://", 8) == 0 ||
               strstr(pkgspec, ".git") != NULL) {
        const char *src = pkgspec;
        if (strncmp(src, "git+", 4) == 0) src += 4;
        const char *hashp = strchr(src, '#');
        const char *end = hashp ? hashp : src + strlen(src);
        const char *dotgit = strstr(src, ".git");
        if (dotgit && dotgit < end) {
            end = dotgit;
        }
        const char *slash = end;
        while (slash > src && *(slash - 1) != '/' && *(slash - 1) != '\\') slash--;
        int nlen = (int)(end - slash);
        if (nlen > 0 && nlen < 128) {
            strncpy(pkgname, slash, nlen);
            pkgname[nlen] = '\0';
        } else {
            strncpy(pkgname, "gitpkg", sizeof(pkgname) - 1);
        }
        strncpy(pkgversion, pkgspec, sizeof(pkgversion) - 1);
    } else {
        strncpy(pkgname, pkgspec, sizeof(pkgname) - 1);
    }
    
    printf("Adding dependency '%s' with constraint '%s'\n", pkgname, pkgversion);
    
    char *json = ReadFileContent("aks.json");
    if (!json) {
        fprintf(stderr, "Error: Missing aks.json manifest\n");
        return 1;
    }
    
    // simple JSON injection for dependency
    FILE *f = fopen("aks.json", "w");
    if (!f) {
        fprintf(stderr, "Error: Could not open aks.json for writing\n");
        free(json);
        return 1;
    }
    
    // search and append dependency key pair
    const char *deppos = strstr(json, "\"dependencies\"");
    if (!deppos) {
        fprintf(stderr, "Error: Invalid JSON format\n");
        free(json);
        fclose(f);
        return 1;
    }
    
    // output everything before dependencies
    fwrite(json, 1, deppos - json, f);
    
    // output dependencies block
    fprintf(f, "\"dependencies\": {\n");
    fprintf(f, "    \"%s\": \"%s\"", pkgname, pkgversion);
    
    // extract existing dependencies if any
    const char *brace = strchr(deppos, '{');
    if (brace) {
        brace++;
        while (*brace == ' ' || *brace == '\t' || *brace == '\r' || *brace == '\n') brace++;
        if (*brace != '}') {
            fprintf(f, ",\n    ");
            // output remainder of existing deps
            const char *endbrace = strchr(brace, '}');
            if (endbrace) {
                fwrite(brace, 1, endbrace - brace, f);
            }
        }
    }
    
    // output remainder of json
    const char *enddep = strchr(deppos, '}');
    if (enddep) {
        fprintf(f, "\n  }");
        fprintf(f, "%s", enddep + 1);
    }
    
    fclose(f);
    free(json);
    printf("Dependency added successfully.\n");
    return 0;
}

// recursive dependency resolver & lockfile writer
typedef struct {
    char name[128];
    char version[64];
    char hash[65];
    int is_git;
} ResolvedDep;

static ResolvedDep gresolved[128];
static int gresolvedcount = 0;

static int ResolvePackage(const char *name, const char *constraint) {
    for (int i = 0; i < gresolvedcount; i++) {
        if (strcmp(gresolved[i].name, name) == 0) {
            SemVer ver;
            ParseSemver(gresolved[i].version, &ver);
            if (VersionMatch(ver, constraint)) {
                return 1;
            } else {
                fprintf(stderr, "Dependency Error: Version conflict for package '%s'. Already resolved to '%s', which does not satisfy '%s'\n", name, gresolved[i].version, constraint);
                return 0;
            }
        }
    }
    
    if (strncmp(constraint, "path:", 5) == 0) {
        const char *srcpath = constraint + 5;
        char normsrc[512];
        snprintf(normsrc, sizeof(normsrc), "%s", srcpath);
        for (char *p = normsrc; *p; p++) { if (*p == '\\') *p = '/'; }
        char manifestpath[1024];
        snprintf(manifestpath, sizeof(manifestpath), "%s/aks.json", normsrc);
        
        char *srcmanifest = ReadFileContent(manifestpath);
        if (!srcmanifest) {
            fprintf(stderr, "Error: Could not find aks.json at path dependency '%s'\n", srcpath);
            return 0;
        }
        
        char entrypoint[256] = "src/main.aks";
        JSONGetVal(srcmanifest, "entrypoint", entrypoint, sizeof(entrypoint));
        free(srcmanifest);
        
        char venvpkgdir[512];
        snprintf(venvpkgdir, sizeof(venvpkgdir), ".akspackages/%s", name);
        MkDirp(".akspackages");
        PkgCopyDirRecursive(normsrc, venvpkgdir);
        
        char entrypath[1024];
        snprintf(entrypath, sizeof(entrypath), "%s/%s", venvpkgdir, entrypoint);
        for (char *p = entrypath; *p; p++) { if (*p == '\\') *p = '/'; }
        
        char computed_hash[65] = "";
        if (!Sha256File(entrypath, computed_hash)) {
            char altpath[1024];
            snprintf(altpath, sizeof(altpath), "%s/src/main.aks", venvpkgdir);
            if (!Sha256File(altpath, computed_hash)) {
                snprintf(altpath, sizeof(altpath), "%s/index.aks", venvpkgdir);
                if (!Sha256File(altpath, computed_hash)) {
                    snprintf(altpath, sizeof(altpath), "%s/aks.json", venvpkgdir);
                    Sha256File(altpath, computed_hash);
                }
            }
        }
        
        strcpy(gresolved[gresolvedcount].name, name);
        snprintf(gresolved[gresolvedcount].version, sizeof(gresolved[gresolvedcount].version),
                 "path:%s", srcpath);
        strcpy(gresolved[gresolvedcount].hash, computed_hash);
        gresolved[gresolvedcount].is_git = 1;
        gresolvedcount++;
        printf("Installed path package '%s@path:%s' (SHA-256: %s) to local environment.\n",
               name, srcpath, computed_hash);
        return 1;
    }
    
    if ((strncmp(constraint, "http://", 7) == 0 || strncmp(constraint, "https://", 8) == 0) &&
        strstr(constraint, ".akspkg") != NULL) {
        uint8_t *resp = NULL;
        size_t rlen = 0;
        int status = 0;
        char *tok = PkgGetStoredToken(constraint);
        int ok = PkgHttpRequest("GET", constraint, tok, NULL, NULL, 0, &resp, &rlen, &status);
        if (tok) free(tok);
        if (!ok || !resp || status < 200 || status >= 300 || rlen < 80) {
            if (resp) free(resp);
            fprintf(stderr, "Error: HTTP download failed (HTTP %d) for package '%s' from '%s'\n",
                    status, name, constraint);
            return 0;
        }
        MkDirp(".akspackages");
        char tmp_akspkg[512];
        snprintf(tmp_akspkg, sizeof(tmp_akspkg), ".akspackages/%s.download.akspkg", name);
        FILE *tf = fopen(tmp_akspkg, "wb");
        if (!tf) { free(resp); return 0; }
        fwrite(resp, 1, rlen, tf);
        fclose(tf);
        free(resp);

        char venvpkgdir[512];
        snprintf(venvpkgdir, sizeof(venvpkgdir), ".akspackages/%s", name);
        char computed_hash[65] = "";
        int ext_ok = PkgExtractBundle(tmp_akspkg, venvpkgdir, computed_hash);
        remove(tmp_akspkg);
        if (!ext_ok) {
            fprintf(stderr, "Security Error: .akspkg bundle verification failed for '%s'\n", name);
            return 0;
        }
        strcpy(gresolved[gresolvedcount].name, name);
        snprintf(gresolved[gresolvedcount].version, sizeof(gresolved[gresolvedcount].version), "%s", "http");
        strcpy(gresolved[gresolvedcount].hash, computed_hash);
        gresolved[gresolvedcount].is_git = 1;
        gresolvedcount++;
        printf("Installed HTTP bundle '%s' (SHA-256: %s) to local environment.\n", name, computed_hash);
        return 1;
    }

    if (strncmp(constraint, "git+", 4) == 0 ||
        strncmp(constraint, "git@", 4) == 0 ||
        strncmp(constraint, "http://", 7) == 0 ||
        strncmp(constraint, "https://", 8) == 0 ||
        strstr(constraint, ".git") != NULL) {
        char giturl[512] = "";
        char gitref[128] = "";
        const char *src = constraint;
        if (strncmp(src, "git+", 4) == 0) src += 4;
        const char *hashp = strchr(src, '#');
        if (hashp) {
            int ulen = (int)(hashp - src);
            if (ulen > 511) ulen = 511;
            strncpy(giturl, src, ulen);
            giturl[ulen] = '\0';
            strncpy(gitref, hashp + 1, sizeof(gitref) - 1);
        } else {
            strncpy(giturl, src, sizeof(giturl) - 1);
        }
        char venvpkgdir[512];
        snprintf(venvpkgdir, sizeof(venvpkgdir), ".akspackages/%s", name);
        MkDirp(".akspackages");
        
        char manifestpath[1024];
        snprintf(manifestpath, sizeof(manifestpath), "%s/aks.json", venvpkgdir);
        FILE *tf = fopen(manifestpath, "r");
        if (tf) {
            fclose(tf);
        } else {
            char cmd[1024];
#ifdef _WIN32
            const char *devnull = "2>nul";
#else
            const char *devnull = "2>/dev/null";
#endif
            if (strlen(gitref) > 0) {
                snprintf(cmd, sizeof(cmd), "git clone --depth 1 --branch %s %s %s %s", gitref, giturl, venvpkgdir, devnull);
                if (system(cmd) != 0) {
                    snprintf(cmd, sizeof(cmd), "git clone %s %s %s", giturl, venvpkgdir, devnull);
                    if (system(cmd) != 0) {}
                }
            } else {
                snprintf(cmd, sizeof(cmd), "git clone --depth 1 %s %s %s", giturl, venvpkgdir, devnull);
                if (system(cmd) != 0) {}
            }
        }
        
        char entrypoint[256] = "src/main.aks";
        char *venvmanifest = ReadFileContent(manifestpath);
        if (venvmanifest) {
            JSONGetVal(venvmanifest, "entrypoint", entrypoint, sizeof(entrypoint));
            free(venvmanifest);
        }
        char entrypath[1024];
        snprintf(entrypath, sizeof(entrypath), "%s/%s", venvpkgdir, entrypoint);
        for (char *p = entrypath; *p; p++) {
            if (*p == '\\') *p = '/';
        }
        char computed_hash[65] = "";
        if (!Sha256File(entrypath, computed_hash)) {
            char altpath[1024];
            snprintf(altpath, sizeof(altpath), "%s/src/main.aks", venvpkgdir);
            if (!Sha256File(altpath, computed_hash)) {
                snprintf(altpath, sizeof(altpath), "%s/index.aks", venvpkgdir);
                if (!Sha256File(altpath, computed_hash)) {
                    snprintf(altpath, sizeof(altpath), "%s/aks.json", venvpkgdir);
                    Sha256File(altpath, computed_hash);
                }
            }
        }
        
        strcpy(gresolved[gresolvedcount].name, name);
        snprintf(gresolved[gresolvedcount].version, sizeof(gresolved[gresolvedcount].version),
                 "git:%.59s", strlen(gitref) > 0 ? gitref : "HEAD");
        strcpy(gresolved[gresolvedcount].hash, computed_hash);
        gresolved[gresolvedcount].is_git = 1;
        gresolvedcount++;
        return 1;
    }
    
    char regpkgdir[1024];
    snprintf(regpkgdir, sizeof(regpkgdir), "%s%c%s", GetRegIsTryDir(), PATHSEP, name);

    char foundversion[64] = "";
    SemVer bestver = {-1, -1, -1, 0};

#ifdef _WIN32
    char searchpat[1024];
    snprintf(searchpat, sizeof(searchpat), "%s\\*", regpkgdir);
    WIN32_FIND_DATAA ffd;
    HANDLE hfind = FindFirstFileA(searchpat, &ffd);
    if (hfind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const char *cand = ffd.cFileName;
#else
    DIR *dir = opendir(regpkgdir);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            const char *cand = ent->d_name;
#endif
            if (strcmp(cand, ".") == 0 || strcmp(cand, "..") == 0) continue;
            char chkpath[1536];
            snprintf(chkpath, sizeof(chkpath), "%s%c%.255s%caks.json", regpkgdir, PATHSEP, cand, PATHSEP);
            FILE *tf = fopen(chkpath, "r");
            if (!tf) continue;
            fclose(tf);
            SemVer parsed;
            if (!ParseSemver(cand, &parsed)) continue;
            if (!VersionMatch(parsed, constraint)) continue;
            int better = (bestver.major < 0) ||
                         (parsed.major > bestver.major) ||
                         (parsed.major == bestver.major && parsed.minor > bestver.minor) ||
                         (parsed.major == bestver.major && parsed.minor == bestver.minor && parsed.patch > bestver.patch);
            if (better) {
                bestver = parsed;
                snprintf(foundversion, sizeof(foundversion), "%.63s", cand);
            }
#ifdef _WIN32
        } while (FindNextFileA(hfind, &ffd));
        FindClose(hfind);
    }
#else
        }
        closedir(dir);
    }
#endif

    if (strlen(foundversion) == 0) {
        const char *env_reg = getenv("AKS_REGISTRY_URL");
        if (env_reg && env_reg[0]) {
            char meta_url[768];
            snprintf(meta_url, sizeof(meta_url), "%s/%s", env_reg, name);
            uint8_t *mresp = NULL;
            size_t mlen = 0;
            int mstatus = 0;
            if (PkgHttpRequest("GET", meta_url, NULL, NULL, NULL, 0, &mresp, &mlen, &mstatus) &&
                mresp && mstatus >= 200 && mstatus < 300) {
                char rver[64] = "";
                char dl_url[768] = "";
                JSONGetVal((const char *)mresp, "version", rver, sizeof(rver));
                JSONGetVal((const char *)mresp, "url", dl_url, sizeof(dl_url));
                free(mresp);
                SemVer rv;
                if (rver[0] && ParseSemver(rver, &rv) && VersionMatch(rv, constraint)) {
                    if (!dl_url[0]) {
                        snprintf(dl_url, sizeof(dl_url), "%s/%s/%s/%s-%s.akspkg",
                                 env_reg, name, rver, name, rver);
                    }
                    uint8_t *bresp = NULL;
                    size_t blen = 0;
                    int bstatus = 0;
                    if (PkgHttpRequest("GET", dl_url, NULL, NULL, NULL, 0, &bresp, &blen, &bstatus) &&
                        bresp && bstatus >= 200 && bstatus < 300) {
                        char dstdir[1024], bpath[1536];
                        snprintf(dstdir, sizeof(dstdir), "%s/%.127s/%.63s", GetRegIsTryDir(), name, rver);
                        MkDirp(dstdir);
                        snprintf(bpath, sizeof(bpath), "%.1023s/%.127s-%.63s.akspkg", dstdir, name, rver);
                        FILE *bf = fopen(bpath, "wb");
                        if (bf) { fwrite(bresp, 1, blen, bf); fclose(bf); }
                        free(bresp);
                        if (PkgExtractBundle(bpath, dstdir, NULL)) {
                            snprintf(foundversion, sizeof(foundversion), "%.63s", rver);
                        }
                    } else if (bresp) {
                        free(bresp);
                    }
                }
            } else if (mresp) {
                free(mresp);
            }
        }
    }

    if (strlen(foundversion) == 0) {
        fprintf(stderr, "Dependency Error: Could not find version matching constraint '%s' for package '%s' in registry '%s'\n", constraint, name, regpkgdir);
        return 0;
    }

    strcpy(gresolved[gresolvedcount].name, name);
    strcpy(gresolved[gresolvedcount].version, foundversion);
    gresolvedcount++;

    char chkpath[1200];
    snprintf(chkpath, sizeof(chkpath), "%s%c%s%caks.json", regpkgdir, PATHSEP, foundversion, PATHSEP);
    char *subjson = ReadFileContent(chkpath);
    if (subjson) {
        char depsblock[1024];
        if (JSONGetVal(subjson, "dependencies", depsblock, sizeof(depsblock))) {
            char *p = depsblock;
            while (*p) {
                if (*p == '"') {
                    p++;
                    char depname[128] = "";
                    int idx = 0;
                    while (*p && *p != '"') depname[idx++] = *p++;
                    depname[idx] = '\0';
                    if (*p == '"') p++;
                    while (*p == ' ' || *p == ':' || *p == '\t') p++;
                    if (*p == '"') {
                        p++;
                        char depconstraint[128] = "";
                        int cidx = 0;
                        while (*p && *p != '"') depconstraint[cidx++] = *p++;
                        depconstraint[cidx] = '\0';
                        if (!ResolvePackage(depname, depconstraint)) {
                            free(subjson);
                            return 0;
                        }
                    } else if (*p == '{') {
                        char objblock[512] = "";
                        int obidx = 0;
                        int depth = 0;
                        while (*p && obidx < 511) {
                            if (*p == '{') depth++;
                            else if (*p == '}') {
                                depth--;
                                objblock[obidx++] = *p++;
                                if (depth == 0) break;
                                continue;
                            }
                            objblock[obidx++] = *p++;
                        }
                        objblock[obidx] = '\0';
                        char pathval[256] = "";
                        if (JSONGetVal(objblock, "path", pathval, sizeof(pathval))) {
                            char depconstraint[512];
                            snprintf(depconstraint, sizeof(depconstraint), "path:%s", pathval);
                            if (!ResolvePackage(depname, depconstraint)) {
                                free(subjson);
                                return 0;
                            }
                        }
                    }
                }
                p++;
            }
        }
        free(subjson);
    }
    
    return 1;
}

int PkgInstall(void) {
    char *json = ReadFileContent("aks.json");
    if (!json) {
        fprintf(stderr, "Error: Missing aks.json manifest\n");
        return 1;
    }
    
    ParseLockfile();
    
    printf("Resolving package dependencies\n");
    gresolvedcount = 0;
    
    char depsblock[2048];
    if (JSONGetVal(json, "dependencies", depsblock, sizeof(depsblock))) {
        char *p = depsblock;
        while (*p) {
            if (*p == '"') {
                p++;
                char depname[128] = "";
                int idx = 0;
                while (*p && *p != '"') depname[idx++] = *p++;
                depname[idx] = '\0';
                if (*p == '"') p++;
                while (*p == ' ' || *p == ':' || *p == '\t') p++;
                if (*p == '"') {
                    p++;
                    char depconstraint[128] = "";
                    int cidx = 0;
                    while (*p && *p != '"') depconstraint[cidx++] = *p++;
                    depconstraint[cidx] = '\0';
                    if (!ResolvePackage(depname, depconstraint)) {
                        free(json);
                        return 1;
                    }
                } else if (*p == '{') {
                    char objblock[512] = "";
                    int obidx = 0;
                    int depth = 0;
                    while (*p && obidx < 511) {
                        if (*p == '{') depth++;
                        else if (*p == '}') {
                            depth--;
                            objblock[obidx++] = *p++;
                            if (depth == 0) break;
                            continue;
                        }
                        objblock[obidx++] = *p++;
                    }
                    objblock[obidx] = '\0';
                    char pathval[256] = "";
                    if (JSONGetVal(objblock, "path", pathval, sizeof(pathval))) {
                        char depconstraint[512];
                        snprintf(depconstraint, sizeof(depconstraint), "path:%s", pathval);
                        if (!ResolvePackage(depname, depconstraint)) {
                            free(json);
                            return 1;
                        }
                    }
                }
            }
            p++;
        }
    }
    free(json);
    
    MkDirp(".akspackages");
    for (int i = 0; i < gresolvedcount; i++) {
        if (gresolved[i].is_git) {
            continue;
        }
        char regpkgdir[1024];
        snprintf(regpkgdir, sizeof(regpkgdir), "%s/%.127s/%.63s", GetRegIsTryDir(), gresolved[i].name, gresolved[i].version);
        
        char srcmanifest[1024];
        snprintf(srcmanifest, sizeof(srcmanifest), "%.900s/aks.json", regpkgdir);
        
        char *venvmanifest = ReadFileContent(srcmanifest);
        if (!venvmanifest) {
            fprintf(stderr, "Error: Resolved package registry manifest not found at '%s'\n", srcmanifest);
            return 1;
        }
        
        char entrypoint[256] = "";
        JSONGetVal(venvmanifest, "entrypoint", entrypoint, sizeof(entrypoint));
        free(venvmanifest);
        
        char srcentry[1024];
        snprintf(srcentry, sizeof(srcentry), "%.700s/%.255s", regpkgdir, entrypoint);
        
        char computed_hash[65] = "";
        if (!Sha256File(srcentry, computed_hash)) {
            fprintf(stderr, "Error: Could not calculate SHA-256 hash for '%s'\n", srcentry);
            return 1;
        }
        strcpy(gresolved[i].hash, computed_hash);
        
        for (int j = 0; j < glockdepscount; j++) {
            if (strcmp(glockdeps[j].name, gresolved[i].name) == 0) {
                if (strlen(glockdeps[j].hash) > 0 && strcmp(glockdeps[j].hash, computed_hash) != 0) {
                    fprintf(stderr, "Security Alert: SHA-256 hash mismatch for package '%s'! Expected '%s', got '%s'. Install aborted.\n", 
                            gresolved[i].name, glockdeps[j].hash, computed_hash);
                    return 1;
                }
                break;
            }
        }
        
        char venvpkgdir[512];
        snprintf(venvpkgdir, sizeof(venvpkgdir), ".akspackages/%.127s", gresolved[i].name);
        MkDirp(venvpkgdir);

        char bundle_file[1024];
        snprintf(bundle_file, sizeof(bundle_file), "%.700s/%.127s-%.63s.akspkg",
                 regpkgdir, gresolved[i].name, gresolved[i].version);
        FILE *bfchk = fopen(bundle_file, "rb");
        if (bfchk) {
            fclose(bfchk);
            char bsha[65] = "";
            if (!PkgExtractBundle(bundle_file, venvpkgdir, bsha)) {
                fprintf(stderr, "Security Alert: Corrupted .akspkg bundle '%s'. Install aborted.\n",
                        bundle_file);
                return 1;
            }
        } else {
            char dstmanifest[1024];
            snprintf(dstmanifest, sizeof(dstmanifest), "%s/aks.json", venvpkgdir);
            PkgCopyFile(srcmanifest, dstmanifest);

            char dstentry[1024];
            snprintf(dstentry, sizeof(dstentry), "%s/%s", venvpkgdir, entrypoint);
            PkgCopyFile(srcentry, dstentry);

            char regsrc[1024], venvsrc[1024];
            snprintf(regsrc, sizeof(regsrc), "%.900s/src", regpkgdir);
            snprintf(venvsrc, sizeof(venvsrc), "%s/src", venvpkgdir);
            PkgCopyDirRecursive(regsrc, venvsrc);
        }

        printf("Installed package '%s@%s' (SHA-256: %s) to local environment.\n", gresolved[i].name, gresolved[i].version, computed_hash);
    }
    
    FILE *lock = fopen("aks.lock", "w");
    if (!lock) {
        fprintf(stderr, "Error: Could not create aks.lock\n");
        return 1;
    }
    fprintf(lock, "{\n");
    fprintf(lock, "  \"resolved\": {\n");
    for (int i = 0; i < gresolvedcount; i++) {
        fprintf(lock, "    \"%s\": {\n", gresolved[i].name);
        fprintf(lock, "      \"version\": \"%s\",\n", gresolved[i].version);
        fprintf(lock, "      \"hash\": \"%s\"\n", gresolved[i].hash);
        fprintf(lock, "    }%s\n", (i == gresolvedcount - 1) ? "" : ",");
    }
    fprintf(lock, "  }\n");
    fprintf(lock, "}\n");
    fclose(lock);
    printf("Lockfile 'aks.lock' generated.\n");
    
    printf("Dependencies successfully installed.\n");
    return 0;
}

int PkgRun(int argc, char **argv) {
    char *json = ReadFileContent("aks.json");
    if (!json) {
        fprintf(stderr, "Error: Missing aks.json manifest\n");
        return 1;
    }
    char entrypoint[256];
    if (!JSONGetVal(json, "entrypoint", entrypoint, sizeof(entrypoint))) {
        fprintf(stderr, "Error: Invalid entrypoint configuration\n");
        free(json);
        return 1;
    }
    free(json);
    
    printf("Running package entrypoint '%s'\n", entrypoint);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s run %s", argv[0], entrypoint);
    return system(cmd);
}

// single-file bundler recursive helper
static void BundleFile(FILE *out, const char *path, char bundledfiles[64][256], int *bundledcount) {
    if (!path || *bundledcount >= 64) return;
    // check if already bundled
    for (int i = 0; i < *bundledcount; i++) {
        if (strcmp(bundledfiles[i], path) == 0) return;
    }
    
    // add to list
    strncpy(bundledfiles[*bundledcount], path, 255);
    bundledfiles[*bundledcount][255] = '\0';
    (*bundledcount)++;
    
    char *content = ReadFileContent(path);
    if (!content) return;
    
    fprintf(out, "// Bundled module: %s\n", path);
    
    // basic parser for imports inside the content
    char *p = content;
    while (*p) {
        if (strncmp(p, "import", 6) == 0 && (p[6] == ' ' || p[6] == '\t' || p[6] == '"')) {
            const char *start = p;
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '"') {
                p++;
                char importpath[256] = "";
                int idx = 0;
                while (*p && *p != '"') importpath[idx++] = *p++;
                importpath[idx] = '\0';
                if (*p == '"') p++;
                
                // resolve import path
                char *resolved = PkgResolveImport(importpath, path);
                if (resolved) {
                    BundleFile(out, resolved, bundledfiles, bundledcount);
                    free(resolved);
                }
                
                // comment out the import in the bundled output
                fprintf(out, "// ");
                fwrite(start, 1, p - start, out);
                fprintf(out, "\n");
                continue;
            }
        }
        fputc(*p++, out);
    }
    fprintf(out, "\n// End of module: %s\n\n", path);
    free(content);
}

int PkgBuild(int bundle) {
    char *json = ReadFileContent("aks.json");
    if (!json) {
        fprintf(stderr, "Error: Missing aks.json manifest\n");
        return 1;
    }
    char name[128];
    char entrypoint[256];
    if (!JSONGetVal(json, "name", name, sizeof(name)) ||
        !JSONGetVal(json, "entrypoint", entrypoint, sizeof(entrypoint))) {
        fprintf(stderr, "Error: Invalid aks.json\n");
        free(json);
        return 1;
    }
    free(json);
    
    if (bundle) {
        char outputfile[512];
        snprintf(outputfile, sizeof(outputfile), "dist/%s.aks", name);
        MkDirp("dist");
        
        printf("Bundling package into '%s'\n", outputfile);
        FILE *out = fopen(outputfile, "w");
        if (!out) {
            fprintf(stderr, "Error: Could not open output bundle file\n");
            return 1;
        }
        
        char bundledfiles[64][256];
        int bundledcount = 0;
        BundleFile(out, entrypoint, bundledfiles, &bundledcount);
        fclose(out);
        printf("Bundling successfully completed.\n");
        return 0;
    } else {
        printf("Building package bytecode cache\n");
        // trigger cache creation on entrypoint
        char *cached = PkgGetBytecodeCache(entrypoint);
        if (cached) {
            printf("Package builds successfully. Bytecode cached at '%s'\n", cached);
            free(cached);
        }
        return 0;
    }
}

// virtual Environment import lookup resolution
char *PkgResolveImport(const char *importpath, const char *currentfilepath) {
    // case 1: If import has local relative paths
    if (strncmp(importpath, "./", 2) == 0 || strncmp(importpath, "../", 3) == 0 || strstr(importpath, ".aks")) {
        char *res = malloc(1024);
        if (currentfilepath && currentfilepath[0] != '\0') {
            // resolve relative to current file path
            char dir[512];
            snprintf(dir, sizeof(dir), "%.511s", currentfilepath);
            char *lastsep = strrchr(dir, '/');
            if (!lastsep) lastsep = strrchr(dir, '\\');
            if (lastsep) {
                *lastsep = '\0';
                snprintf(res, 1024, "%s/%s", dir, importpath);
            } else {
                snprintf(res, 1024, "%.1023s", importpath);
            }
        } else {
            snprintf(res, 1024, "%.1023s", importpath);
        }
        return res;
    }
    
    // case 2: Virtual Environment lookup in .akspackages/
    char venvpath[512];
    snprintf(venvpath, sizeof(venvpath), ".akspackages/%s/aks.json", importpath);
    FILE *f = fopen(venvpath, "r");
    if (f) {
        fclose(f);
        char *manifest = ReadFileContent(venvpath);
        if (manifest) {
            char entrypoint[256];
            if (JSONGetVal(manifest, "entrypoint", entrypoint, sizeof(entrypoint))) {
                char *res = malloc(512);
                snprintf(res, 512, ".akspackages/%s/%s", importpath, entrypoint);
                free(manifest);
                return res;
            }
            free(manifest);
        }
    }
    return NULL;
}

// bytecode cache & incremental compilation implementation (.bca archive support)

char *PkgGetBytecodeCache(const char *sourcepath) {
    if (!sourcepath) return NULL;
    char *src = ReadFileContent(sourcepath);
    if (!src) return NULL;

    PKGSHA256CTX sha;
    PkgSha256Init(&sha);
    PkgSha256Update(&sha, (const uint8_t *)src, strlen(src));
    uint8_t digest[32];
    PkgSha256Final(&sha, digest);
    free(src);

    char hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[64] = '\0';

    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), ".aks_cache/%s.bca", hex);

    FILE *f = fopen(cache_path, "rb");
    if (!f) return NULL;

    BcaHeader hdr;
    if (fread(&hdr, 1, sizeof(BcaHeader), f) != sizeof(BcaHeader)) {
        fclose(f);
        return NULL;
    }
    fclose(f);

    if (memcmp(hdr.magic, BCA_MAGIC, 8) != 0 || hdr.version != BCA_VERSION ||
        memcmp(hdr.src_hash, digest, 32) != 0) {
        return NULL;
    }

    return strdup(cache_path);
}

void PkgWriteBytecodeCache(const char *sourcepath, const char *bytecodepath) {
    if (!sourcepath || !bytecodepath) return;
    char *src = ReadFileContent(sourcepath);
    if (!src) return;

    PKGSHA256CTX sha;
    PkgSha256Init(&sha);
    PkgSha256Update(&sha, (const uint8_t *)src, strlen(src));
    uint8_t digest[32];
    PkgSha256Final(&sha, digest);
    free(src);

    char hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[64] = '\0';

    MkDirNative(".aks_cache");

    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), ".aks_cache/%s.bca", hex);

    FILE *in = fopen(bytecodepath, "rb");
    if (!in) return;
    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(in);
        return;
    }

    uint8_t *data = malloc((size_t)sz);
    if (!data) {
        fclose(in);
        return;
    }
    if (fread(data, 1, (size_t)sz, in) != (size_t)sz) {
        fclose(in);
        free(data);
        return;
    }
    fclose(in);

    BcaHeader hdr;
    memcpy(hdr.magic, BCA_MAGIC, 8);
    hdr.version = BCA_VERSION;
    memcpy(hdr.src_hash, digest, 32);
    hdr.timestamp = (uint64_t)time(NULL);
    hdr.data_size = (uint64_t)sz;

    FILE *out = fopen(cache_path, "wb");
    if (out) {
        fwrite(&hdr, 1, sizeof(BcaHeader), out);
        fwrite(data, 1, sz, out);
        fclose(out);
    }
    free(data);
}

int PkgExtractBytecodeCache(const char *cache_path, const char *dest_path) {
    FILE *f = fopen(cache_path, "rb");
    if (!f) return 0;
    BcaHeader hdr;
    if (fread(&hdr, 1, sizeof(BcaHeader), f) != sizeof(BcaHeader)) {
        fclose(f);
        return 0;
    }
    if (memcmp(hdr.magic, BCA_MAGIC, 8) != 0 || hdr.version != BCA_VERSION) {
        fclose(f);
        return 0;
    }
    FILE *out = fopen(dest_path, "wb");
    if (!out) {
        fclose(f);
        return 0;
    }
    char buf[4096];
    size_t remaining = (size_t)hdr.data_size;
    while (remaining > 0) {
        size_t to_read = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        size_t n = fread(buf, 1, to_read, f);
        if (n == 0) break;
        fwrite(buf, 1, n, out);
        remaining -= n;
    }
    fclose(f);
    fclose(out);
    return remaining == 0;
}

int PkgGithubActions(void) {
    printf("Generating GitHub Actions workflow template...\n");
    MkDirp(".github");
    MkDirp(".github/workflows");
    FILE *f = fopen(".github/workflows/aks-ci.yml", "w");
    if (!f) {
        fprintf(stderr, "Error: Could not open .github/workflows/aks-ci.yml for writing\n");
        return 1;
    }
    fprintf(f, "name: AKS CI\n\n");
    fprintf(f, "on:\n");
    fprintf(f, "  push:\n");
    fprintf(f, "    branches: [ main ]\n");
    fprintf(f, "  pull_request:\n");
    fprintf(f, "    branches: [ main ]\n\n");
    fprintf(f, "jobs:\n");
    fprintf(f, "  build-and-test:\n");
    fprintf(f, "    runs-on: ubuntu-latest\n\n");
    fprintf(f, "    steps:\n");
    fprintf(f, "      - name: Checkout repository\n");
    fprintf(f, "        uses: actions/checkout@v4\n\n");
    fprintf(f, "      - name: Set up AKS Runtime\n");
    fprintf(f, "        run: |\n");
    fprintf(f, "          docker pull anoopksharma/aksruntime:latest\n\n");
    fprintf(f, "      - name: Install dependencies\n");
    fprintf(f, "        run: |\n");
    fprintf(f, "          docker run --rm -v ${{ github.workspace }}:/workspace -w /workspace anoopksharma/aksruntime:latest pkg install\n\n");
    fprintf(f, "      - name: Run tests\n");
    fprintf(f, "        run: |\n");
    fprintf(f, "          docker run --rm -v ${{ github.workspace }}:/workspace -w /workspace anoopksharma/aksruntime:latest test src/main.aks\n\n");
    fprintf(f, "      - name: Build release binary\n");
    fprintf(f, "        run: |\n");
    fprintf(f, "          docker run --rm -v ${{ github.workspace }}:/workspace -w /workspace anoopksharma/aksruntime:latest compile-aot src/main.aks dist/app --profile=release\n");
    fclose(f);
    printf("GitHub Actions workflow template saved to '.github/workflows/aks-ci.yml'\n");
    return 0;
}

int PkgDocker(void) {
    printf("Generating Dockerfile for aksruntime:latest...\n");
    FILE *f = fopen("Dockerfile", "w");
    if (!f) {
        fprintf(stderr, "Error: Could not open Dockerfile for writing\n");
        return 1;
    }
    fprintf(f, "# Dockerfile for AKS Runtime (aksruntime:latest)\n");
    fprintf(f, "FROM ubuntu:22.04\n\n");
    fprintf(f, "RUN apt-get update && apt-get install -y \\\n");
    fprintf(f, "    build-essential \\\n");
    fprintf(f, "    clang \\\n");
    fprintf(f, "    curl \\\n");
    fprintf(f, "    git \\\n");
    fprintf(f, "    && rm -rf /var/lib/apt/lists/*\n\n");
    fprintf(f, "# Install AKS compiler and runtime binary\n");
    fprintf(f, "COPY aks /usr/local/bin/aks\n");
    fprintf(f, "COPY aksls /usr/local/bin/aksls\n\n");
    fprintf(f, "WORKDIR /workspace\n\n");
    fprintf(f, "ENTRYPOINT [\"aks\"]\n");
    fclose(f);
    printf("Dockerfile generated successfully.\n");
    return 0;
}

int AKSJSONGetVal(const char *json, const char *key, char *outval, int maxlen) {
    return JSONGetVal(json, key, outval, maxlen);
}

int PkgKeygen(const char *prefix) {
    char *priv_pem = NULL;
    char *pub_pem = NULL;
    int res = ECCGenerateKeys(BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0, 0, 256, &pub_pem, &priv_pem);
    if (!res || !priv_pem || !pub_pem) {
        fprintf(stderr, "Error: Keypair generation failed\n");
        return 1;
    }
    char privpath[256];
    char pubpath[256];
    snprintf(privpath, sizeof(privpath), "%s.private.pem", prefix);
    snprintf(pubpath, sizeof(pubpath), "%s.public.pem", prefix);
    
    FILE *privf = fopen(privpath, "w");
    if (!privf) {
        fprintf(stderr, "Error: Could not write private key to '%s'\n", privpath);
        free(priv_pem);
        free(pub_pem);
        return 1;
    }
    fputs(priv_pem, privf);
    fclose(privf);
    
    FILE *pubf = fopen(pubpath, "w");
    if (!pubf) {
        fprintf(stderr, "Error: Could not write public key to '%s'\n", pubpath);
        free(priv_pem);
        free(pub_pem);
        return 1;
    }
    fputs(pub_pem, pubf);
    fclose(pubf);
    
    free(priv_pem);
    free(pub_pem);
    printf("Generated ECDSA P-256 keypair successfully:\n  Private key: %s\n  Public key:  %s\n", privpath, pubpath);
    return 0;
}

int PkgSign(const char *bundle_path, const char *privkey_path) {
    FILE *f = fopen(bundle_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open bundle '%s'\n", bundle_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return 1; }

    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return 1;
    }
    fclose(f);
    
    long payload_size = size;
    const char *sig_marker = "\n[SIGNATURE]\n";
    for (long i = 0; i < size - 13; i++) {
        if (memcmp(data + i, sig_marker, 13) == 0) {
            payload_size = i;
            break;
        }
    }
    
    PKGSHA256CTX sha_ctx;
    PkgSha256Init(&sha_ctx);
    PkgSha256Update(&sha_ctx, data, payload_size);
    uint8_t digest[32];
    PkgSha256Final(&sha_ctx, digest);
    
    char *priv_pem = ReadFileContent(privkey_path);
    if (!priv_pem) {
        fprintf(stderr, "Error: Could not read private key PEM '%s'\n", privkey_path);
        free(data);
        return 1;
    }
    
    uint8_t sig[1024];
    size_t sig_len = sizeof(sig);
    int res = ECCSignData(BCRYPT_ECDSA_P256_ALGORITHM, NULL, priv_pem, digest, 32, sig, &sig_len);
    free(priv_pem);
    if (!res) {
        fprintf(stderr, "Error: ECDSA P-256 signature generation failed\n");
        free(data);
        return 1;
    }
    
    char sig_hex[512];
    for (size_t i = 0; i < sig_len && (i * 2 + 2) < sizeof(sig_hex); i++) {
        snprintf(sig_hex + i * 2, sizeof(sig_hex) - i * 2, "%02x", sig[i]);
    }
    sig_hex[sig_len * 2 < sizeof(sig_hex) ? sig_len * 2 : sizeof(sig_hex) - 1] = '\0';
    
    FILE *out = fopen(bundle_path, "wb");
    if (!out) {
        fprintf(stderr, "Error: Could not write signature to '%s'\n", bundle_path);
        free(data);
        return 1;
    }
    
    fwrite(data, 1, payload_size, out);
    fprintf(out, "\n[SIGNATURE]\n%s\n", sig_hex);
    fclose(out);
    free(data);
    
    printf("Bytecode bundle '%s' signed successfully.\n", bundle_path);
    return 0;
}

int PkgVerify(const char *bundle_path, const char *pubkey_path) {
    FILE *f = fopen(bundle_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open bundle '%s'\n", bundle_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return 1; }

    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return 1;
    }
    fclose(f);

    if (size >= 8 && memcmp(data, "AKSPKG1\n", 8) == 0 && !pubkey_path) {
        free(data);
        char hex[65] = "";
        if (PkgExtractBundle(bundle_path, NULL, hex)) {
            printf("Package bundle '%s' SHA-256 verified (%s).\n", bundle_path, hex);
            return 0;
        }
        return 1;
    }
    if (!pubkey_path) {
        fprintf(stderr, "Error: Public key PEM required for verifying '%s'\n", bundle_path);
        free(data);
        return 1;
    }
    
    long payload_size = -1;
    const char *sig_marker = "\n[SIGNATURE]\n";
    for (long i = 0; i < size - 13; i++) {
        if (memcmp(data + i, sig_marker, 13) == 0) {
            payload_size = i;
            break;
        }
    }
    
    if (payload_size == -1) {
        fprintf(stderr, "Verification Error: Bundle '%s' is not signed!\n", bundle_path);
        free(data);
        return 1;
    }
    
    char sig_hex[512] = "";
    int sig_idx = 0;
    long start_sig = payload_size + 13;
    while (start_sig < size && data[start_sig] != '\n' && data[start_sig] != '\r' && sig_idx < 511) {
        sig_hex[sig_idx++] = data[start_sig++];
    }
    sig_hex[sig_idx] = '\0';
    
    size_t hex_len = strlen(sig_hex);
    if (hex_len % 2 != 0 || hex_len == 0 || hex_len > 512) {
        fprintf(stderr, "Verification Error: Invalid signature length in bundle '%s'!\n", bundle_path);
        free(data);
        return 1;
    }
    
    size_t sig_len = hex_len / 2;
    uint8_t sig[256];
    for (size_t i = 0; i < sig_len; i++) {
        unsigned int b;
        sscanf(sig_hex + i * 2, "%02x", &b);
        sig[i] = (uint8_t)b;
    }
    
    PKGSHA256CTX sha_ctx;
    PkgSha256Init(&sha_ctx);
    PkgSha256Update(&sha_ctx, data, payload_size);
    uint8_t digest[32];
    PkgSha256Final(&sha_ctx, digest);
    
    char *pub_pem = ReadFileContent(pubkey_path);
    if (!pub_pem) {
        fprintf(stderr, "Error: Could not read public key PEM '%s'\n", pubkey_path);
        free(data);
        return 1;
    }
    
    int res = ECCVerifyData(BCRYPT_ECDSA_P256_ALGORITHM, NULL, pub_pem, digest, 32, sig, sig_len);
    free(pub_pem);
    free(data);
    
    if (res) {
        printf("Bytecode bundle '%s' verified successfully. Signature is VALID.\n", bundle_path);
        return 0;
    } else {
        fprintf(stderr, "Security Alert: Bytecode bundle '%s' signature verification FAILED!\n", bundle_path);
        return 1;
    }
}

int PkgVerifyLock(void) {
    ParseLockfile();
    if (glockdepscount == 0) {
        FILE *f = fopen("aks.lock", "r");
        if (!f) {
            printf("No lockfile 'aks.lock' found in current directory.\n");
            return 0;
        }
        fclose(f);
        printf("Lockfile 'aks.lock' contains 0 dependencies.\n");
        return 0;
    }
    printf("Verifying lockfile 'aks.lock' integrity (%d package%s)...\n",
           glockdepscount, glockdepscount == 1 ? "" : "s");
    int errors = 0;
    for (int i = 0; i < glockdepscount; i++) {
        char pkgmanifest[1024];
        snprintf(pkgmanifest, sizeof(pkgmanifest), ".akspackages\\%.200s\\aks.json", glockdeps[i].name);
        char *json = ReadFileContent(pkgmanifest);
        if (!json) {
            snprintf(pkgmanifest, sizeof(pkgmanifest), ".akspackages/%.200s/aks.json", glockdeps[i].name);
            json = ReadFileContent(pkgmanifest);
        }
        if (!json) {
            fprintf(stderr, "Error: Package '%s' not found in .akspackages directory.\n", glockdeps[i].name);
            errors++;
            continue;
        }
        char entrypoint[256] = "";
        JSONGetVal(json, "entrypoint", entrypoint, sizeof(entrypoint));
        free(json);
        if (!entrypoint[0]) {
            strcpy(entrypoint, "index.aks");
        }
        char pkentry[1024];
        snprintf(pkentry, sizeof(pkentry), ".akspackages\\%.200s\\%.255s", glockdeps[i].name, entrypoint);
        char computed_hash[65] = "";
        if (!Sha256File(pkentry, computed_hash)) {
            snprintf(pkentry, sizeof(pkentry), ".akspackages/%.200s/%.255s", glockdeps[i].name, entrypoint);
            if (!Sha256File(pkentry, computed_hash)) {
                fprintf(stderr, "Error: Could not calculate SHA-256 for entrypoint '%s'\n", pkentry);
                errors++;
                continue;
            }
        }
        if (strlen(glockdeps[i].hash) > 0 && strcmp(glockdeps[i].hash, computed_hash) != 0) {
            fprintf(stderr, "Security Alert: SHA-256 mismatch for '%s'! Expected '%s', got '%s'.\n",
                    glockdeps[i].name, glockdeps[i].hash, computed_hash);
            errors++;
        } else {
            printf("  \033[1;32m[VERIFIED]\033[0m %s@%s (SHA-256: %.16s...)\n",
                   glockdeps[i].name, glockdeps[i].version, computed_hash);
        }
    }
    if (errors == 0) {
        printf("\033[1;32mAll %d dependencies passed integrity verification (0 errors).\033[0m\n", glockdepscount);
        return 0;
    } else {
        fprintf(stderr, "Lockfile verification failed: %d error(s) found.\n", errors);
        return 1;
    }
}

int PkgExportSbom(const char *output_path) {
    char *json = ReadFileContent("aks.json");
    if (!json) {
        fprintf(stderr, "Error: Missing aks.json manifest\n");
        return 1;
    }
    char name[128] = "";
    char version[64] = "";
    JSONGetVal(json, "name", name, sizeof(name));
    JSONGetVal(json, "version", version, sizeof(version));
    free(json);
    
    const char *outpath = output_path ? output_path : "sbom.json";
    ParseLockfile();
    
    FILE *f = fopen(outpath, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not open '%s' for writing SBOM\n", outpath);
        return 1;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "  \"bomFormat\": \"CycloneDX\",\n");
    fprintf(f, "  \"specVersion\": \"1.4\",\n");
    fprintf(f, "  \"metadata\": {\n");
    fprintf(f, "    \"component\": {\n");
    fprintf(f, "      \"name\": \"%s\",\n", name);
    fprintf(f, "      \"version\": \"%s\",\n", version);
    fprintf(f, "      \"type\": \"application\"\n");
    fprintf(f, "    }\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"components\": [\n");
    for (int i = 0; i < glockdepscount; i++) {
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", glockdeps[i].name);
        fprintf(f, "      \"version\": \"%s\",\n", glockdeps[i].version);
        fprintf(f, "      \"hashes\": [\n");
        fprintf(f, "        {\n");
        fprintf(f, "          \"alg\": \"SHA-256\",\n");
        fprintf(f, "          \"content\": \"%s\"\n", glockdeps[i].hash);
        fprintf(f, "        }\n");
        fprintf(f, "      ]\n");
        fprintf(f, "    }%s\n", (i == glockdepscount - 1) ? "" : ",");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("SBOM exported to '%s' successfully.\n", outpath);
    return 0;
}
