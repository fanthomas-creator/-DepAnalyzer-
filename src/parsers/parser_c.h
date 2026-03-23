#ifndef PARSER_C_H
#define PARSER_C_H

#include "types.h"

/* Parse a C/C++ file: fills fe->imports (#include), fe->calls.
   Also collects function definitions into defs[]. */
void parser_c_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_C_H */
