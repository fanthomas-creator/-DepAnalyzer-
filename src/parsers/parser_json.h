#ifndef PARSER_JSON_H
#define PARSER_JSON_H

#include "types.h"

/* Parse JSON files relevant to dependency analysis:
 *   package.json     → dependencies, devDependencies, peerDependencies
 *   tsconfig.json    → paths aliases, references
 *   composer.json    → PHP require / require-dev
 *   Pipfile / pyproject.toml handled elsewhere
 *
 * Detected data goes into fe->external_deps (npm packages)
 * and fe->internal_deps (workspace / path references).
 */
void parser_json_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_JSON_H */
