// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#include "direct_linker.h"
#include "ast.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "opcode.h"
extern const char *AKSPRELUDE;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

extern int VMLoadBuffer(VM *vm, const uint8_t *buffer, size_t size);

int CheckAndRunEmbeddedPayload(int argc, char **argv) {
    char exepath[1024];
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, exepath, sizeof(exepath)) == 0) {
        return -999;
    }
#else
    ssize_t len = readlink("/proc/self/exe", exepath, sizeof(exepath) - 1);
    if (len > 0) {
        exepath[len] = '\0';
    } else if (argc > 0 && argv && argv[0]) {
        strncpy(exepath, argv[0], sizeof(exepath) - 1);
        exepath[sizeof(exepath) - 1] = '\0';
    } else {
        return -999;
    }
#endif

    FILE *f = fopen(exepath, "rb");
    if (!f) return -999;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -999;
    }
    long filesize = ftell(f);
    if (filesize < (long)(sizeof(AksEmbedTrailer) + 32)) {
        fclose(f);
        return -999;
    }

    if (fseek(f, filesize - (long)sizeof(AksEmbedTrailer), SEEK_SET) != 0) {
        fclose(f);
        return -999;
    }

    AksEmbedTrailer trailer;
    if (fread(&trailer, sizeof(trailer), 1, f) != 1) {
        fclose(f);
        return -999;
    }

    if (trailer.magic != AKS_EMBED_MAGIC) {
        fclose(f);
        return -999;
    }

    if (trailer.payload_size == 0 || trailer.payload_size > (uint64_t)(filesize - sizeof(AksEmbedTrailer))) {
        fclose(f);
        return -999;
    }

    long payload_offset = filesize - (long)sizeof(AksEmbedTrailer) - (long)trailer.payload_size;
    if (fseek(f, payload_offset, SEEK_SET) != 0) {
        fclose(f);
        return -999;
    }

    uint8_t *payload = (uint8_t*)malloc((size_t)trailer.payload_size);
    if (!payload) {
        fclose(f);
        fprintf(stderr, "Fatal error: out of memory loading embedded application payload\n");
        return 1;
    }

    if (fread(payload, 1, (size_t)trailer.payload_size, f) != (size_t)trailer.payload_size) {
        free(payload);
        fclose(f);
        fprintf(stderr, "Fatal error: failed to read embedded application payload\n");
        return 1;
    }
    fclose(f);

    VM *aksVM = VMNew();
    aksVM->jitmode = 1;
    if (!VMLoadBuffer(aksVM, payload, (size_t)trailer.payload_size)) {
        free(payload);
        VMFree(aksVM);
        fprintf(stderr, "Fatal error: corrupt embedded bytecode payload\n");
        return 1;
    }
    free(payload);

    int vmresult = VMRun(aksVM);
    VMFree(aksVM);
    return vmresult ? 0 : 1;
}

static char *ReadFileContent(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    if (out_size) *out_size = rd;
    return buf;
}

int CreateDirectExecutable(const char *sourcepath, const char *outputpath) {
    size_t sourcesize = 0;
    char *rawsource = ReadFileContent(sourcepath, &sourcesize);
    if (!rawsource) {
        fprintf(stderr, "Error: Could not read source file '%s'\n", sourcepath);
        return 0;
    }

    extern int gImmutableByDefault, gTypeCheckingEnabled, gBorrowCheckerEnabled;
    int savedImmutable = gImmutableByDefault;
    int savedType = gTypeCheckingEnabled;
    int savedBorrow = gBorrowCheckerEnabled;
    gImmutableByDefault = 0;
    gTypeCheckingEnabled = 0;
    gBorrowCheckerEnabled = 0;

    Compiler *aksCompiler = CompNew();
    int parseError = 0;
    Node *preludeAst = Parse(AKSPRELUDE, &parseError);
    if (parseError || !preludeAst) {
        fprintf(stderr, "Compiler error: Prelude syntax analysis failed.\n");
        if (preludeAst) NodeFree(preludeAst);
        CompFree(aksCompiler);
        free(rawsource);
        gImmutableByDefault = savedImmutable;
        gTypeCheckingEnabled = savedType;
        gBorrowCheckerEnabled = savedBorrow;
        return 0;
    }
    if (!CompCompile(aksCompiler, preludeAst)) {
        fprintf(stderr, "Compiler error: Prelude compilation failed.\n");
        NodeFree(preludeAst);
        CompFree(aksCompiler);
        free(rawsource);
        gImmutableByDefault = savedImmutable;
        gTypeCheckingEnabled = savedType;
        gBorrowCheckerEnabled = savedBorrow;
        return 0;
    }
    NodeFree(preludeAst);

    if (aksCompiler->fns[0]->chunk.count > 0 &&
        aksCompiler->fns[0]->chunk.code[aksCompiler->fns[0]->chunk.count - 1] == OPHALT) {
        aksCompiler->fns[0]->chunk.count--;
    }

    gImmutableByDefault = savedImmutable;
    gTypeCheckingEnabled = savedType;
    gBorrowCheckerEnabled = savedBorrow;

    Node *programast = Parse(rawsource, &parseError);
    free(rawsource);
    if (parseError || !programast) {
        fprintf(stderr, "Compiler error: Syntax analysis failed for '%s'.\n", sourcepath);
        if (programast) NodeFree(programast);
        CompFree(aksCompiler);
        return 0;
    }

    if (!CompCompile(aksCompiler, programast)) {
        fprintf(stderr, "Compiler error: Code generation failed for '%s'.\n", sourcepath);
        NodeFree(programast);
        CompFree(aksCompiler);
        return 0;
    }
    NodeFree(programast);

    char tempbcpath[1024];
    snprintf(tempbcpath, sizeof(tempbcpath), "%s.tmpbc", outputpath);
    if (!CompWrt(aksCompiler, tempbcpath)) {
        fprintf(stderr, "Compiler error: Failed to serialize bytecode chunk to '%s'.\n", tempbcpath);
        CompFree(aksCompiler);
        return 0;
    }
    CompFree(aksCompiler);

    size_t bc_size = 0;
    char *bc_data = ReadFileContent(tempbcpath, &bc_size);
    remove(tempbcpath);
    if (!bc_data || bc_size == 0) {
        fprintf(stderr, "Compiler error: Could not read serialized bytecode chunk.\n");
        if (bc_data) free(bc_data);
        return 0;
    }

    char hostpath[1024];
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, hostpath, sizeof(hostpath)) == 0) {
        strncpy(hostpath, "aks.exe", sizeof(hostpath) - 1);
        hostpath[sizeof(hostpath) - 1] = '\0';
    }
#else
    ssize_t hlen = readlink("/proc/self/exe", hostpath, sizeof(hostpath) - 1);
    if (hlen > 0) hostpath[hlen] = '\0';
    else strncpy(hostpath, "aks", sizeof(hostpath) - 1);
#endif

    size_t host_size = 0;
    char *host_data = ReadFileContent(hostpath, &host_size);
    if (!host_data || host_size == 0) {
        fprintf(stderr, "Compiler error: Could not read host runner binary '%s'.\n", hostpath);
        free(bc_data);
        if (host_data) free(host_data);
        return 0;
    }

    /* Check if host_data already contains an embedded trailer; if so, truncate it */
    if (host_size > sizeof(AksEmbedTrailer)) {
        AksEmbedTrailer *ex_trailer = (AksEmbedTrailer*)(host_data + host_size - sizeof(AksEmbedTrailer));
        if (ex_trailer->magic == AKS_EMBED_MAGIC && ex_trailer->payload_size < host_size - sizeof(AksEmbedTrailer)) {
            host_size = host_size - sizeof(AksEmbedTrailer) - (size_t)ex_trailer->payload_size;
        }
    }

    FILE *outf = fopen(outputpath, "wb");
    if (!outf) {
        fprintf(stderr, "Compiler error: Could not create output standalone executable '%s'.\n", outputpath);
        free(bc_data);
        free(host_data);
        return 0;
    }

    /* Write host PE/ELF binary image */
    if (fwrite(host_data, 1, host_size, outf) != host_size) {
        fprintf(stderr, "Compiler error: Failed to write host image to '%s'.\n", outputpath);
        fclose(outf);
        free(bc_data);
        free(host_data);
        return 0;
    }
    free(host_data);

    /* Write compiled AKS bytecode payload */
    if (fwrite(bc_data, 1, bc_size, outf) != bc_size) {
        fprintf(stderr, "Compiler error: Failed to write bytecode payload to '%s'.\n", outputpath);
        fclose(outf);
        free(bc_data);
        return 0;
    }
    free(bc_data);

    /* Write 16-byte trailer: payload_size + magic */
    AksEmbedTrailer trailer;
    trailer.payload_size = (uint64_t)bc_size;
    trailer.magic = AKS_EMBED_MAGIC;
    if (fwrite(&trailer, sizeof(trailer), 1, outf) != 1) {
        fprintf(stderr, "Compiler error: Failed to write embed trailer to '%s'.\n", outputpath);
        fclose(outf);
        return 0;
    }
    fclose(outf);

#ifndef _WIN32
    chmod(outputpath, 0755);
#endif

    printf("Standalone native executable generated successfully: '%s' (%zu KB, 0 external dependencies)\n",
           outputpath, (host_size + bc_size + sizeof(trailer)) / 1024);
    return 1;
}
