#ifndef PARSER_MD_H
#define PARSER_MD_H

#include "types.h"

/* Parse Markdown files.
 * Detects:
 *   [text](./other.md)               -> internal link
 *   [text](https://example.com/foo)  -> external (domain stored)
 *   ![alt](./img.png)                -> image asset (ignored)
 *   ```python / ```javascript        -> code fence language tag
 *   # Title / ## Section             -> heading stored as call (structure)
 */
void parser_md_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
