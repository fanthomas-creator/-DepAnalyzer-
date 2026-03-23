#ifndef PARSER_JS_H
#define PARSER_JS_H

#include "types.h"

/* Parse a JS/TS file: fills fe->imports, fe->calls.
   Also collects function definitions into defs[]. */
void parser_js_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_JS_H */
