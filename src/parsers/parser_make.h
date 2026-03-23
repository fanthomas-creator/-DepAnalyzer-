#ifndef PARSER_MAKE_H
#define PARSER_MAKE_H

#include "types.h"

/* Parse Makefile / makefile / GNUmakefile.
 * Detects:
 *   include config.mk             → internal dep
 *   include $(DIR)/rules.mk       → internal dep
 *   -include optional.mk          → internal dep
 *   target: dep1 dep2 dep3        → dep1/dep2/dep3 as calls
 *   @$(CC) / $(MAKE)              → tool references
 *   target name                   → FunctionIndex (target as "function")
 */
void parser_make_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
