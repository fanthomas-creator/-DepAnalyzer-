#ifndef RESOLVER_H
#define RESOLVER_H

#include "types.h"


void resolver_resolve(FileIndex *index);


const char *resolver_find_function(const FunctionIndex *fi, const char *function_name);

#endif 