/* parser_ocaml.c  -  OCaml dependency parser
 *
 * Handles:
 *   open Printf                       → dep "Printf" (external)
 *   open MyApp.Models                 → root "MyApp" (local candidate)
 *   module M = Map.Make(String)       → dep "Map"
 *   module M = MyApp.Utils            → dep "MyApp"
 *   include Comparable                → dep
 *   let my_func args = ...            → FunctionIndex
 *   let rec my_func args = ...        → FunctionIndex
 *   type my_type = ...                → FunctionIndex
 *   exception MyExn                   → FunctionIndex
 *   class my_class = ...              → FunctionIndex
 *   external c_func : t = "c_name"   → external C symbol
 *   module MyModule = struct end      → FunctionIndex
 *   module type MYSIG = sig end       → FunctionIndex
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_ocaml.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s,pre,strlen(pre))==0;
}
static const char *skip_ws(const char *s) {
    while(*s==' '||*s=='\t') s++; return s;
}
static int read_ident(const char *s, char *out, int out_size) {
    int i=0;
    /* OCaml idents: letters, digits, _, ' (tick) */
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='\'')&&i<out_size-1)
        out[i++]=*s++;
    out[i]='\0'; return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if(!val||!val[0]) return;
    for(int i=0;i<*count;i++) if(strcmp(arr[i],val)==0) return;
    if(*count<max) strncpy(arr[(*count)++],val,MAX_SYMBOL-1);
}
static void add_def(FunctionDef *defs, int *count, int max,
                    const char *func, const char *file) {
    if(!func||!func[0]) return;
    for(int i=0;i<*count;i++)
        if(strcmp(defs[i].function,func)==0&&strcmp(defs[i].file,file)==0) return;
    if(*count<max) {
        strncpy(defs[*count].function,func,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* Known OCaml stdlib modules (always external) */
static int is_stdlib(const char *m) {
    static const char *std[]={
        "Stdlib","Printf","Scanf","Format","Buffer","String","Bytes","Char",
        "Int","Float","Bool","Unit","List","Array","Map","Set","Hashtbl",
        "Queue","Stack","Seq","Option","Result","Either","Random","Sys",
        "Filename","Unix","Thread","Mutex","Condition","Semaphore",
        "Bigarray","Gc","Lazy","Stream","Lexing","Parsing","Printexc",
        "Arg","Callback","Marshal","Obj","Digest","Base64","Re",
        "Lwt","Async","Core","Base","Ppx_jane","Yojson","Zarith",NULL
    };
    for(int i=0;std[i];i++) if(strcmp(std[i],m)==0) return 1;
    return 0;
}

/* Get root of dotted module path: "MyApp.Models.User" → "MyApp" */
static void get_root(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&*s!='.'&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
}

/* ================================================================ open / include */

static void parse_open(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"open ")) return;
    p=skip_ws(p+5);

    /* read dotted module path */
    char mod[MAX_SYMBOL]={0}; int i=0;
    while(*p&&(isalnum((unsigned char)*p)||*p=='.'||*p=='_')&&i<MAX_SYMBOL-1)
        mod[i++]=*p++;
    mod[i]='\0';
    if(!mod[0]) return;

    char root[MAX_SYMBOL]={0}; get_root(mod,root,sizeof(root));
    if(!root[0]) return;

    if(is_stdlib(root)) {
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
    } else {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",root);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

static void parse_include(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"include ")) return;
    p=skip_ws(p+8);
    char mod[MAX_SYMBOL]={0};
    read_ident(p,mod,sizeof(mod));
    if(mod[0]) {
        if(is_stdlib(mod)) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
        else {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",mod);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
    }
}

/* ================================================================ module aliases */

static void parse_module(const char *line, FileEntry *fe,
                          FunctionDef *defs, int *def_count, int max_defs) {
    const char *p=line;
    if(!starts_with(p,"module ")) return;
    p=skip_ws(p+7);

    /* module type MYSIG = ... */
    int is_type=0;
    if(starts_with(p,"type ")) { is_type=1; p=skip_ws(p+5); }

    /* module name */
    char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm)); p+=l;
    if(!nm[0]) return;

    /* def recorded in parse_def with proper filename */

    p=skip_ws(p);
    /* skip generic params (F : SIG) */
    if(*p=='(') { while(*p&&*p!=')') p++; if(*p) p++; p=skip_ws(p); }

    /* = ModulePath or = struct/sig */
    if(*p=='=') {
        p=skip_ws(p+1);
        if(!starts_with(p,"struct")&&!starts_with(p,"sig")&&!starts_with(p,"functor")) {
            /* module alias: module M = Foo.Bar */
            char rhs[MAX_SYMBOL]={0}; int i=0;
            while(*p&&(isalnum((unsigned char)*p)||*p=='.'||*p=='_')&&i<MAX_SYMBOL-1)
                rhs[i++]=*p++;
            rhs[i]='\0';
            char root[MAX_SYMBOL]={0}; get_root(rhs,root,sizeof(root));
            if(root[0]) {
                if(is_stdlib(root)) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
                else {
                    char tagged[MAX_SYMBOL]={0};
                    snprintf(tagged,sizeof(tagged),"local:%s",root);
                    add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
                }
            }
        }
    }
    (void)is_type;
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FileEntry *fe, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* let [rec] name ... = */
    if(starts_with(p,"let ")) {
        p=skip_ws(p+4);
        if(starts_with(p,"rec ")) p=skip_ws(p+4);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if(l&&nm[0]&&islower((unsigned char)nm[0]))
            add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* type my_type = ... */
    if(starts_with(p,"type ")) {
        p=skip_ws(p+5);
        /* skip nonrec / rec */
        if(starts_with(p,"nonrec ")) p=skip_ws(p+7);
        if(starts_with(p,"rec "))    p=skip_ws(p+4);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* exception MyExn */
    if(starts_with(p,"exception ")) {
        p=skip_ws(p+10);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* class my_class */
    if(starts_with(p,"class ")) {
        p=skip_ws(p+6);
        if(starts_with(p,"virtual ")) p=skip_ws(p+8);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* external c_func : type = "c_symbol" */
    if(starts_with(p,"external ")) {
        p=skip_ws(p+9);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        /* also record the C symbol name as external dep */
        const char *eq=strchr(p,'=');
        if(eq) {
            const char *q=eq+1;
            while(*q&&*q!='"'&&*q!='\'') q++;
            if(*q) {
                char csym[MAX_SYMBOL]={0}; char cq=*q++; int i=0;
                while(*q&&*q!=cq&&i<MAX_SYMBOL-1) csym[i++]=*q++;
                csym[i]='\0';
                if(csym[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,csym);
            }
        }
    }
}

/* ================================================================ Public API */

void parser_ocaml_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_ocaml] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0; /* (* ... *) block comments */

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* OCaml block comments: (* ... *) */
        if(!in_bc&&strstr(line,"(*")) in_bc=1;
        if(in_bc){if(strstr(line,"*)"))in_bc=0;continue;}

        const char *t=skip_ws(line); if(!*t) continue;

        if(starts_with(t,"open "))    parse_open(t,fe);
        if(starts_with(t,"include ")) parse_include(t,fe);
        if(starts_with(t,"module "))  parse_module(t,fe,defs,def_count,max_defs);
        parse_def(t,fe,defs,def_count,max_defs,fe->name);
    }
    fclose(f);
}
