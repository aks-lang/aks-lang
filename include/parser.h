// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
#ifndef PARSERH
#define PARSERH

struct Node; // forward declare Node from ast.h

struct Node *Parse(const char *source, int *haderror);
struct Node *ParseExpr(const char *source, int *haderror);

#endif // Parserh
