#ifndef PARSER_OCAML_H
#define PARSER_OCAML_H

#include "types.h"

/* Parse OCaml source files (.ml, .mli).
 * Detects:
 *   open Printf                       → external dep
 *   open MyApp.Models                 → internal/external
 *   module M = Map.Make(String)       → dep
 *   module M = MyApp.Utils            → dep
 *   include Comparable                → dep
 *   let my_func args =                → FunctionIndex
 *   let rec my_func args =            → FunctionIndex
 *   type my_type =                    → FunctionIndex
 *   exception MyExn                   → FunctionIndex
 *   class my_class =                  → FunctionIndex
 *   external c_func : ...             → external C dep
 *   module MyModule = struct ... end  → FunctionIndex
 */
void parser_ocaml_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_OCAML_H */
