

#include "resolver.h"
#include <string.h>
#include <stdio.h>

static void add_unique_name(char arr[][MAX_NAME], int *count, int max, const char *val) {
    if (!val || val[0] == '\0') return;
    for (int i = 0; i < *count; i++)
        if (strcmp(arr[i], val) == 0) return;
    if (*count < max)
        strncpy(arr[(*count)++], val, MAX_NAME - 1);
}

static void add_unique_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val || val[0] == '\0') return;
    for (int i = 0; i < *count; i++)
        if (strcmp(arr[i], val) == 0) return;
    if (*count < max)
        strncpy(arr[(*count)++], val, MAX_SYMBOL - 1);
}


static const char *find_internal_file(const FileIndex *index, const char *import_name) {

    const char *name = import_name;
    if (strncmp(name, "local:", 6) == 0) name += 6;

    char stem[MAX_NAME];


    for (int i = 0; i < index->count; i++) {
        strip_ext(index->files[i].name, stem, sizeof(stem));
        if (strcmp(stem, name) == 0)
            return index->files[i].name;
    }


    for (int i = 0; i < index->count; i++) {
        const char *p = index->files[i].path;

        char pat_fwd[MAX_NAME + 3];
        char pat_bwd[MAX_NAME + 3];
        snprintf(pat_fwd, sizeof(pat_fwd), "/%s/",  name);
        snprintf(pat_bwd, sizeof(pat_bwd), "\\%s\\", name);
        if (strstr(p, pat_fwd) || strstr(p, pat_bwd))
            return index->files[i].name;
    }

    return NULL;
}


static int is_system_header(const char *name) {
    static const char *sys[] = {
        "stdio","stdlib","string","math","time","assert","ctype","errno",
        "limits","locale","setjmp","signal","stdarg","stddef","stdint",
        "stdbool","float","complex","fenv","inttypes","iso646","stdalign",
        "stdnoreturn","tgmath","uchar","wchar","wctype",
        
        "unistd","dirent","sys","fcntl","pthread","semaphore",
        
        "windows","winbase","winsock","winsock2",NULL
    };
    for (int i = 0; sys[i]; i++)
        if (strcmp(sys[i], name) == 0) return 1;
    return 0;
}

void resolver_resolve(FileIndex *index) {
    for (int i = 0; i < index->count; i++) {
        FileEntry *fe = &index->files[i];

        for (int j = 0; j < fe->import_count; j++) {
            const char *imp = fe->imports[j];


            int is_local_tag = (strncmp(imp, "local:", 6) == 0);
            const char *bare = is_local_tag ? imp + 6 : imp;

            const char *found = find_internal_file(index, bare);
            if (found) {

                if (strcmp(found, fe->name) != 0)
                    add_unique_name(fe->internal_deps, &fe->internal_count,
                                    MAX_IMPORTS, found);
            } else {

                if (!is_local_tag && is_system_header(bare)) {

                }
                add_unique_sym(fe->external_deps, &fe->external_count,
                               MAX_IMPORTS, bare);
            }
        }
    }
}

const char *resolver_find_function(const FunctionIndex *fi, const char *function_name) {
    for (int i = 0; i < fi->count; i++)
        if (strcmp(fi->defs[i].function, function_name) == 0)
            return fi->defs[i].file;
    return NULL;
}
