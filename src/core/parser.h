#ifndef PARSER_H
#define PARSER_H

#include "types.h"


void parser_parse_file(FileEntry *fe);


void parser_parse_all(FileIndex *index);


void parser_build_function_index(const FileIndex *index, FunctionIndex *fi);

#endif 
