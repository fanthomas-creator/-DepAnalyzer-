#ifndef PARSER_CR_H
#define PARSER_CR_H

#include "types.h"

/* Parse Crystal source files (.cr).
 * Detects:
 *   require "http/server"              → external (stdlib)
 *   require "json"                     → external
 *   require "./models/user"            → internal
 *   require "../lib/utils"             → internal
 *   class MyClass < BaseClass          → FunctionIndex + dep BaseClass
 *   struct MyStruct                    → FunctionIndex
 *   module MyModule                    → FunctionIndex
 *   abstract class Foo                 → FunctionIndex
 *   def my_method(args)                → FunctionIndex
 *   def self.class_method(args)        → FunctionIndex
 *   include Comparable                 → dep (mixin)
 *   extend SomeModule                  → dep (mixin)
 *   alias MyAlias = Type               → FunctionIndex
 *   annotation MyAnnotation            → FunctionIndex
 *   obj.method() / Class.static()      → calls
 */
void parser_cr_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_CR_H */
