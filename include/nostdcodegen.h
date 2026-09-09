// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Freestanding no_std bare-metal and firmware C code generator.
#ifndef NOSTDCODEGEN_H
#define NOSTDCODEGEN_H

#include <stdio.h>
#include <stdint.h>
#include "compiler.h"
#include "ast.h"

int EmitNoStdCModule(Compiler *comp, Node *ast_root, const char *out_filepath);

#endif // NOSTDCODEGEN_H
