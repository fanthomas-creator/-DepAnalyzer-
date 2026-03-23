#ifndef PARSER_CS_H
#define PARSER_CS_H

#include "types.h"

/* Parse C# source files.
 * Detects:
 *   using System.Collections.Generic  → external (System.*)
 *   using Microsoft.AspNetCore.Mvc    → external
 *   using MyApp.Models                → internal (project namespace)
 *   namespace MyApp.Controllers       → namespace declaration
 *   class / interface / struct / enum / record → FunctionIndex
 *   abstract class / sealed class     → FunctionIndex
 *   void Method() / async Task Func() → FunctionIndex
 *   [Attribute]                        → dependency hint
 *   : BaseClass, IInterface            → dependency
 *   obj.Method() / Class.Static()      → calls
 */
void parser_cs_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_CS_H */
