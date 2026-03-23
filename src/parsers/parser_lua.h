#ifndef PARSER_LUA_H
#define PARSER_LUA_H

#include "types.h"

/* Parse Lua source files.
 * Detects:
 *   require 'module'              → import
 *   require "lib.utils"           → import (dot = path separator)
 *   require('./relative')         → internal
 *   local x = require('x')       → import
 *   dofile('./other.lua')         → internal
 *   loadfile('./config.lua')      → internal
 *   function myFunc()             → FunctionIndex
 *   local function myFunc()       → FunctionIndex
 *   MyClass = {}                  → FunctionIndex (Lua class pattern)
 *   function MyClass:method()     → FunctionIndex
 *   obj:method() / lib.func()    → calls
 */
void parser_lua_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_LUA_H */
