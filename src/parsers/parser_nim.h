#ifndef PARSER_NIM_H
#define PARSER_NIM_H

#include "types.h"

/* Parse Nim source files (.nim, .nims).
 * Detects:
 *   import strutils, sequtils         → external (stdlib)
 *   import ./utils                    → internal
 *   import myapp/models               → internal (slash = path)
 *   from strutils import split, join  → external
 *   include ./common                  → internal
 *   proc myProc(args: Type): RetType  → FunctionIndex
 *   func myFunc(args): RetType        → FunctionIndex
 *   method myMethod(args): RetType    → FunctionIndex
 *   iterator myIter(args): Type       → FunctionIndex
 *   macro myMacro(args): untyped      → FunctionIndex
 *   template myTemplate(args): untyped → FunctionIndex
 *   type MyType = object              → FunctionIndex
 *   type MyEnum = enum                → FunctionIndex
 *   const MY_CONST =                  → FunctionIndex
 *   obj.method() / Module.func()      → calls
 */
void parser_nim_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_NIM_H */
