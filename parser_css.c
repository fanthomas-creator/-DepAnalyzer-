#include "parser_css.h"

static void add_css_def(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs, const char *name) {
    /* Ajout dans calls du fichier */
    int call_exists = 0;
    for (int i = 0; i < fe->call_count; i++) {
        if (strcmp(fe->calls[i], name) == 0) { call_exists = 1; break; }
    }
    if (!call_exists && fe->call_count < MAX_CALLS) {
        strncpy(fe->calls[fe->call_count], name, MAX_SYMBOL - 1);
        fe->calls[fe->call_count][MAX_SYMBOL - 1] = '\0';
        fe->call_count++;
    }

    /* Ajout au FunctionIndex global */
    int def_exists = 0;
    for (int i = 0; i < *def_count; i++) {
        if (strcmp(defs[i].function, name) == 0 && strcmp(defs[i].file, fe->name) == 0) {
            def_exists = 1; break;
        }
    }
    if (!def_exists && *def_count < max_defs) {
        strncpy(defs[*def_count].function, name, MAX_SYMBOL - 1);
        defs[*def_count].function[MAX_SYMBOL - 1] = '\0';
        strncpy(defs[*def_count].file, fe->name, MAX_NAME - 1);
        defs[*def_count].file[MAX_NAME - 1] = '\0';
        (*def_count)++;
    }
}

static void add_css_import(FileEntry *fe, const char *url) {
    const char *base = path_basename(url);
    char no_ext[MAX_SYMBOL];
    strip_ext(base, no_ext, sizeof(no_ext));
    if (!*no_ext) return;

    for (int i = 0; i < fe->import_count; i++) {
        if (strcmp(fe->imports[i], no_ext) == 0) return;
    }
    if (fe->import_count < MAX_IMPORTS) {
        strncpy(fe->imports[fe->import_count], no_ext, MAX_SYMBOL - 1);
        fe->imports[fe->import_count][MAX_SYMBOL - 1] = '\0';
        fe->import_count++;
    }
}

void parser_css_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f = fopen(fe->path, "r");
    if (!f) return;

    int in_block   = 0;
    int in_comment = 0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p) {
            /* Commentaires CSS : slash-star ... star-slash */
            if (!in_comment && *p == '/' && *(p+1) == '*') {
                in_comment = 1; p += 2; continue;
            }
            if (in_comment && *p == '*' && *(p+1) == '/') {
                in_comment = 0; p += 2; continue;
            }
            if (in_comment) { p++; continue; }

            /* @import "file.css" ou @import url("file.css") */
            if (strncmp(p, "@import", 7) == 0) {
                p += 7;
                while (*p == ' ' || *p == '\t') p++;
                char  q     = 0;
                const char *start = NULL;

                if (strncmp(p, "url(", 4) == 0) {
                    p += 4;
                    if (*p == '"' || *p == '\'') { q = *p; start = p + 1; }
                    else { q = ')'; start = p; }
                } else if (*p == '"' || *p == '\'') {
                    q = *p; start = p + 1;
                }

                if (start) {
                    const char *end = strchr(start, q);
                    if (end) {
                        char url[MAX_PATH];
                        int len = (int)(end - start);
                        if (len < MAX_PATH) {
                            strncpy(url, start, len);
                            url[len] = '\0';
                            add_css_import(fe, url);
                        }
                        p = (char *)end + 1; continue;
                    }
                }
            }

            /* Suivi des blocs { } */
            if (*p == '{') { in_block++; p++; continue; }
            if (*p == '}') { if (in_block > 0) in_block--; p++; continue; }

            /* .classe et #id — uniquement hors blocs */
            if (in_block == 0 && (*p == '.' || *p == '#')) {
                p++;
                const char *start = p;
                while (*p && ((*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') ||
                              (*p>='0'&&*p<='9') || *p=='-' || *p=='_')) {
                    p++;
                }
                if (p > start && (p - start) < MAX_SYMBOL) {
                    char name[MAX_SYMBOL];
                    strncpy(name, start, p - start);
                    name[p - start] = '\0';
                    add_css_def(fe, defs, def_count, max_defs, name);
                    continue;
                }
            }
            p++;
        }
    }
    fclose(f);
}
