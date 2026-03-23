#include "parser_html.h"
#include <ctype.h>

static void add_html_call(FileEntry *fe, const char *val) {
    if (!val || !*val) return;
    for (int i = 0; i < fe->call_count; i++) {
        if (strcmp(fe->calls[i], val) == 0) return;
    }
    if (fe->call_count < MAX_CALLS) {
        strncpy(fe->calls[fe->call_count], val, MAX_SYMBOL - 1);
        fe->calls[fe->call_count][MAX_SYMBOL - 1] = '\0';
        fe->call_count++;
    }
}

static void add_html_import(FileEntry *fe, const char *url) {
    if (!url || !*url) return;
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

void parser_html_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;

    FILE *f = fopen(fe->path, "r");
    if (!f) return;

    char line[MAX_LINE];
    int in_target_tag = 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p) {
            if (strncmp(p, "<script", 7) == 0 || strncmp(p, "<link", 5) == 0 || strncmp(p, "<iframe", 7) == 0) {
                in_target_tag = 1;
            }
            if (*p == '>') {
                in_target_tag = 0;
            }

            /* id="..." */
            if (strncmp(p, "id=", 3) == 0 && (p[3] == '"' || p[3] == '\'')) {
                char q = p[3];
                const char *start = p + 4;
                const char *end = strchr(start, q);
                if (end && (end - start) < MAX_SYMBOL) {
                    char val[MAX_SYMBOL];
                    strncpy(val, start, end - start);
                    val[end - start] = '\0';
                    add_html_call(fe, val);
                    p = (char *)end; continue;
                }
            }

            /* class="..." */
            if (strncmp(p, "class=", 6) == 0 && (p[6] == '"' || p[6] == '\'')) {
                char q = p[6];
                const char *start = p + 7;
                const char *end = strchr(start, q);
                if (end && (end - start) < MAX_LINE) {
                    char val[MAX_LINE];
                    strncpy(val, start, end - start);
                    val[end - start] = '\0';
                    char *tok = strtok(val, " \t\r\n");
                    while (tok) {
                        add_html_call(fe, tok);
                        tok = strtok(NULL, " \t\r\n");
                    }
                    p = (char *)end; continue;
                }
            }

            /* src="..." et href="..." dans les balises cibles */
            if (in_target_tag) {
                int is_src  = (strncmp(p, "src=",  4) == 0);
                int is_href = (!is_src && strncmp(p, "href=", 5) == 0);

                if (is_src || is_href) {
                    int offset = is_src ? 4 : 5;
                    char q = p[offset];
                    if (q == '"' || q == '\'') {
                        const char *start = p + offset + 1;
                        const char *end = strchr(start, q);
                        if (end && (end - start) < MAX_PATH) {
                            char val[MAX_PATH];
                            strncpy(val, start, end - start);
                            val[end - start] = '\0';
                            add_html_import(fe, val);
                            p = (char *)end; continue;
                        }
                    }
                }
            }
            p++;
        }
    }
    fclose(f);
}
