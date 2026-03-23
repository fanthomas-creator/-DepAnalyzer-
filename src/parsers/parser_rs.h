#ifndef PARSER_RS_H
#define PARSER_RS_H

#include "types.h"

/* Parse Rust source files.
 * Detects:
 *   use std::collections::HashMap    → external (std)
 *   use crate::utils::helper         → internal (crate::)
 *   use super::models::User          → internal (super::)
 *   use self::config                 → internal (self::)
 *   extern crate serde               → external
 *   mod utils;                       → internal module (utils.rs)
 *   mod utils { ... }                → inline module
 *   fn my_func(...)                  → FunctionIndex
 *   pub fn / async fn / pub async fn → FunctionIndex
 *   struct MyStruct                  → FunctionIndex
 *   enum MyEnum                      → FunctionIndex
 *   trait MyTrait                    → FunctionIndex
 *   impl MyStruct                    → FunctionIndex
 *   obj.method()  / Type::func()     → call
 */
void parser_rs_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_RS_H */
