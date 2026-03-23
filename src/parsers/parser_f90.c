/* parser_f90.c  -  Fortran dependency parser (F77 through F2018)
 *
 * Handles both:
 *   Free-format  (F90+): ! comments, no column restrictions
 *   Fixed-format (F77):  col 1=C,asterisk,excl -> comment, col 6=continuation
 *
 * USE module_name                     -> dep "module_name"
 * USE module_name, ONLY: sym1, sym2  -> dep "module_name"
 * USE, INTRINSIC :: iso_fortran_env   -> external intrinsic dep
 * INCLUDE 'other.f90'                 -> internal
 * cpp-include "c_header.h"            -> external
 * MODULE my_module                    -> def
 * SUBMODULE (parent) my_sub           -> def + dep parent
 * PROGRAM my_program                  -> def
 * SUBROUTINE name(args)               -> def
 * FUNCTION name(args)                 -> def
 * TYPE my_type                        -> def
 * CALL subroutine(args)               -> call
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_f90.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int swi(const char *s,const char *p){
    while(*p){if(tolower((unsigned char)*s)!=tolower((unsigned char)*p))return 0;s++;p++;}return 1;
}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static int ri(const char *s,char *o,int n){
    int i=0;
    while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<n-1)o[i++]=*s++;
    o[i]=0;return i;
}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){
    if(!v||!v[0])return;
    for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;
    if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);
}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){
    if(!f||!f[0])return;
    for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;
    if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}
}

/* Known Fortran intrinsic modules (always external) */
static int is_intrinsic(const char *m) {
    static const char *intr[]={
        "iso_fortran_env","iso_c_binding","ieee_arithmetic","ieee_exceptions",
        "ieee_features","intrinsic","omp_lib","mpi","mpi_f08","openacc",
        "hdf5","netcdf","lapack95","blas95",NULL
    };
    for(int i=0;intr[i];i++)if(!strcasecmp(intr[i],m))return 1;
    return 0;
}

/* ================================================================ USE module */

static void parse_use(const char *line, FileEntry *fe) {
    const char *p=ws(line);
    if(!swi(p,"USE")) return;
    p=ws(p+3);

    /* USE, INTRINSIC :: module or USE, NON_INTRINSIC :: module */
    int is_intrinsic_kw=0;
    if(*p==',') {
        p=ws(p+1);
        if(swi(p,"INTRINSIC"))is_intrinsic_kw=1;
        while(*p&&*p!=':') p++;
        if(*p==':'&&*(p+1)==':') p+=2;
        p=ws(p);
    }
    /* optional :: */
    if(*p==':'&&*(p+1)==':') p=ws(p+2);

    char mod[MAX_SYMBOL]={0}; ri(p,mod,sizeof(mod));
    if(!mod[0]) return;

    if(is_intrinsic_kw||is_intrinsic(mod)) {
        as(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
    } else {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",mod);
        as(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ INCLUDE */

static void parse_include(const char *line, FileEntry *fe) {
    const char *p=ws(line);
    if(!swi(p,"INCLUDE")) return;
    p=ws(p+7);

    /* INCLUDE 'file.f90' or INCLUDE "file.f90" */
    if(*p=='\''||*p=='"') {
        char q=*p++;
        char path[MAX_PATH]={0};int i=0;
        while(*p&&*p!=q&&i<MAX_PATH-1)path[i++]=*p++;
        path[i]=0;
        const char *base=path;
        for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
        char no_ext[MAX_SYMBOL]={0};strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.');if(dot)*dot=0;
        if(no_ext[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",no_ext);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
    }

    /* #include "c_header.h" */
    if(*p=='<'||*p=='"') {
        char q=(*p=='<')?'>':'"';p++;
        char hdr[MAX_SYMBOL]={0};int i=0;
        while(*p&&*p!=q&&*p!='.'&&i<MAX_SYMBOL-1)hdr[i++]=*p++;hdr[i]=0;
        if(hdr[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,hdr);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename, FileEntry *fe) {
    const char *p=ws(line);

    /* Skip prefix keywords: PURE RECURSIVE ELEMENTAL IMPURE MODULE */
    const char *pre[]={"PURE ","RECURSIVE ","ELEMENTAL ","IMPURE ","MODULE ","NON_RECURSIVE ",NULL};
    int changed=1;
    while(changed){changed=0;for(int i=0;pre[i];i++)if(swi(p,pre[i])){p=ws(p+strlen(pre[i]));changed=1;}}

    /* PROGRAM name */
    if(swi(p,"PROGRAM ")) {
        p=ws(p+8); char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
        if(nm[0])ad(defs,def_count,max_defs,nm,filename);
        return;
    }
    /* MODULE name (but not MODULE PROCEDURE) */
    if(swi(p,"MODULE ")) {
        p=ws(p+7);
        if(swi(p,"PROCEDURE")||swi(p,"SUBROUTINE")||swi(p,"FUNCTION")) return;
        char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
        if(nm[0])ad(defs,def_count,max_defs,nm,filename);
        return;
    }
    /* SUBMODULE (parent_module[:parent_submodule]) submodule_name */
    if(swi(p,"SUBMODULE ")) {
        p=ws(p+10);
        /* extract parent in (...) */
        if(*p=='(') {
            p++;char parent[MAX_SYMBOL]={0};ri(p,parent,sizeof(parent));
            if(parent[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",parent);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
            while(*p&&*p!=')')p++;if(*p)p++;
        }
        p=ws(p);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
        if(nm[0])ad(defs,def_count,max_defs,nm,filename);
        return;
    }
    /* SUBROUTINE name(...) */
    if(swi(p,"SUBROUTINE ")) {
        p=ws(p+11); char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
        if(nm[0])ad(defs,def_count,max_defs,nm,filename);
        return;
    }
    /* [type] FUNCTION name(...) */
    if(swi(p,"FUNCTION ")) {
        p=ws(p+9); char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
        if(nm[0])ad(defs,def_count,max_defs,nm,filename);
        return;
    }
    /* Detect function after type: REAL FUNCTION foo */
    {
        const char *ftypes[]={"INTEGER","REAL","DOUBLE","COMPLEX","LOGICAL","CHARACTER",
                              "TYPE(","CLASS(",NULL};
        for(int i=0;ftypes[i];i++){
            if(!swi(p,ftypes[i]))continue;
            const char *q=p+strlen(ftypes[i]);
            /* skip type params */
            if(*(ftypes[i]+strlen(ftypes[i])-1)=='('){while(*q&&*q!=')')q++;if(*q)q++;}
            q=ws(q);
            if(swi(q,"FUNCTION ")){q=ws(q+9);char nm[MAX_SYMBOL]={0};ri(q,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,filename);}
            return;
        }
    }
    /* TYPE :: name or TYPE name */
    if(swi(p,"TYPE ")) {
        p=ws(p+5);
        if(*p==':') return; /* TYPE :: var = declaration not def */
        if(swi(p,"(")) return; /* TYPE(something) = variable */
        char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
        if(nm[0]&&strcasecmp(nm,"IS")!=0)ad(defs,def_count,max_defs,nm,filename);
    }
    (void)fe;
}

/* ================================================================ CALL parsing */

static void parse_call(const char *line, FileEntry *fe) {
    const char *p=ws(line);
    if(!swi(p,"CALL ")) return;
    p=ws(p+5);
    char nm[MAX_SYMBOL]={0}; ri(p,nm,sizeof(nm));
    if(nm[0]) as(fe->calls,&fe->call_count,MAX_CALLS,nm);
}

/* ================================================================ Public API */

void parser_f90_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r"); if(!f)return;
    char line[MAX_LINE];

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* Fixed-format: col 0 = C, c, * -> comment */
        if(line[0]=='C'||line[0]=='c'||line[0]=='*') continue;
        /* Free-format: ! comment */
        char *cm=strchr(line,'!'); if(cm)*cm=0;
        /* #include */
        const char *t=ws(line); if(!*t)continue;

        if(swi(t,"USE"))            parse_use(t,fe);
        else if(swi(t,"INCLUDE"))   parse_include(t,fe);
        else if(*t=='#'&&swi(t+1,"include")) parse_include(t+1,fe);
        else if(swi(t,"CALL "))     parse_call(t,fe);
        else parse_def(t,defs,def_count,max_defs,fe->name,fe);
    }
    fclose(f);
}
