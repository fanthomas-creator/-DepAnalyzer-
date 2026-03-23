#ifndef PARSER_PY_H
#define PARSER_PY_H

#include "types.h"

/* Parse a Python file: fills fe->imports, fe->calls.
   Also collects function definitions into defs[]. */
void parser_py_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_PY_H */
