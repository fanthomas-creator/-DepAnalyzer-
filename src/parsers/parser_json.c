/* parser_json.c  –  JSON dependency file parser
 *
 * Supports:
 *   package.json    → dependencies / devDependencies / peerDependencies / workspaces
 *   tsconfig.json   → paths (alias map), references (project refs)
 *   composer.json   → require / require-dev (PHP)
 *
 * Strategy: line-by-line heuristic (no full JSON parser needed).
 * We detect section headers and extract string keys/values.
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_json.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================
 * Helpers
 * ================================================================ */

static const char *skip_ws(const char *s) {
    while (*s==' '||*s=='\t') s++;
    return s;
}

/* Extract content of first quoted string on line into out */
static int extract_string(const char *s, char *out, int out_size) {
    while (*s && *s!='"' && *s!='\'') s++;
    if (!*s) return 0;
    char q=*s++;
    int i=0;
    while (*s && *s!=q && i<out_size-1) out[i++]=*s++;
    out[i]='\0';
    return i;
}

/* Extract the KEY of a JSON line:  "key": ... */
static int extract_key(const char *line, char *out, int out_size) {
    return extract_string(line, out, out_size);
}

/* Extract the VALUE string of a JSON line:  "key": "value" */
static int extract_value(const char *line, char *out, int out_size) {
    const char *p = line;
    /* skip first quoted string (key) */
    while (*p && *p!='"' && *p!='\'') p++;
    if (!*p) return 0;
    char q=*p++; while (*p && *p!=q) p++; if (*p) p++; /* past closing quote */
    /* skip colon */
    while (*p && *p!=':') p++; if (*p) p++;
    /* now get value string */
    while (*p && *p!='"' && *p!='\'') p++;
    if (!*p) return 0;
    q=*p++;
    int i=0;
    while (*p && *p!=q && i<out_size-1) out[i++]=*p++;
    out[i]='\0';
    return i;
}

static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->import_count;i++) if(strcmp(fe->imports[i],val)==0) return;
    if (fe->import_count < MAX_IMPORTS)
        strncpy(fe->imports[fe->import_count++], val, MAX_SYMBOL-1);
}

/* ================================================================
 * Section detection
 * ================================================================ */

typedef enum {
    SEC_NONE = 0,
    /* package.json */
    SEC_DEPS,           /* "dependencies"        */
    SEC_DEV_DEPS,       /* "devDependencies"     */
    SEC_PEER_DEPS,      /* "peerDependencies"    */
    SEC_OPT_DEPS,       /* "optionalDependencies"*/
    SEC_WORKSPACES,     /* "workspaces"          */
    SEC_SCRIPTS,        /* "scripts" — skip      */
    /* tsconfig.json */
    SEC_PATHS,          /* "paths"               */
    SEC_REFERENCES,     /* "references"          */
    /* composer.json */
    SEC_COMPOSER_REQ,   /* "require"             */
    SEC_COMPOSER_DEV,   /* "require-dev"         */
} Section;

static Section detect_section(const char *key) {
    if (strcmp(key,"dependencies")        ==0) return SEC_DEPS;
    if (strcmp(key,"devDependencies")     ==0) return SEC_DEV_DEPS;
    if (strcmp(key,"peerDependencies")    ==0) return SEC_PEER_DEPS;
    if (strcmp(key,"optionalDependencies")==0) return SEC_OPT_DEPS;
    if (strcmp(key,"workspaces")          ==0) return SEC_WORKSPACES;
    if (strcmp(key,"scripts")             ==0) return SEC_SCRIPTS;
    if (strcmp(key,"paths")               ==0) return SEC_PATHS;
    if (strcmp(key,"references")          ==0) return SEC_REFERENCES;
    if (strcmp(key,"require")             ==0) return SEC_COMPOSER_REQ;
    if (strcmp(key,"require-dev")         ==0) return SEC_COMPOSER_DEV;
    return SEC_NONE;
}

/* Strip leading @scope/ from npm scoped packages for display:
 * "@angular/core" → "@angular/core" (keep full name, it's meaningful) */
static int is_npm_package(const char *s) {
    /* packages don't contain spaces, quotes, brackets */
    if (!s||!s[0]) return 0;
    if (s[0]=='@') return 1;                /* scoped package */
    if (strncmp(s,"php",3)==0) return 0;    /* PHP namespace */
    return 1;
}

/* ================================================================
 * Public API
 * ================================================================ */

void parser_json_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;

    FILE *f = fopen(fe->path,"r");
    if (!f) { fprintf(stderr,"[parser_json] Cannot open: %s\n",fe->path); return; }

    char line[MAX_LINE];
    Section current_sec = SEC_NONE;
    int depth = 0;           /* brace nesting depth                    */
    int sec_depth = 0;       /* depth at which current section started */

    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        const char *p = skip_ws(line);
        if (!*p) continue;

        /* Track brace depth */
        for (const char *c=line; *c; c++) {
            if (*c=='{') depth++;
            if (*c=='}') {
                if (current_sec!=SEC_NONE && depth==sec_depth) {
                    current_sec=SEC_NONE;  /* exiting section block */
                }
                depth--;
            }
            if (*c=='[') depth++;
            if (*c==']') depth--;
        }

        /* Look for section-header line: "sectionName": { */
        if (strchr(line,'"') && strchr(line,':')) {
            char key[MAX_SYMBOL]={0};
            extract_key(line, key, sizeof(key));
            Section s = detect_section(key);
            if (s != SEC_NONE) {
                current_sec = s;
                sec_depth   = depth;   /* depth AFTER the '{' on this line */
                continue;
            }
        }

        if (current_sec == SEC_NONE || current_sec == SEC_SCRIPTS) continue;

        /* ---- package.json dependency sections ---- */
        if (current_sec==SEC_DEPS || current_sec==SEC_DEV_DEPS ||
            current_sec==SEC_PEER_DEPS || current_sec==SEC_OPT_DEPS) {
            /* Line format: "package-name": "^1.2.3" */
            char pkg[MAX_SYMBOL]={0};
            if (extract_key(line, pkg, sizeof(pkg)) && is_npm_package(pkg)) {
                add_import(fe, pkg);
            }
        }

        /* ---- workspaces: ["packages/*", "apps/*"] ---- */
        if (current_sec == SEC_WORKSPACES) {
            char val[MAX_SYMBOL]={0};
            if (extract_string(line, val, sizeof(val)) && val[0]) {
                /* store workspace pattern as internal reference */
                add_import(fe, val);
            }
        }

        /* ---- tsconfig paths: "@app/*": ["./src/*"] ---- */
        if (current_sec == SEC_PATHS) {
            char alias[MAX_SYMBOL]={0};
            extract_key(line, alias, sizeof(alias));
            /* strip trailing /* */
            char *star = strstr(alias,"/*");
            if (star) *star='\0';
            if (alias[0]) add_import(fe, alias);
        }

        /* ---- tsconfig references: { "path": "../other-pkg" } ---- */
        if (current_sec == SEC_REFERENCES) {
            char key[MAX_SYMBOL]={0}, val[MAX_SYMBOL]={0};
            extract_key(line, key, sizeof(key));
            if (strcmp(key,"path")==0) {
                extract_value(line, val, sizeof(val));
                if (val[0]) add_import(fe, val);
            }
        }

        /* ---- composer.json ---- */
        if (current_sec==SEC_COMPOSER_REQ || current_sec==SEC_COMPOSER_DEV) {
            char pkg[MAX_SYMBOL]={0};
            extract_key(line, pkg, sizeof(pkg));
            /* skip "php" version constraint */
            if (pkg[0] && strcmp(pkg,"php")!=0 && strcmp(pkg,"ext-json")!=0
                && strcmp(pkg,"ext-mbstring")!=0) {
                add_import(fe, pkg);
            }
        }
    }

    fclose(f);
}
