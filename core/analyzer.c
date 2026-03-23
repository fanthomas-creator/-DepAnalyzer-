
#include "analyzer.h"
#include "resolver.h"
#include <string.h>

static void add_unique_name(char arr[][MAX_NAME], int *count, int max, const char *val) {
    if (!val || val[0] == '\0') return;
    for (int i = 0; i < *count; i++)
        if (strcmp(arr[i], val) == 0) return;
    if (*count < max)
        strncpy(arr[(*count)++], val, MAX_NAME - 1);
}

void analyzer_resolve_calls(FileIndex *index, const FunctionIndex *fi) {
    for (int i = 0; i < index->count; i++) {
        FileEntry *fe = &index->files[i];

        for (int j = 0; j < fe->call_count; j++) {
            const char *defining_file = resolver_find_function(fi, fe->calls[j]);
            if (defining_file && strcmp(defining_file, fe->name) != 0) {
               
                add_unique_name(fe->call_files, &fe->call_file_count,
                                MAX_CALLS, defining_file);
            }
        }
    }
}
