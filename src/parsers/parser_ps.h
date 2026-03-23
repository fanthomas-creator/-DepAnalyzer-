#ifndef PARSER_PS_H
#define PARSER_PS_H
#include "types.h"
/* Parse PowerShell (.ps1, .psm1, .psd1).
 * . ./lib.ps1 / . "$PSScriptRoot/utils.ps1"  → internal
 * & ./script.ps1                              → internal
 * Import-Module ActiveDirectory               → external
 * Import-Module ./MyModule.psm1               → internal
 * using module ./MyModule                     → internal
 * using namespace System.Collections          → external
 * #Requires -Module Az                        → external
 * function My-Function { }                    → FunctionIndex
 * filter My-Filter { }                        → FunctionIndex
 * class MyClass { }                           → FunctionIndex
 */
void parser_ps_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
