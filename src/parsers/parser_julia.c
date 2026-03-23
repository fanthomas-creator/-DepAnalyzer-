/* parser_julia.c  -  Julia dependency parser
 *
 * Handles:
 *   using LinearAlgebra               → external "LinearAlgebra"
 *   using MyApp.Models                → root "MyApp" (local candidate)
 *   using Plots, Statistics           → multiple packages
 *   using DataFrames: DataFrame, nrow → selective import
 *   import Pkg                        → external
 *   import .Utils                     → internal (leading dot)
 *   import ..Parent.Mod               → internal (double dot)
 *   include("./utils.jl")             → internal
 *   function my_func(x, y)            → FunctionIndex
 *   function MyMod.method(x)          → FunctionIndex
 *   struct MyStruct / mutable struct  → FunctionIndex
 *   abstract type T                   → FunctionIndex
 *   primitive type T                  → FunctionIndex
 *   macro my_macro(args)              → FunctionIndex
 *   module MyModule ... end           → FunctionIndex
 *   const MY_CONST = value            → FunctionIndex
 *   obj.method() / Mod.func()         → calls
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_julia.h"
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
    /* Julia identifiers can include ! and ? at end */
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='!')&&i<out_size-1)
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

/* Known Julia stdlib modules (always external) */
static int is_stdlib(const char *m) {
    static const char *std[]={
        "Base","Core","Main","REPL","Pkg","LinearAlgebra","Statistics",
        "Random","Dates","Printf","Logging","Test","Distributed","Sockets",
        "Serialization","SharedArrays","SparseArrays","InteractiveUtils",
        "TOML","Profile","DelimitedFiles","FileWatching","Mmap",
        "Unicode","UUIDs","Downloads","LibGit2","SHA","Markdown",NULL
    };
    for(int i=0;std[i];i++) if(strcmp(std[i],m)==0) return 1;
    return 0;
}

/* Get root module: "MyApp.Models.User" → "MyApp" */
static void get_root(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&*s!='.'&&*s!=':'&&*s!=','&&*s!=' '&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
}

/* ================================================================ using / import */

static void store_module(const char *mod, FileEntry *fe) {
    if(!mod||!mod[0]) return;
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

static void parse_using(const char *line, FileEntry *fe) {
    const char *p=line;
    int is_import=0;

    if(starts_with(p,"using "))       { p=skip_ws(p+6); }
    else if(starts_with(p,"import ")) { p=skip_ws(p+7); is_import=1; }
    else return;

    /* skip relative dots: .Utils → internal, ..Parent → internal */
    while(*p=='.') { p++; }

    /* parse comma-separated list: using A, B, C */
    while(*p&&*p!='\n'&&*p!='#') {
        p=skip_ws(p);
        char mod[MAX_SYMBOL]={0}; int i=0;
        /* read dotted module name */
        while(*p&&(isalnum((unsigned char)*p)||*p=='.'||*p=='_')&&i<MAX_SYMBOL-1)
            mod[i++]=*p++;
        mod[i]='\0';

        if(mod[0]) store_module(mod,fe);

        /* skip selective imports: using X: a, b */
        if(*p==':') { while(*p&&*p!=','&&*p!='\n') p++; }

        p=skip_ws(p);
        if(*p==',') { p++; continue; }
        break;
    }
    (void)is_import;
}

/* ================================================================ include */

static void parse_include(const char *line, FileEntry *fe) {
    /* include("./path/file.jl") or include(joinpath(@__DIR__,"file.jl")) */
    const char *p=strstr(line,"include(");
    if(!p) return;
    if(p>line&&(isalnum((unsigned char)*(p-1))||*(p-1)=='_')) return;
    p+=8;

    /* find quoted path */
    while(*p&&*p!='"'&&*p!='\'') p++;
    if(!*p) return;
    char q=*p++; char path[MAX_PATH]={0}; int i=0;
    while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
    path[i]='\0';
    if(!path[0]) return;

    /* basename without extension */
    const char *base=path;
    for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
    char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
    if(no_ext[0]) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* function my_func(...) or function MyMod.method(...) */
    if(starts_with(p,"function ")) {
        p=skip_ws(p+9);
        /* skip module qualifier: MyMod.method → method */
        char nm[MAX_SYMBOL]={0}; int i=0;
        while(*p&&(isalnum((unsigned char)*p)||*p=='_'||*p=='.'||*p=='!')&&i<MAX_SYMBOL-1)
            nm[i++]=*p++;
        nm[i]='\0';
        /* get last component */
        char *dot=strrchr(nm,'.'); const char *func=dot?dot+1:nm;
        if(func[0]) add_def(defs,def_count,max_defs,func,filename);
        return;
    }

    /* struct / mutable struct */
    if(starts_with(p,"struct ")||starts_with(p,"mutable struct ")) {
        const char *q=starts_with(p,"mutable ")?p+15:p+7;
        q=skip_ws(q);
        char nm[MAX_SYMBOL]={0}; read_ident(q,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* abstract type / primitive type */
    if(starts_with(p,"abstract type ")||starts_with(p,"primitive type ")) {
        p+=starts_with(p,"abstract ")?14:15;
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* macro my_macro(...) */
    if(starts_with(p,"macro ")) {
        p=skip_ws(p+6);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* module MyModule */
    if(starts_with(p,"module ")) {
        p=skip_ws(p+7);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *JULIA_KW[]={
    "if","elseif","else","for","while","do","try","catch","finally",
    "return","break","continue","let","local","global","const","begin",
    "end","function","macro","struct","module","baremodule","using",
    "import","export","abstract","primitive","type","mutable","where",
    "nothing","true","false","missing","Inf","NaN","typeof","isa",
    "print","println","@show","error","throw","warn","@warn",
    "push!","pop!","append!","insert!","deleteat!","resize!",
    "length","size","ndims","axes","eachindex","enumerate","zip",NULL
};
static int is_kw(const char *s) {
    for(int i=0;JULIA_KW[i];i++) if(strcmp(JULIA_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while(*p) {
        /* skip macro calls: @macro → not a dep */
        if(*p=='@'){while(*p&&*p!=' '&&*p!='\t'&&*p!='(')p++;continue;}
        if(isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* . chaining */
            while(*p=='.') {
                p++;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if(sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if(*p=='('&&!is_kw(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_julia_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_julia] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0; /* #= ... =# */

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* Julia block comments: #= ... =# */
        if(!in_block_comment&&strstr(line,"#=")) in_block_comment=1;
        if(in_block_comment){if(strstr(line,"=#"))in_block_comment=0;continue;}
        /* strip # comments */
        char *cmt=strchr(line,'#'); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        parse_using(t,fe);
        parse_include(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
