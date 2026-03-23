#ifndef PARSER_COB_H
#define PARSER_COB_H
#include "types.h"
/* Parse COBOL source files (.cob, .cbl, .cpy, .cobol).
 * COPY SOMELIB.                  → internal/external copybook
 * COPY SOMELIB IN LIBRARY.       → external lib
 * CALL 'SUBPROGRAM'              → external call
 * CALL WS-PROG-NAME              → dynamic call
 * PROGRAM-ID. MY-PROGRAM.        → FunctionIndex (program name)
 * SECTION name.                  → FunctionIndex (section)
 * PERFORM MY-PARA.               → call
 * ENVIRONMENT DIVISION           → structural
 */
void parser_cob_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
