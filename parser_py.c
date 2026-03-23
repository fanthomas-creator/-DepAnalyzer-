/* parser_py.c  –  Python import + function analysis
 * Handles:
 *   import foo
 *   import foo.bar
 *   from foo import bar
 *   from foo.bar import baz
 *   def my_func(...):
 *   foo.bar(...)  / bar(...)   → calls
 * No external deps.
 * ---------------------------------------------------------- */

#include "parser_py.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ---- helpers ----------------------------------------------------- */

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Skip leading spaces/tabs; returns pointer to first non-whitespace */
static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Read an identifier (letters, digits, underscore) into out */
static int read_ident(const char *s, char *out, int out_size) {
    int i = 0;
    while (*s && (isalnum((unsigned char)*s) || *s == '_') && i < out_size - 1) {
        out[i++] = *s++;
    }
    out[i] = '\0';
    return i;
}

/* Add unique string to array */
static void add_unique(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val || val[0] == '\0') return;
    for (int i = 0; i < *count; i++)
        if (strcmp(arr[i], val) == 0) return;
    if (*count < max) {
        strncpy(arr[(*count)++], val, MAX_SYMBOL - 1);
    }
}

/* ---- Import parsing ---------------------------------------------- */

/*  "import foo.bar.baz"  → take first component: "foo" */
static void parse_import(const char *line, FileEntry *fe) {
    const char *p = skip_ws(line + 7);   /* skip "import " */
    char mod[MAX_SYMBOL] = {0};
    int i = 0;
    /* take up to first dot or comma or space */
    while (*p && *p != '.' && *p != ',' && *p != ' ' && *p != '\t' && *p != '\n' && i < MAX_SYMBOL - 1)
        mod[i++] = *p++;
    mod[i] = '\0';
    if (mod[0]) add_unique(fe->imports, &fe->import_count, MAX_IMPORTS, mod);

    /* handle  "import foo, bar, baz" */
    while (*p == ',' || *p == ' ') {
        while (*p == ',' || *p == ' ') p++;
        /* skip to next non-ws identifier */
        char mod2[MAX_SYMBOL] = {0};
        int j = 0;
        while (*p && *p != '.' && *p != ',' && *p != ' ' && *p != '\t' && *p != '\n' && j < MAX_SYMBOL - 1)
            mod2[j++] = *p++;
        if (mod2[0]) add_unique(fe->imports, &fe->import_count, MAX_IMPORTS, mod2);
    }
}

/*  "from foo.bar import baz"  → take first component of module: "foo" */
static void parse_from_import(const char *line, FileEntry *fe) {
    const char *p = skip_ws(line + 5);   /* skip "from " */
    char mod[MAX_SYMBOL] = {0};
    int i = 0;
    /* relative imports: skip leading dots */
    while (*p == '.') p++;
    while (*p && *p != '.' && *p != ' ' && *p != '\t' && i < MAX_SYMBOL - 1)
        mod[i++] = *p++;
    mod[i] = '\0';
    if (mod[0]) add_unique(fe->imports, &fe->import_count, MAX_IMPORTS, mod);
}

/* ---- Function definition parsing --------------------------------- */

/*  "def my_func(...):"  or  "async def my_func(...):" */
static void parse_def(const char *line, FunctionDef *defs, int *def_count, int max_defs, const char *filename) {
    const char *p = strstr(line, "def ");
    if (!p) return;
    p += 4;
    p = skip_ws(p);
    char fname[MAX_SYMBOL] = {0};
    read_ident(p, fname, sizeof(fname));
    if (!fname[0] || fname[0] == '_') return;   /* skip private */
    if (*def_count < max_defs) {
        strncpy(defs[*def_count].function, fname, MAX_SYMBOL - 1);
        strncpy(defs[*def_count].file, filename, MAX_NAME - 1);
        (*def_count)++;
    }
}

/* ---- Call parsing ------------------------------------------------- */

/*  Scan line for identifier( patterns → collect as calls.
    Skip keywords that aren't real function calls. */
static const char *PY_KEYWORDS[] = {
    "if","else","elif","for","while","def","class","return","import",
    "from","with","as","try","except","finally","raise","lambda","print",
    "assert","pass","break","continue","yield","not","and","or","in",
    "is","True","False","None", NULL
};

static int is_keyword(const char *s) {
    for (int i = 0; PY_KEYWORDS[i]; i++)
        if (strcmp(PY_KEYWORDS[i], s) == 0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p = line;
    while (*p) {
        /* look for identifier followed by '(' */
        if (isalpha((unsigned char)*p) || *p == '_') {
            char ident[MAX_SYMBOL] = {0};
            const char *start = p;
            int len = read_ident(p, ident, sizeof(ident));
            p += len;
            /* skip dots: module.func → record func */
            while (*p == '.') {
                p++;
                char sub[MAX_SYMBOL] = {0};
                int sl = read_ident(p, sub, sizeof(sub));
                p += sl;
                if (sl) strncpy(ident, sub, MAX_SYMBOL - 1);
            }
            if (*p == '(' && !is_keyword(ident) && ident[0] != '\0') {
                add_unique(fe->calls, &fe->call_count, MAX_CALLS, ident);
            }
            (void)start;
        } else {
            p++;
        }
    }
}

/* ---- Public API --------------------------------------------------- */

void parser_py_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f = fopen(fe->path, "r");
    if (!f) {
        fprintf(stderr, "[parser_py] Cannot open: %s\n", fe->path);
        return;
    }

    char line[MAX_LINE];
    int in_multiline_str = 0;

    while (fgets(line, sizeof(line), f)) {
        str_trim(line);

        /* Very basic multiline string skip (triple quotes) */
        if (strstr(line, "\"\"\"") || strstr(line, "'''")) {
            in_multiline_str = !in_multiline_str;
            if (!in_multiline_str) continue;
        }
        if (in_multiline_str) continue;

        /* Skip comment lines */
        const char *trimmed = skip_ws(line);
        if (*trimmed == '#') continue;

        if (starts_with(trimmed, "import "))
            parse_import(trimmed, fe);
        else if (starts_with(trimmed, "from "))
            parse_from_import(trimmed, fe);

        if (strstr(trimmed, "def "))
            parse_def(trimmed, defs, def_count, max_defs, fe->name);

        parse_calls(trimmed, fe);
    }

    fclose(f);
}
