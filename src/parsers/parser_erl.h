#ifndef PARSER_ERL_H
#define PARSER_ERL_H
#include "types.h"
/* Parse Erlang source files (.erl, .hrl).
 * -module(my_module).            → module declaration
 * -include("header.hrl").        → internal dep
 * -include_lib("stdlib/include/lists.hrl"). → external dep
 * -behaviour(gen_server).        → external behaviour dep
 * -import(lists, [map/2]).       → external dep
 * -export([func/arity]).         → FunctionIndex
 * func(Args) ->                  → FunctionIndex
 * -record(name, {fields}).       → FunctionIndex (record)
 * -type mytype() ::              → FunctionIndex
 * -spec func(T) -> T.            → type spec (dep hint)
 */
void parser_erl_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
