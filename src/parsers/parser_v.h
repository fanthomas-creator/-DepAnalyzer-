#ifndef PARSER_V_H
#define PARSER_V_H

#include "types.h"

/* Parse V (Vlang) source files (.v, .vv).
 * Detects:
 *   import os                          → external (stdlib)
 *   import net.http                    → external
 *   import myapp.utils                 → internal (dot = path sep)
 *   import myapp.models { User, Post } → internal + selective
 *   #include <stdio.h>                 → external C dep
 *   fn my_func(args Type) RetType      → FunctionIndex
 *   pub fn my_func(args) RetType       → FunctionIndex
 *   fn (r Receiver) method(args) RetType → FunctionIndex (method)
 *   struct MyStruct { }                → FunctionIndex
 *   interface MyInterface { }          → FunctionIndex
 *   enum MyEnum { }                    → FunctionIndex
 *   type MyAlias = ExistingType        → FunctionIndex
 *   const my_const = value             → FunctionIndex
 *   obj.method() / mod.func()          → calls
 */
void parser_v_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_V_H */
