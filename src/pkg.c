// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "value.h"
#include "pkg.h"
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
#define MkDirNative(path) _mkdir(path)
#define AccessNative(path, mode) _access(path, mode)
#define PATHSEP '\\'
#else
#include <unistd.h>
#include <sys/stat.h>
#define MkDirNative(path) mkdir(path, 0777)
#define AccessNative(path, mode) access(path, mode)
#define PATHSEP '/'
#define LPCWSTR const wchar_t*
#define DWORD unsigned long
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
    snprintf(g_registrydir, sizeof(g_registrydir), "%s\\.aksregistry", home);
    return g_registrydir;
}

const char *gcompiletarget = "host";

// helper to create directory recursively
static int MkDirp(const char *path) {
    char temp[512];
    snprintf(temp, sizeof(temp), "%s", path);
    int len = (int)strlen(temp);
    if (len == 0) return 0;
    
    // trim trailing separators
    if (temp[len - 1] == PATHSEP) {
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

// semVer parser
typedef struct {
    int major;
    int minor;
    int patch;
} SemVer;

static int ParseSemver(const char *str, SemVer *ver) {
    ver->major = ver->minor = ver->patch = 0;
    return sscanf(str, "%d.%d.%d", &ver->major, &ver->minor, &ver->patch) >= 1;
}

// version match check
static int VersionMatch(SemVer val, const char *constraint) {
    if (strcmp(constraint, "*") == 0 || strlen(constraint) == 0) return 1;
    
    // parse constraint operator
    const char *p = constraint;
    char op[8] = "";
    int opidx = 0;
    while (*p && (*p == '=' || *p == '>' || *p == '<' || *p == '^' || *p == '~')) {
        if (opidx < 7) op[opidx++] = *p;
        p++;
    }
    op[opidx] = '\0';
    
    SemVer target;
    if (!ParseSemver(p, &target)) return 0;
    
    if (strcmp(op, "==") == 0 || strcmp(op, "") == 0) {
        return (val.major == target.major && val.minor == target.minor && val.patch == target.patch);
    } else if (strcmp(op, ">=") == 0) {
        if (val.major != target.major) return val.major > target.major;
        if (val.minor != target.minor) return val.minor > target.minor;
        return val.patch >= target.patch;
    } else if (strcmp(op, "<=") == 0) {
        if (val.major != target.major) return val.major < target.major;
        if (val.minor != target.minor) return val.minor < target.minor;
        return val.patch <= target.patch;
    } else if (strcmp(op, ">") == 0) {
        if (val.major != target.major) return val.major > target.major;
        if (val.minor != target.minor) return val.minor > target.minor;
        return val.patch > target.patch;
    } else if (strcmp(op, "<") == 0) {
        if (val.major != target.major) return val.major < target.major;
        if (val.minor != target.minor) return val.minor < target.minor;
        return val.patch < target.patch;
    } else if (strcmp(op, "^") == 0) {
        // compatible with target (same major version, >= minor/patch)
        if (val.major != target.major) return 0;
        if (val.minor != target.minor) return val.minor > target.minor;
        return val.patch >= target.patch;
    } else if (strcmp(op, "~") == 0) {
        // compatible with target (same major and minor, >= patch)
        if (val.major != target.major || val.minor != target.minor) return 0;
        return val.patch >= target.patch;
    }
    return 0;
}

// file reading helper
static char *ReadFileContent(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t readbytes = fread(buf, 1, sz, f);
    buf[readbytes] = '\0';
    fclose(f);
    return buf;
}

// package manager commands
int PkgInit(const char *name) {
    printf("Initializing package '%s'\n", name);
    
    char srcdir[512];
    snprintf(srcdir, sizeof(srcdir), "%s\\src", name);
    MkDirp(srcdir);
    
    char manifestpath[512];
    snprintf(manifestpath, sizeof(manifestpath), "%s\\aks.json", name);
    
    FILE *f = fopen(manifestpath, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not create %s\n", manifestpath);
        return 1;
    }
    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", name);
    fprintf(f, "  \"version\": \"1.0.0\",\n");
    fprintf(f, "  \"entrypoint\": \"src/main.aks\",\n");
    fprintf(f, "  \"dependencies\": {}\n");
    fprintf(f, "}\n");
    fclose(f);
    
    char mainpath[512];
    snprintf(mainpath, sizeof(mainpath), "%s\\src\\main.aks", name);
    FILE *mainf = fopen(mainpath, "w");
    if (mainf) {
        fprintf(mainf, "// Main entrypoint for package %s\n", name);
        fprintf(mainf, "print(\"Hello from %s!\");\n", name);
        fclose(mainf);
    }
    
    printf("Package initialized successfully.\n");
    return 0;
}

int PkgPublish(void) {
    char *json = ReadFileContent("aks.json");
    if (!json) {
        fprintf(stderr, "Error: Missing aks.json manifest\n");
        return 1;
    }
    
    char name[128];
    char version[64];
    char entrypoint[256];
    
    if (!JSONGetVal(json, "name", name, sizeof(name)) ||
        !JSONGetVal(json, "version", version, sizeof(version)) ||
        !JSONGetVal(json, "entrypoint", entrypoint, sizeof(entrypoint))) {
        fprintf(stderr, "Error: Invalid aks.json manifest structure\n");
        free(json);
        return 1;
    }
    free(json);
    
    printf("Publishing package '%s@%s' to registry\n", name, version);
    
    char destdir[512];
    snprintf(destdir, sizeof(destdir), "%s\\%s\\%s", GetRegIsTryDir(), name, version);
    MkDirp(destdir);
    
    // copy manifest
    char dstmanifest[512];
    snprintf(dstmanifest, sizeof(dstmanifest), "%s\\aks.json", destdir);
    PkgCopyFile("aks.json", dstmanifest);
    
    // copy entrypoint and src files (basic file copier for package files)
    char dstentry[512];
    snprintf(dstentry, sizeof(dstentry), "%s\\%s", destdir, entrypoint);
    PkgCopyFile(entrypoint, dstentry);
    
    printf("Package published successfully.\n");
    return 0;
}

int PkgAdd(const char *pkgspec) {
    char pkgname[128] = "";
    char pkgversion[128] = "*";
    
    const char *at = strchr(pkgspec, '@');
    if (at) {
        int namelen = (int)(at - pkgspec);
        if (namelen > 127) namelen = 127;
        strncpy(pkgname, pkgspec, namelen);
        pkgname[namelen] = '\0';
        strncpy(pkgversion, at + 1, sizeof(pkgversion) - 1);
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
    
    char regpkgdir[512];
    snprintf(regpkgdir, sizeof(regpkgdir), "%s\\%s", GetRegIsTryDir(), name);
    
    char foundversion[64] = "";
    SemVer bestver = {-1, -1, -1};
    
    char searchpat[512];
    snprintf(searchpat, sizeof(searchpat), "%s\\*", regpkgdir);
    WIN32_FIND_DATAA ffd;
    HANDLE hfind = FindFirstFileA(searchpat, &ffd);
    if (hfind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
            char chkpath[512];
            snprintf(chkpath, sizeof(chkpath), "%s\\%s\\aks.json", regpkgdir, ffd.cFileName);
            FILE *tf = fopen(chkpath, "r");
            if (!tf) continue;
            fclose(tf);
            SemVer parsed;
            if (!ParseSemver(ffd.cFileName, &parsed)) continue;
            if (!VersionMatch(parsed, constraint)) continue;
            int better = (bestver.major < 0) ||
                         (parsed.major > bestver.major) ||
                         (parsed.major == bestver.major && parsed.minor > bestver.minor) ||
                         (parsed.major == bestver.major && parsed.minor == bestver.minor && parsed.patch > bestver.patch);
            if (better) {
                bestver = parsed;
                snprintf(foundversion, sizeof(foundversion), "%s", ffd.cFileName);
            }
        } while (FindNextFileA(hfind, &ffd));
        FindClose(hfind);
    }
    
    if (strlen(foundversion) == 0) {
        fprintf(stderr, "Dependency Error: Could not find version matching constraint '%s' for package '%s' in registry '%s'\n", constraint, name, regpkgdir);
        return 0;
    }
    
    strcpy(gresolved[gresolvedcount].name, name);
    strcpy(gresolved[gresolvedcount].version, foundversion);
    gresolvedcount++;
    
    char chkpath[512];
    snprintf(chkpath, sizeof(chkpath), "%s\\%s\\aks.json", regpkgdir, foundversion);
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
                }
            }
            p++;
        }
    }
    free(json);
    
    MkDirp(".akspackages");
    for (int i = 0; i < gresolvedcount; i++) {
        char regpkgdir[512];
        snprintf(regpkgdir, sizeof(regpkgdir), "%s\\%s\\%s", GetRegIsTryDir(), gresolved[i].name, gresolved[i].version);
        
        char srcmanifest[512];
        snprintf(srcmanifest, sizeof(srcmanifest), "%s\\aks.json", regpkgdir);
        
        char *venvmanifest = ReadFileContent(srcmanifest);
        if (!venvmanifest) {
            fprintf(stderr, "Error: Resolved package registry manifest not found at '%s'\n", srcmanifest);
            return 1;
        }
        
        char entrypoint[256] = "";
        JSONGetVal(venvmanifest, "entrypoint", entrypoint, sizeof(entrypoint));
        free(venvmanifest);
        
        char srcentry[512];
        snprintf(srcentry, sizeof(srcentry), "%s\\%s", regpkgdir, entrypoint);
        
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
        snprintf(venvpkgdir, sizeof(venvpkgdir), ".akspackages\\%s", gresolved[i].name);
        MkDirp(venvpkgdir);
        
        char dstmanifest[512];
        snprintf(dstmanifest, sizeof(dstmanifest), "%s\\aks.json", venvpkgdir);
        PkgCopyFile(srcmanifest, dstmanifest);
        
        char dstentry[512];
        snprintf(dstentry, sizeof(dstentry), "%s\\%s", venvpkgdir, entrypoint);
        PkgCopyFile(srcentry, dstentry);
        
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
        snprintf(outputfile, sizeof(outputfile), "dist\\%s.aks", name);
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
        char *res = malloc(512);
        if (currentfilepath && currentfilepath[0] != '\0') {
            // resolve relative to current file path
            char dir[512];
            snprintf(dir, sizeof(dir), "%s", currentfilepath);
            char *lastsep = strrchr(dir, '/');
            if (!lastsep) lastsep = strrchr(dir, '\\');
            if (lastsep) {
                *lastsep = '\0';
                snprintf(res, 512, "%s/%s", dir, importpath);
            } else {
                snprintf(res, 512, "%s", importpath);
            }
        } else {
            snprintf(res, 512, "%s", importpath);
        }
        return res;
    }
    
    // case 2: Virtual Environment lookup in .akspackages/
    char venvpath[512];
    snprintf(venvpath, sizeof(venvpath), ".akspackages\\%s\\aks.json", importpath);
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

    uint8_t *data = malloc(sz);
    if (!data) {
        fclose(in);
        return;
    }
    fread(data, 1, sz, in);
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
    
    uint8_t *data = malloc(size);
    if (fread(data, 1, size, f) != size) {
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
    
    uint8_t *data = malloc(size);
    if (fread(data, 1, size, f) != size) {
        fclose(f);
        free(data);
        return 1;
    }
    fclose(f);
    
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
        char pkgmanifest[512];
        snprintf(pkgmanifest, sizeof(pkgmanifest), ".akspackages\\%s\\aks.json", glockdeps[i].name);
        char *json = ReadFileContent(pkgmanifest);
        if (!json) {
            snprintf(pkgmanifest, sizeof(pkgmanifest), ".akspackages/%s/aks.json", glockdeps[i].name);
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
        char pkentry[512];
        snprintf(pkentry, sizeof(pkentry), ".akspackages\\%s\\%s", glockdeps[i].name, entrypoint);
        char computed_hash[65] = "";
        if (!Sha256File(pkentry, computed_hash)) {
            snprintf(pkentry, sizeof(pkentry), ".akspackages/%s/%s", glockdeps[i].name, entrypoint);
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
