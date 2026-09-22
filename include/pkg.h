// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef PKGH
#define PKGH

#include "compiler.h"

// Cli package manager commands
int PkgInit(const char *name);
int PkgAdd(const char *pkgspec);
int PkgInstall(void);
int PkgRun(int argc, char **argv);
int PkgPublish(void);
int PkgBuild(int bundle);
int PkgGithubActions(void);
int PkgDocker(void);

// Advanced supply-chain security commands
int PkgKeygen(const char *prefix);
int PkgSign(const char *bundle_path, const char *privkey_path);
int PkgVerify(const char *bundle_path, const char *pubkey_path);
int PkgVerifyLock(void);
int PkgExportSbom(const char *output_path);

// virtual environment & isolated package resolution
char *PkgResolveImport(const char *importpath, const char *currentfilepath);
int PkgCopyFile(const char *src, const char *dst);

// bytecode cache & incremental compilation helpers
#define BCA_MAGIC "AKSBCA01"
#define BCA_VERSION 1

typedef struct {
    char magic[8];        /* "AKSBCA01" */
    uint32_t version;     /* Format version (1) */
    uint8_t src_hash[32]; /* SHA-256 hash of source code */
    uint64_t timestamp;   /* Build timestamp */
    uint64_t data_size;   /* Payload length */
} BcaHeader;

char *PkgGetBytecodeCache(const char *sourcepath);
void PkgWriteBytecodeCache(const char *sourcepath, const char *bytecodepath);
int PkgExtractBytecodeCache(const char *cache_path, const char *dest_path);

// JSON key extraction helper
int AKSJSONGetVal(const char *json, const char *key, char *outval, int maxlen);

// cross compilation target settings
extern const char *gcompiletarget;

#endif // Pkgh
