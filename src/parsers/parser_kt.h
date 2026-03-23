#ifndef PARSER_KT_H
#define PARSER_KT_H

#include "types.h"

/* Parse Kotlin source files.
 * Detects:
 *   import com.example.MyClass       → import
 *   import com.example.*             → wildcard import
 *   class / data class / sealed class / object / interface → FunctionIndex
 *   fun myFunc(...) / suspend fun    → FunctionIndex
 *   @Annotation                      → dependency
 *   obj.method() / Class.method()    → calls
 *   companion object                 → inner object
 */
void parser_kt_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_KT_H */
