/* parser_ts.c  –  TypeScript parser
 *
 * Extends JS detection with TS-specific constructs :
 *   import type { Foo }  from './foo'
 *   import './side-effect'
 *   interface Foo { }         → FunctionIndex (reused for type defs)
 *   enum Direction { }
 *   type Alias = string
 *   abstract class Foo { }
 *   export default class / function / const
 *   decorators @Decorator
 *   path aliases  @app/services/auth  (tsconfig paths)
 *
 * Also inherits full JS logic:
 *   require(), browser globals, arrow functions, etc.
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_ts.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================
 * Helpers
 * ================================================================ */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}
static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}
static int read_ident(const char *s, char *out, int out_size) {
    int i = 0;
    /* TS identifiers can start with @ (decorators) */
    if (*s == '@') { out[i++] = *s++; }
    while (*s && (isalnum((unsigned char)*s) || *s=='_' || *s=='$' || *s=='/') && i < out_size-1)
        out[i++] = *s++;
    out[i] = '\0';
    return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val || !val[0]) return;
    for (int i=0; i<*count; i++) if (strcmp(arr[i],val)==0) return;
    if (*count < max) strncpy(arr[(*count)++], val, MAX_SYMBOL-1);
}
static void add_def(FunctionDef *defs, int *count, int max,
                    const char *func, const char *file) {
    if (!func||!func[0]) return;
    for (int i=0; i<*count; i++)
        if (strcmp(defs[i].function,func)==0 && strcmp(defs[i].file,file)==0) return;
    if (*count < max) {
        strncpy(defs[*count].function, func, MAX_SYMBOL-1);
        strncpy(defs[*count].file,     file, MAX_NAME-1);
        (*count)++;
    }
}

/* ================================================================
 * TS / JS keywords + browser globals to ignore as imports
 * ================================================================ */
static const char *IGNORE[] = {
    "if","else","for","while","do","switch","case","default","return",
    "typeof","instanceof","new","delete","void","throw","catch","finally",
    "try","import","export","from","class","extends","super","this","self",
    "const","let","var","function","async","await","of","in","debugger",
    "break","continue","yield","static","get","set","null","undefined",
    "true","false","NaN","Infinity","arguments",
    /* TS specific */
    "type","interface","enum","abstract","implements","declare","namespace",
    "module","readonly","override","as","satisfies","keyof","typeof",
    "infer","never","unknown","any","object","string","number","boolean",
    "bigint","symbol","void",
    /* Built-ins */
    "console","window","document","navigator","Math","JSON","Date",
    "Array","Object","String","Number","Boolean","Promise","Error",
    "Map","Set","Symbol","Proxy","Reflect","parseInt","parseFloat",
    "setTimeout","setInterval","clearTimeout","clearInterval",
    "fetch","XMLHttpRequest","WebSocket","URL","URLSearchParams",
    "localStorage","sessionStorage","alert","confirm","prompt",
    "requestAnimationFrame","ArrayBuffer","Uint8Array","TextEncoder",
    "TextDecoder","Blob","File","FileReader","FormData","Event",
    "CustomEvent","EventTarget","AbortController","Worker","process",
    "Buffer","global","module","exports","require","__dirname","__filename",
    NULL
};
static int is_ignored(const char *s) {
    for (int i=0; IGNORE[i]; i++) if (strcmp(IGNORE[i],s)==0) return 1;
    return 0;
}

/* ================================================================
 * Local-declaration tracking (prevent false-positive globals)
 * ================================================================ */
#define MAX_LOCAL 512
typedef struct { char n[MAX_LOCAL][MAX_SYMBOL]; int c; } LocalDecls;

static void ld_add(LocalDecls *ld, const char *name) {
    if (!name||!name[0]) return;
    for (int i=0;i<ld->c;i++) if(strcmp(ld->n[i],name)==0) return;
    if (ld->c < MAX_LOCAL) strncpy(ld->n[ld->c++], name, MAX_SYMBOL-1);
}
static int ld_has(const LocalDecls *ld, const char *name) {
    for (int i=0;i<ld->c;i++) if(strcmp(ld->n[i],name)==0) return 1;
    return 0;
}

static void collect_decls(const char *line, LocalDecls *ld) {
    const char *p;

    /* class Foo / abstract class Foo */
    p = strstr(line, "class ");
    if (p && (p==line||!isalnum((unsigned char)*(p-1)))) {
        char nm[MAX_SYMBOL]={0}; read_ident(skip_ws(p+6),nm,sizeof(nm)); ld_add(ld,nm);
    }
    /* interface Foo */
    p = strstr(line, "interface ");
    if (p && (p==line||!isalnum((unsigned char)*(p-1)))) {
        char nm[MAX_SYMBOL]={0}; read_ident(skip_ws(p+10),nm,sizeof(nm)); ld_add(ld,nm);
    }
    /* enum Foo */
    p = strstr(line, "enum ");
    if (p && (p==line||!isalnum((unsigned char)*(p-1)))) {
        char nm[MAX_SYMBOL]={0}; read_ident(skip_ws(p+5),nm,sizeof(nm)); ld_add(ld,nm);
    }
    /* type Foo = */
    p = strstr(line, "type ");
    if (p && (p==line||!isalnum((unsigned char)*(p-1)))) {
        char nm[MAX_SYMBOL]={0}; read_ident(skip_ws(p+5),nm,sizeof(nm)); ld_add(ld,nm);
    }
    /* function foo */
    p = strstr(line, "function ");
    if (p && (p==line||!isalnum((unsigned char)*(p-1)))) {
        p=skip_ws(p+9); if(*p!='('){char nm[MAX_SYMBOL]={0};read_ident(p,nm,sizeof(nm));ld_add(ld,nm);}
    }
    /* const/let/var foo  or  const { a, b } */
    const char *kws[]={"const ","let ","var ",NULL};
    for (int i=0;kws[i];i++) {
        const char *q=line;
        while ((q=strstr(q,kws[i]))!=NULL) {
            if (q>line&&isalnum((unsigned char)*(q-1))){q++;continue;}
            q=skip_ws(q+strlen(kws[i]));
            if (*q=='{') {
                q++;
                while (*q&&*q!='}') {
                    q=skip_ws(q);
                    char nm[MAX_SYMBOL]={0}; int l=read_ident(q,nm,sizeof(nm));
                    if (l){ld_add(ld,nm);q+=l;}
                    while (*q&&*q!=','&&*q!='}') q++;
                    if (*q==',') q++;
                }
            } else {
                char nm[MAX_SYMBOL]={0}; read_ident(q,nm,sizeof(nm)); ld_add(ld,nm);
            }
            q++;
        }
    }
}

/* ================================================================
 * Module path extraction from quoted string
 * ================================================================ */
static void extract_module(const char *s, char *out, int out_size) {
    while (*s && *s!='"' && *s!='\'') s++;
    if (!*s) {out[0]='\0';return;}
    char q=*s++;
    int i=0, is_rel=0;
    if (*s=='.') { is_rel=1; while (*s=='.'||*s=='/'||*s=='\\') s++; }
    /* path alias like @app/services → keep full as identifier */
    if (!is_rel && *s=='@') {
        while (*s&&*s!=q&&i<out_size-1) out[i++]=*s++;
        out[i]='\0';
        /* strip extension */
        char *dot=strrchr(out,'.'); if(dot) *dot='\0';
        return;
    }
    while (*s&&*s!=q&&*s!='/'&&*s!='\\'&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
    char *dot=strrchr(out,'.');
    if (dot&&(strcmp(dot,".js")==0||strcmp(dot,".ts")==0||
              strcmp(dot,".jsx")==0||strcmp(dot,".tsx")==0)) *dot='\0';
}

/* ================================================================
 * Import parsing
 * ================================================================ */
static void parse_import(const char *line, FileEntry *fe) {
    /* import type { Foo } from './foo'  → still extract module */
    /* import './side-effect'            → extract module        */
    /* import foo from './foo'           → extract module        */
    char mod[MAX_SYMBOL]={0};
    extract_module(line, mod, sizeof(mod));
    if (mod[0]) add_sym(fe->imports, &fe->import_count, MAX_IMPORTS, mod);
}

static void parse_require(const char *line, FileEntry *fe) {
    const char *p=strstr(line,"require("); if(!p) return;
    char mod[MAX_SYMBOL]={0}; extract_module(p+7,mod,sizeof(mod));
    if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
}

/* ================================================================
 * Definition parsing  (class / interface / enum / function / type)
 * ================================================================ */
static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *kw_types[]={ "class ","interface ","enum ","function ",NULL };
    int         kw_lens[] ={ 6,       10,          5,      9          };

    for (int k=0; kw_types[k]; k++) {
        const char *p=strstr(line, kw_types[k]);
        if (!p||(p>line&&isalnum((unsigned char)*(p-1)))) continue;
        p=skip_ws(p+kw_lens[k]);
        if (*p=='(') continue; /* anonymous function */
        /* skip generic params: class Foo<T> → stop at < */
        char nm[MAX_SYMBOL]={0}; int i=0;
        while (*p&&(isalnum((unsigned char)*p)||*p=='_'||*p=='$')&&i<MAX_SYMBOL-1)
            nm[i++]=*p++;
        nm[i]='\0';
        if (nm[0]&&!is_ignored(nm))
            add_def(defs,def_count,max_defs,nm,filename);
    }

    /* type Alias = ... */
    const char *p=strstr(line,"type ");
    if (p&&(p==line||!isalnum((unsigned char)*(p-1)))) {
        p=skip_ws(p+5);
        char nm[MAX_SYMBOL]={0}; int i=0;
        while (*p&&(isalnum((unsigned char)*p)||*p=='_')&&i<MAX_SYMBOL-1) nm[i++]=*p++;
        nm[i]='\0';
        const char *after=skip_ws(p);
        if (*after=='='&&nm[0]&&!is_ignored(nm))
            add_def(defs,def_count,max_defs,nm,filename);
    }

    /* const/let foo = () => or = function */
    const char *kws[]={"const ","let ","var ",NULL};
    for (int i=0;kws[i];i++) {
        p=strstr(line,kws[i]);
        if (!p||(p>line&&isalnum((unsigned char)*(p-1)))) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if (!l) continue;
        p=skip_ws(p+l);
        if (*p!='=') continue;
        const char *rhs=p+1;
        if (strstr(rhs,"=>")||strstr(rhs,"function"))
            add_def(defs,def_count,max_defs,nm,filename);
    }

    /* class method: [async] [public|private|protected|static] methodName( */
    p=skip_ws(line);
    const char *mods[]={"async ","public ","private ","protected ","static ","readonly ","abstract ","override ",NULL};
    int changed=1;
    while (changed) { changed=0;
        for (int i=0;mods[i];i++) {
            if (starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
        }
    }
    char mn[MAX_SYMBOL]={0}; int ml=read_ident(p,mn,sizeof(mn));
    if (ml>0&&mn[0]!='@'&&!is_ignored(mn)) {
        const char *after=skip_ws(p+ml);
        if (*after=='('||*after=='<')
            add_def(defs,def_count,max_defs,mn,filename);
    }
}

/* ================================================================
 * Global-reference detection  (root.something not declared locally)
 * ================================================================ */
static void parse_global_refs(const char *line, FileEntry *fe,
                               const LocalDecls *ld) {
    const char *p=line;
    while (*p) {
        if (!(isalpha((unsigned char)*p)||*p=='_'||*p=='$'||*p=='@')){p++;continue;}
        if (p>line&&(isalnum((unsigned char)*(p-1))||*(p-1)=='_'||*(p-1)=='$')){p++;continue;}
        if (p>line&&*(p-1)=='.'){char sk[MAX_SYMBOL]={0};p+=read_ident(p,sk,sizeof(sk));continue;}

        char root[MAX_SYMBOL]={0};
        int rlen=read_ident(p,root,sizeof(root));
        const char *after=p+rlen;

        /* decorator @Something → treat as external dep */
        if (root[0]=='@') {
            char bare[MAX_SYMBOL]; strncpy(bare,root+1,MAX_SYMBOL-1);
            if (!is_ignored(bare)&&!ld_has(ld,bare))
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,bare);
            p=after; continue;
        }

        if (*after=='.'&&!is_ignored(root)&&!ld_has(ld,root))
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
        p=after;
    }
}

/* ================================================================
 * Call parsing
 * ================================================================ */
static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_'||*p=='$') {
            char ident[MAX_SYMBOL]={0}; int len=read_ident(p,ident,sizeof(ident)); p+=len;
            while (*p=='.'){p++;char sub[MAX_SYMBOL]={0};int sl=read_ident(p,sub,sizeof(sub));p+=sl;if(sl)strncpy(ident,sub,MAX_SYMBOL-1);}
            if (*p=='('&&!is_ignored(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================
 * Public API — two-pass
 * ================================================================ */
void parser_ts_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_ts] Cannot open: %s\n",fe->path);return;}

    LocalDecls ld; memset(&ld,0,sizeof(ld));
    char line[MAX_LINE];
    int in_bc=0;

    /* Pass 1 — collect local declarations */
    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        if (!in_bc&&strstr(line,"/*")) in_bc=1;
        if (in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cmt=strstr(line,"//");if(cmt)*cmt='\0';
        collect_decls(line,&ld);
    }

    /* Pass 2 — extract deps, defs, calls */
    rewind(f); in_bc=0;
    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        if (!in_bc&&strstr(line,"/*")) in_bc=1;
        if (in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cmt=strstr(line,"//");if(cmt)*cmt='\0';
        const char *t=skip_ws(line);if(!*t)continue;

        if (starts_with(t,"import "))   parse_import(t,fe);
        if (strstr(t,"require("))       parse_require(t,fe);
        parse_global_refs(t,fe,&ld);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
