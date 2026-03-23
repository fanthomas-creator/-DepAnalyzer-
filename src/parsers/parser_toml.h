#ifndef PARSER_TOML_H
#define PARSER_TOML_H

#include "types.h"

/* Parse TOML files: Cargo.toml, pyproject.toml, package.toml etc.
 * Detects:
 *   [dependencies] / [dev-dependencies] / [build-dependencies]
 *     serde = "1.0"                → external dep
 *     tokio = { version="1", features=["full"] } → external dep
 *   [tool.poetry.dependencies]     → poetry deps
 *   [project] / dependencies = [...] → PEP 517 deps
 *   [[workspace.members]]          → internal workspace refs
 *   path = "../other-crate"        → internal dep
 */
void parser_toml_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
