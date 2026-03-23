#ifndef PARSER_GO_H
#define PARSER_GO_H

#include "types.h"

/* Parse Go source files.
 * Detects:
 *   import "fmt"                     → external
 *   import "github.com/gin-gonic/gin"→ external (module path)
 *   import "./utils"                 → internal
 *   import ( "pkg1" \n "pkg2" )      → multi-line import block
 *   func MyFunc(...)                 → FunctionIndex
 *   func (r *Receiver) Method(...)   → FunctionIndex
 *   type MyStruct struct             → FunctionIndex
 *   type MyInterface interface       → FunctionIndex
 *   pkg.Function()                   → call
 */
void parser_go_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_GO_H */
