#ifndef PARSER_CSS_H
#define PARSER_CSS_H

#include "types.h"

void parser_css_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_CSS_H */
