#ifndef PARSER_SH_H
#define PARSER_SH_H

#include "types.h"

/* Parse Shell script files (.sh, .bash, .zsh, .fish).
 * Detects:
 *   source ./lib.sh               → internal dep
 *   . ./utils.sh                  → internal dep
 *   source lib.sh                 → internal dep
 *   bash ./scripts/run.sh         → internal dep
 *   sh ./deploy.sh                → internal dep
 *   ./myscript.sh                 → internal dep
 *   curl https://example.com/..   → external URL
 *   wget https://example.com/..   → external URL
 *   apt install pkg / apt-get     → external package
 *   pip install pkg               → external package
 *   npm install pkg               → external package
 *   brew install pkg              → external package
 *   function_name() { }           → FunctionIndex
 *   function func_name { }        → FunctionIndex
 */
void parser_sh_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_SH_H */
