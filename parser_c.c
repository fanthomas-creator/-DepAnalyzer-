/* parser_c.c  –  C / C++ include + function analysis
 * Handles:
 *   #include "local.h"    → internal (quoted)
 *   #include <stdlib.h>   → external (angled)
 *   return_type func_name(  → definition
 *   func_name(  → call
 * No external deps.
 * ---------------------------------------------------------- */

#include "parser_c.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int read_ident(const char *s, char *out, int out_size) {
    int i = 0;
    while (*s && (isalnum((unsigned char)*s) || *s == '_') && i < out_size - 1)
        out[i++] = *s++;
    out[i] = '\0';
    return i;
}

static void add_unique(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val || val[0] == '\0') return;
    for (int i = 0; i < *count; i++)
        if (strcmp(arr[i], val) == 0) return;
    if (*count < max)
        strncpy(arr[(*count)++], val, MAX_SYMBOL - 1);
}

/* ---- Include parsing --------------------------------------------- */

static void parse_include(const char *line, FileEntry *fe) {
    const char *p = skip_ws(line + 8);   /* skip "#include" */
    p = skip_ws(p);

    char mod[MAX_SYMBOL] = {0};
    int i = 0;

    if (*p == '"') {
        /* local include → internal candidate */
        p++;
        while (*p && *p != '"' && *p != '.' && i < MAX_SYMBOL - 1)
            mod[i++] = *p++;
        mod[i] = '\0';
        /* store with prefix "local:" to help resolver */
        char tagged[MAX_SYMBOL];
        snprintf(tagged, sizeof(tagged), "local:%s", mod);
        add_unique(fe->imports, &fe->import_count, MAX_IMPORTS, tagged);
    } else if (*p == '<') {
        /* system include → external */
        p++;
        while (*p && *p != '>' && *p != '.' && i < MAX_SYMBOL - 1)
            mod[i++] = *p++;
        mod[i] = '\0';
        add_unique(fe->imports, &fe->import_count, MAX_IMPORTS, mod);
    }
}

/* ---- Function definition parsing --------------------------------- */
/* Simple heuristic: non-indented line with ident( that isn't a keyword */

static const char *C_KEYWORDS[] = {
    "if","else","for","while","do","switch","case","return","sizeof",
    "typedef","struct","enum","union","static","extern","inline","const",
    "volatile","register","goto","break","continue","default","void",
    "int","long","short","char","float","double","unsigned","signed",
    "auto","printf","fprintf","sprintf","malloc","free","memset","memcpy",
    NULL
};

static int is_keyword(const char *s) {
    for (int i = 0; C_KEYWORDS[i]; i++)
        if (strcmp(C_KEYWORDS[i], s) == 0) return 1;
    return 0;
}

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    /* Must start at column 0 (non-indented) for definition heuristic */
    if (line[0] == ' ' || line[0] == '\t') return;

    /* Find rightmost identifier before '(' */
    const char *p = line;
    char last_ident[MAX_SYMBOL] = {0};
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            char ident[MAX_SYMBOL] = {0};
            int len = read_ident(p, ident, sizeof(ident));
            p += len;
            const char *q = skip_ws(p);
            if (*q == '(' && !is_keyword(ident))
                strncpy(last_ident, ident, MAX_SYMBOL - 1);
        } else if (*p == '(') {
            break;
        } else {
            p++;
        }
    }

    /* Check line ends with ')' or has a body open (simple check) */
    if (last_ident[0] && !is_keyword(last_ident) && *def_count < max_defs) {
        strncpy(defs[*def_count].function, last_ident, MAX_SYMBOL - 1);
        strncpy(defs[*def_count].file, filename, MAX_NAME - 1);
        (*def_count)++;
    }
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p = line;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            char ident[MAX_SYMBOL] = {0};
            int len = read_ident(p, ident, sizeof(ident));
            p += len;
            /* skip :: for C++ */
            while (p[0] == ':' && p[1] == ':') {
                p += 2;
                char sub[MAX_SYMBOL] = {0};
                int sl = read_ident(p, sub, sizeof(sub));
                p += sl;
                if (sl) strncpy(ident, sub, MAX_SYMBOL - 1);
            }
            /* skip -> or . for method calls */
            while ((p[0] == '-' && p[1] == '>') || p[0] == '.') {
                p += (p[0] == '-') ? 2 : 1;
                char sub[MAX_SYMBOL] = {0};
                int sl = read_ident(p, sub, sizeof(sub));
                p += sl;
                if (sl) strncpy(ident, sub, MAX_SYMBOL - 1);
            }
            if (*p == '(' && !is_keyword(ident) && ident[0])
                add_unique(fe->calls, &fe->call_count, MAX_CALLS, ident);
        } else {
            p++;
        }
    }
}

/* ---- Public API --------------------------------------------------- */

void parser_c_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f = fopen(fe->path, "r");
    if (!f) {
        fprintf(stderr, "[parser_c] Cannot open: %s\n", fe->path);
        return;
    }

    char line[MAX_LINE];
    int in_block_comment = 0;

    while (fgets(line, sizeof(line), f)) {
        str_trim(line);

        if (!in_block_comment && strstr(line, "/*")) in_block_comment = 1;
        if (in_block_comment) {
            if (strstr(line, "*/")) in_block_comment = 0;
            continue;
        }

        /* Strip // comment */
        char *cmt = strstr(line, "//");
        if (cmt) *cmt = '\0';

        const char *trimmed = line;
        if (!*trimmed) continue;

        if (strncmp(trimmed, "#include", 8) == 0)
            parse_include(trimmed, fe);

        parse_def(trimmed, defs, def_count, max_defs, fe->name);
        parse_calls(trimmed, fe);
    }

    fclose(f);
}
