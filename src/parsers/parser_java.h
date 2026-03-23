#ifndef PARSER_JAVA_H
#define PARSER_JAVA_H

#include "types.h"

/* Parse Java source files.
 * Detects:
 *   import com.example.MyClass       → external/internal
 *   import static java.lang.Math.*   → external
 *   package com.example.controllers  → namespace declaration
 *   class / interface / enum / record → FunctionIndex
 *   @Annotation                      → dependency
 *   public void method() / static T func() → FunctionIndex
 *   object.method() / Class.staticMethod() → calls
 */
void parser_java_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_JAVA_H */
