#ifndef PARSER_R_H
#define PARSER_R_H

#include "types.h"

/* Parse R source files (.r, .R, .Rmd embedded chunks).
 * Detects:
 *   library(ggplot2)              → external package
 *   require(dplyr)                → external package
 *   library("tidyr")              → external package
 *   source("./utils.R")           → internal file
 *   source('../helpers/plot.R')   → internal file
 *   foo <- function(x, y)         → FunctionIndex
 *   myFunc = function(...)        → FunctionIndex
 *   setGeneric("myFunc", ...)     → FunctionIndex (S4)
 *   setClass("MyClass", ...)      → FunctionIndex (S4)
 *   R6Class("MyClass", ...)       → FunctionIndex (R6)
 *   pkg::func() / pkg:::func()   → external call with package
 *   func()                        → call
 */
void parser_r_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_R_H */
