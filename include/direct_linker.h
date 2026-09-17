// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef DIRECT_LINKER_H
#define DIRECT_LINKER_H

#include <stdint.h>
#include <stddef.h>

#define AKS_EMBED_MAGIC 0x414B53454D424544ULL /* "AKSEMBED" */

#pragma pack(push, 1)
typedef struct {
    uint64_t payload_size;
    uint64_t magic;
} AksEmbedTrailer;
#pragma pack(pop)

/* Check if the currently executing binary contains an embedded AKS bytecode payload.
 * If yes, executes it immediately and returns the program exit code (0 or 1).
 * If no (running as standard compiler tool), returns -999. */
int CheckAndRunEmbeddedPayload(int argc, char **argv);

/* Compiles the given .aks source file and packages it directly with the host executable
 * into a self-contained, standalone native executable without invoking external compilers.
 * Returns 1 on success, 0 on failure. */
int CreateDirectExecutable(const char *sourcepath, const char *outputpath);

#endif /* DIRECT_LINKER_H */
