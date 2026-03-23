#ifndef PARSER_EX_H
#define PARSER_EX_H

#include "types.h"

/* Parse Elixir source files (.ex, .exs).
 * Detects:
 *   use Phoenix.Controller            → external dep
 *   use MyApp.Web, :controller        → internal dep
 *   alias MyApp.Accounts.User         → internal/external
 *   alias MyApp.Repo, as: R           → internal
 *   import Ecto.Query                  → external
 *   require Logger                     → external
 *   defmodule MyApp.UserController    → FunctionIndex
 *   def my_func(args)                  → FunctionIndex
 *   defp private_func(args)            → FunctionIndex
 *   defmacro my_macro(args)            → FunctionIndex
 *   @behaviour MyBehaviour             → dependency
 *   obj.method() / Module.func()       → calls
 */
void parser_ex_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_EX_H */
