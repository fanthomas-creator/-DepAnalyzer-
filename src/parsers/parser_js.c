/* parser_js.c  –  JavaScript / TypeScript import + function analysis
 *
 * Handles three dependency styles:
 *
 *  1. ES modules     : import foo from './foo'
 *                      import { bar } from './bar'
 *  2. CommonJS       : const x = require('./x')
 *  3. Browser globals: vault.init() / signaling.connect() / crypto.hash()
 *                      → identifiers used as root objects but never declared
 *                        in this file  →  implicit cross-file dependency
 *
 * No external deps.
 */

#include "parser_js.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}
static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}
static int read_ident(const char *s, char *out, int out_size) {
    int i = 0;
    while (*s && (isalnum((unsigned char)*s) || *s == '_' || *s == '$') && i < out_size - 1)
        out[i++] = *s++;
    out[i] = '\0';
    return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val || !val[0]) return;
    for (int i = 0; i < *count; i++)
        if (strcmp(arr[i], val) == 0) return;
    if (*count < max)
        strncpy(arr[(*count)++], val, MAX_SYMBOL - 1);
}

/* ================================================================ Keywords + known globals to ignore */

static const char *IGNORE[] = {
    /* JS language */
    "if","else","for","while","do","switch","case","default","return",
    "typeof","instanceof","new","delete","void","throw","catch","finally",
    "try","import","export","from","class","extends","super","this","self",
    "const","let","var","function","async","await","of","in","debugger",
    "break","continue","yield","static","get","set","null","undefined",
    "true","false","NaN","Infinity","arguments",
    /* Built-in objects */
    "console","window","document","navigator","location","history",
    "Math","JSON","Date","Array","Object","String","Number","Boolean",
    "Promise","Error","Map","Set","WeakMap","WeakSet","Symbol",
    "Proxy","Reflect","parseInt","parseFloat","isNaN","isFinite",
    "encodeURIComponent","decodeURIComponent","encodeURI","decodeURI",
    /* Browser APIs */
    "setTimeout","setInterval","clearTimeout","clearInterval",
    "fetch","XMLHttpRequest","WebSocket","URL","URLSearchParams",
    "localStorage","sessionStorage","indexedDB",
    "alert","confirm","prompt","open",
    "requestAnimationFrame","cancelAnimationFrame","performance",
    "ArrayBuffer","Uint8Array","Int8Array","Uint16Array","Int16Array",
    "Uint32Array","Int32Array","Float32Array","Float64Array","DataView",
    "TextEncoder","TextDecoder","Blob","File","FileReader","FormData",
    "Event","CustomEvent","EventTarget","AbortController",
    "RTCPeerConnection","RTCSessionDescription","RTCIceCandidate",
    "MediaStream","Worker","Notification","WebAssembly",
    /* Node.js */
    "process","Buffer","global","module","exports","require",
    "__dirname","__filename",
    NULL
};

static int is_ignored(const char *s) {
    for (int i = 0; IGNORE[i]; i++)
        if (strcmp(IGNORE[i], s) == 0) return 1;
    return 0;
}

/* ================================================================ Local-declaration tracking */

#define MAX_LOCAL 512
typedef struct { char n[MAX_LOCAL][MAX_SYMBOL]; int c; } LocalDecls;

static void ld_add(LocalDecls *ld, const char *name) {
    if (!name || !name[0]) return;
    for (int i = 0; i < ld->c; i++) if (strcmp(ld->n[i], name) == 0) return;
    if (ld->c < MAX_LOCAL) strncpy(ld->n[ld->c++], name, MAX_SYMBOL - 1);
}
static int ld_has(const LocalDecls *ld, const char *name) {
    for (int i = 0; i < ld->c; i++) if (strcmp(ld->n[i], name) == 0) return 1;
    return 0;
}

static void collect_decls(const char *line, LocalDecls *ld) {
    const char *p;

    /* class Foo */
    p = strstr(line, "class ");
    if (p && (p == line || !isalnum((unsigned char)*(p-1)))) {
        char nm[MAX_SYMBOL]={0}; read_ident(skip_ws(p+6), nm, sizeof(nm)); ld_add(ld, nm);
    }
    /* function foo */
    p = strstr(line, "function ");
    if (p && (p == line || !isalnum((unsigned char)*(p-1)))) {
        p = skip_ws(p+9);
        if (*p != '(') { char nm[MAX_SYMBOL]={0}; read_ident(p, nm, sizeof(nm)); ld_add(ld, nm); }
    }
    /* const/let/var foo  or  const { a, b } */
    const char *kws[] = {"const ","let ","var ",NULL};
    for (int i = 0; kws[i]; i++) {
        const char *q = line;
        while ((q = strstr(q, kws[i])) != NULL) {
            if (q > line && isalnum((unsigned char)*(q-1))) { q++; continue; }
            q = skip_ws(q + strlen(kws[i]));
            if (*q == '{') {
                q++;
                while (*q && *q != '}') {
                    q = skip_ws(q);
                    char nm[MAX_SYMBOL]={0}; int l=read_ident(q,nm,sizeof(nm));
                    if (l) { ld_add(ld,nm); q+=l; }
                    while (*q && *q!=',' && *q!='}') q++;
                    if (*q==',') q++;
                }
            } else {
                char nm[MAX_SYMBOL]={0}; read_ident(q,nm,sizeof(nm)); ld_add(ld,nm);
            }
            q++;
        }
    }
}

/* ================================================================ Import / require */

static void extract_module(const char *s, char *out, int out_size) {
    while (*s && *s != '"' && *s != '\'') s++;
    if (!*s) { out[0]='\0'; return; }
    char q = *s++;
    int i = 0;
    if (*s == '.') { while (*s=='.'||*s=='/'||*s=='\\') s++; }
    while (*s && *s != q && *s != '/' && *s != '\\' && i < out_size-1)
        out[i++] = *s++;
    out[i] = '\0';
    char *dot = strrchr(out,'.');
    if (dot && (strcmp(dot,".js")==0||strcmp(dot,".ts")==0||
                strcmp(dot,".jsx")==0||strcmp(dot,".tsx")==0)) *dot='\0';
}

static void parse_import_stmt(const char *line, FileEntry *fe) {
    char mod[MAX_SYMBOL]={0}; extract_module(line, mod, sizeof(mod));
    if (mod[0]) add_sym(fe->imports, &fe->import_count, MAX_IMPORTS, mod);
}
static void parse_require(const char *line, FileEntry *fe) {
    const char *p = strstr(line, "require(");
    if (!p) return;
    char mod[MAX_SYMBOL]={0}; extract_module(p+7, mod, sizeof(mod));
    if (mod[0]) add_sym(fe->imports, &fe->import_count, MAX_IMPORTS, mod);
}

/* ================================================================ Global-reference detection
 *
 * Finds patterns like:   vault.init()   signaling.connect()   crypto.hash()
 * "root" identifier before '.' is not declared locally → implicit dependency.
 */
static void parse_global_refs(const char *line, FileEntry *fe, const LocalDecls *ld) {
    const char *p = line;
    while (*p) {
        if (!(isalpha((unsigned char)*p) || *p=='_' || *p=='$')) { p++; continue; }
        /* word boundary: skip if part of longer ident */
        if (p > line && (isalnum((unsigned char)*(p-1)) || *(p-1)=='_' || *(p-1)=='$')) { p++; continue; }
        /* skip if preceded by '.' (we are a method/property, not a root) */
        if (p > line && *(p-1) == '.') { char skip[MAX_SYMBOL]={0}; p+=read_ident(p,skip,sizeof(skip)); continue; }

        char root[MAX_SYMBOL]={0};
        int rlen = read_ident(p, root, sizeof(root));
        const char *after = p + rlen;

        /* Only capture "root.something" — root must be followed by a dot
         * This avoids capturing bare function calls like sendMessage() which
         * could be local methods.  root( alone is too noisy without context. */
        if (*after == '.' && !is_ignored(root) && !ld_has(ld, root)) {
            add_sym(fe->imports, &fe->import_count, MAX_IMPORTS, root);
        }
        p = after;
    }
}

/* ================================================================ Function-definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p;

    /* function foo() */
    p = strstr(line, "function ");
    if (p && (p==line||!isalnum((unsigned char)*(p-1)))) {
        p = skip_ws(p+9);
        if (*p != '(') {
            char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
            if (nm[0] && *def_count < max_defs) {
                strncpy(defs[*def_count].function, nm, MAX_SYMBOL-1);
                strncpy(defs[*def_count].file, filename, MAX_NAME-1);
                (*def_count)++;
            }
        }
    }

    /* const/let/var foo = () => ... */
    const char *kws[] = {"const ","let ","var ",NULL};
    for (int i = 0; kws[i]; i++) {
        p = strstr(line, kws[i]);
        if (!p) continue;
        if (p>line && isalnum((unsigned char)*(p-1))) continue;
        p = skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if (!l) continue;
        p = skip_ws(p+l);
        if (*p!='=') continue;
        const char *rhs=p+1;
        if (strstr(rhs,"=>") || strstr(rhs,"function")) {
            if (nm[0] && *def_count < max_defs) {
                strncpy(defs[*def_count].function, nm, MAX_SYMBOL-1);
                strncpy(defs[*def_count].file, filename, MAX_NAME-1);
                (*def_count)++;
            }
        }
    }

    /* class methods: [async] methodName(...) { */
    p = skip_ws(line);
    if (starts_with(p,"async ")) p=skip_ws(p+6);
    char mn[MAX_SYMBOL]={0}; int ml=read_ident(p,mn,sizeof(mn));
    if (ml>0 && !is_ignored(mn)) {
        const char *after=skip_ws(p+ml);
        if (*after=='(' && *def_count < max_defs) {
            strncpy(defs[*def_count].function, mn, MAX_SYMBOL-1);
            strncpy(defs[*def_count].file, filename, MAX_NAME-1);
            (*def_count)++;
        }
    }
}

/* ================================================================ Call parsing */

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p = line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_'||*p=='$') {
            char ident[MAX_SYMBOL]={0}; int len=read_ident(p,ident,sizeof(ident)); p+=len;
            while (*p=='.') {
                p++; char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if (*p=='(' && !is_ignored(ident) && ident[0])
                add_sym(fe->calls, &fe->call_count, MAX_CALLS, ident);
        } else p++;
    }
}

/* ================================================================ Public API — two-pass */

void parser_js_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f = fopen(fe->path, "r");
    if (!f) { fprintf(stderr, "[parser_js] Cannot open: %s\n", fe->path); return; }

    LocalDecls ld; memset(&ld, 0, sizeof(ld));
    char line[MAX_LINE];
    int in_bc = 0;

    /* Pass 1 — collect local declarations */
    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        if (!in_bc && strstr(line,"/*")) in_bc=1;
        if (in_bc) { if (strstr(line,"*/")) in_bc=0; continue; }
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        collect_decls(line, &ld);
    }

    /* Pass 2 — extract deps, defs, calls */
    rewind(f); in_bc=0;
    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        if (!in_bc && strstr(line,"/*")) in_bc=1;
        if (in_bc) { if (strstr(line,"*/")) in_bc=0; continue; }
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if (starts_with(t,"import "))  parse_import_stmt(t, fe);
        if (strstr(t,"require("))      parse_require(t, fe);
        parse_global_refs(t, fe, &ld);
        parse_def(t, defs, def_count, max_defs, fe->name);
        parse_calls(t, fe);
    }

    fclose(f);
}
