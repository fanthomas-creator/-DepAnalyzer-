#ifndef PARSER_RB_H
#define PARSER_RB_H

#include "types.h"

/* Parse Ruby files.
 * Detects:
 *   require 'net/http'          → external
 *   require_relative './utils'  → internal
 *   require 'my_module'         → internal (if file exists in project)
 *   include ModuleName          → mixin dependency
 *   extend  ModuleName          → mixin dependency
 *   class Foo < Bar             → def + Bar as dependency
 *   module Foo                  → FunctionIndex
 *   def foo / def self.foo      → FunctionIndex
 *   foo.bar() / Foo::bar()      → calls
 */
void parser_rb_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_RB_H */
