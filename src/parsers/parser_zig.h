#ifndef PARSER_ZIG_H
#define PARSER_ZIG_H

#include "types.h"

/* Parse Zig source files (.zig).
 * Detects:
 *   const std = @import("std")         → external "std"
 *   const io = @import("std").io       → external "std"
 *   const utils = @import("./utils.zig") → internal
 *   const lib = @import("../lib/foo.zig") → internal
 *   pub fn myFunc(args: Type) RetType  → FunctionIndex
 *   fn myFunc(args: Type) RetType      → FunctionIndex
 *   pub const MY_CONST = value         → FunctionIndex
 *   const MyStruct = struct { }        → FunctionIndex
 *   const MyUnion = union { }          → FunctionIndex
 *   const MyEnum = enum { }            → FunctionIndex
 *   const MyError = error { }          → FunctionIndex
 *   obj.method() / Struct.staticFn()   → calls
 *   @import / @cImport / @cInclude     → C dependency
 */
void parser_zig_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_ZIG_H */
