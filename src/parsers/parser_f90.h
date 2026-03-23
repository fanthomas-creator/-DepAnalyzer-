#ifndef PARSER_F90_H
#define PARSER_F90_H
#include "types.h"
/* Parse Fortran source files (.f90, .f95, .f03, .f08, .for, .f, .F90).
 * USE module_name                → external/internal module dep
 * USE module_name, ONLY: sym     → dep
 * INCLUDE 'file.f90'             → internal include
 * MODULE my_module               → FunctionIndex
 * PROGRAM my_program             → FunctionIndex
 * SUBROUTINE my_sub(args)        → FunctionIndex
 * FUNCTION my_func(args)         → FunctionIndex
 * CALL my_sub(args)              → call
 * INTERFACE / CONTAINS           → structural
 */
void parser_f90_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
