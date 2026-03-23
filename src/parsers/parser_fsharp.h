#ifndef PARSER_FSHARP_H
#define PARSER_FSHARP_H

#include "types.h"

/* Parse F# source files (.fs, .fsx, .fsi).
 * Detects:
 *   open System.Collections.Generic   → external dep "System"
 *   open MyApp.Models                 → internal/external dep
 *   #load "./utils.fsx"               → internal (script)
 *   #r "nuget: Newtonsoft.Json"        → external NuGet
 *   #r "./bin/MyLib.dll"              → internal ref
 *   module MyModule =                 → FunctionIndex
 *   let myFunc args =                 → FunctionIndex
 *   let rec myFunc args =             → FunctionIndex
 *   type MyType =                     → FunctionIndex
 *   exception MyExn                   → FunctionIndex
 *   [<Attribute>]                     → dep hint
 *   interface IFoo with               → dep
 *   inherit BaseClass()               → dep
 */
void parser_fsharp_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_FSHARP_H */
