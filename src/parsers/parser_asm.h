#ifndef PARSER_ASM_H
#define PARSER_ASM_H

#include "types.h"

/* Parse Assembly source files (.asm, .s, .S).
 * Supports NASM, MASM, GAS (AT&T) syntax.
 *
 * Detects:
 *   %include "macros.asm"        → internal (NASM)
 *   %include <linux/unistd.h>    → external (NASM system include)
 *   .include "utils.s"           → internal (GAS)
 *   INCLUDE utils.asm            → internal (MASM)
 *   EXTERN printf                → external symbol
 *   EXTERN _start                → symbol reference
 *   extern printf                → external (GAS/NASM lowercase)
 *   .extern symbol               → external (GAS)
 *   GLOBAL my_func               → FunctionIndex (exported)
 *   global my_func               → FunctionIndex
 *   .global my_func              → FunctionIndex (GAS)
 *   my_func:                     → FunctionIndex (label definition)
 *   call printf / bl printf      → call
 *   jmp label / j label          → call
 */
void parser_asm_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_ASM_H */
