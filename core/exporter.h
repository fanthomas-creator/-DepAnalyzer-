#ifndef EXPORTER_H
#define EXPORTER_H

#include "types.h"
#include <stdio.h>


void exporter_write_json(const FileIndex *index, const char *root_dir, FILE *out);

#endif 
