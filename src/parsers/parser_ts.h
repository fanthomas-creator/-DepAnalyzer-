#ifndef PARSER_TS_H
#define PARSER_TS_H

#include "types.h"

/* Parse a TypeScript file.
 * Handles everything parser_js handles PLUS:
 *   import type { Foo } from './foo'
 *   interface Foo { }
 *   enum Direction { }
 *   type Alias = ...
 *   export class / export function / export const
 *   decorators: @Component, @Injectable ...
 */
void parser_ts_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_TS_H */
