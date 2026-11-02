/*
 * AKS Self-Contained Installer Packager
 * Packs distribution binaries and assets into a single standalone aks-setup.exe
 * Copyright (c) 2026 AKS Language Authors. All Rights Reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <io.h>

#define AKS_MAGIC "AKSPKG10"
#define MAX_ENTRIES 256

#pragma pack(push, 1)
typedef struct {
    char rel_path[260];
    uint64_t file_offset;
    uint64_t file_size;
} AksPkgFileEntry;

typedef struct {
    uint32_t file_count;
    uint64_t toc_offset;
    char magic[8];
} AksPkgTrailer;
#pragma pack(pop)

static AksPkgFileEntry g_entries[MAX_ENTRIES];
static uint32_t g_entry_count = 0;

static bool AppendFileToInstaller(FILE *out_f, const char *src_path, const char *rel_path) {
    if (_access(src_path, 0) != 0) {
        printf("  [WARN] Source file not found, skipping: %s\n", src_path);
        return false;
    }

    FILE *in_f = fopen(src_path, "rb");
    if (!in_f) {
        printf("  [ERROR] Cannot open source file: %s\n", src_path);
        return false;
    }

    _fseeki64(in_f, 0, SEEK_END);
    int64_t fsize = _ftelli64(in_f);
    _fseeki64(in_f, 0, SEEK_SET);

    _fseeki64(out_f, 0, SEEK_END);
    int64_t offset = _ftelli64(out_f);

    char buf[65536];
    int64_t remaining = fsize;
    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : (size_t)remaining;
        size_t n = fread(buf, 1, chunk, in_f);
        if (n == 0) break;
        fwrite(buf, 1, n, out_f);
        remaining -= n;
    }
    fclose(in_f);

    strncpy(g_entries[g_entry_count].rel_path, rel_path, sizeof(g_entries[g_entry_count].rel_path) - 1);
    g_entries[g_entry_count].file_offset = (uint64_t)offset;
    g_entries[g_entry_count].file_size = (uint64_t)fsize;
    g_entry_count++;

    printf("  [+] Packed '%s' (%lld bytes)\n", rel_path, (long long)fsize);
    return true;
}

int main(int argc, char **argv) {
    const char *stub_path = "gui_installer_stub.exe";
    const char *out_path = "aks-installer.exe";

    if (argc >= 2) stub_path = argv[1];
    if (argc >= 3) out_path = argv[2];

    printf("Building Standalone AKS GUI Installer: %s\n", out_path);

    FILE *src_stub = fopen(stub_path, "rb");
    if (!src_stub) {
        printf("Cannot open installer stub '%s'\n", stub_path);
        return 1;
    }

    FILE *out_f = fopen(out_path, "wb");
    if (!out_f) {
        printf("Cannot create output installer '%s'\n", out_path);
        fclose(src_stub);
        return 1;
    }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src_stub)) > 0) {
        fwrite(buf, 1, n, out_f);
    }
    fclose(src_stub);

    // Binaries
    AppendFileToInstaller(out_f, "aks.exe", "bin\\aks.exe");
    AppendFileToInstaller(out_f, "aksls.exe", "bin\\aksls.exe");
    AppendFileToInstaller(out_f, "aksfmt.exe", "bin\\aksfmt.exe");
    AppendFileToInstaller(out_f, "akslint.exe", "bin\\akslint.exe");
    AppendFileToInstaller(out_f, "aks.dll", "bin\\aks.dll");

    // C Headers
    const char *headers[] = {
        "aks.h", "aks_plat.h", "ast.h", "compiler.h", "vm.h", "value.h",
        "learning.h", "diagnostic.h", "opcode.h", "typer.h", "staticcodegen.h"
    };
    for (size_t i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
        char src[256];
        char rel[256];
        snprintf(src, sizeof(src), "include\\%s", headers[i]);
        snprintf(rel, sizeof(rel), "include\\%s", headers[i]);
        AppendFileToInstaller(out_f, src, rel);
    }

    // VS Code Extension
    AppendFileToInstaller(out_f, "editors\\vscode\\aks-lang-1.0.0.vsix", "editors\\vscode\\aks-lang-1.0.0.vsix");

    // Documentation
    AppendFileToInstaller(out_f, "README.md", "README.md");
    AppendFileToInstaller(out_f, "LICENSE", "LICENSE");

    // Write TOC (Table of Contents)
    _fseeki64(out_f, 0, SEEK_END);
    int64_t toc_offset = _ftelli64(out_f);

    for (uint32_t i = 0; i < g_entry_count; i++) {
        fwrite(&g_entries[i], sizeof(AksPkgFileEntry), 1, out_f);
    }

    // Write Trailer
    AksPkgTrailer trailer;
    trailer.file_count = g_entry_count;
    trailer.toc_offset = (uint64_t)toc_offset;
    memcpy(trailer.magic, AKS_MAGIC, 8);
    fwrite(&trailer, sizeof(trailer), 1, out_f);

    _fseeki64(out_f, 0, SEEK_END);
    int64_t total_size = _ftelli64(out_f);
    fclose(out_f);

    printf("Successfully created '%s' (%u components, %.2f MB)\n",
           out_path, g_entry_count, (double)total_size / (1024.0 * 1024.0));

    return 0;
}
