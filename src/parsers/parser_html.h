#ifndef PARSER_HTML_H
#define PARSER_HTML_H

#include "types.h"

void parser_html_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_HTML_H */
