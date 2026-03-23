/* parser_r.c  -  R language dependency parser
 *
 * Handles:
 *   library(ggplot2)              → external
 *   require(dplyr)                → external
 *   library("pkg")                → external
 *   require(pkg, quietly=TRUE)    → external
 *   source("./utils.R")           → internal
 *   source('../helpers/plot.R')   → internal
 *   foo <- function(x)            → FunctionIndex
 *   bar = function(x)             → FunctionIndex
 *   setGeneric("foo", ...)        → FunctionIndex (S4)
 *   setClass("MyClass", ...)      → FunctionIndex (S4)
 *   R6Class("ClassName", ...)     → FunctionIndex (R6)
 *   pkg::func() / pkg:::func()   → import pkg + call func
 *   func(args)                    → call
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_r.h"
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
    /* R identifiers can contain . */
    while (*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='.')&&i<out_size-1)
        out[i++]=*s++;
    out[i]='\0'; return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<*count;i++) if(strcmp(arr[i],val)==0) return;
    if (*count<max) strncpy(arr[(*count)++],val,MAX_SYMBOL-1);
}
static void add_def(FunctionDef *defs, int *count, int max,
                    const char *func, const char *file) {
    if (!func||!func[0]) return;
    for (int i=0;i<*count;i++)
        if(strcmp(defs[i].function,func)==0&&strcmp(defs[i].file,file)==0) return;
    if (*count<max) {
        strncpy(defs[*count].function,func,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* Extract first argument from func(arg, ...) — handles quoted and unquoted */
static int extract_first_arg(const char *s, char *out, int out_size) {
    /* find opening paren */
    while (*s&&*s!='(') s++;
    if (!*s) return 0;
    s=skip_ws(s+1);

    /* quoted */
    if (*s=='\''||*s=='"') {
        char q=*s++; int i=0;
        while (*s&&*s!=q&&i<out_size-1) out[i++]=*s++;
        out[i]='\0'; return i;
    }
    /* unquoted identifier */
    return read_ident(s,out,out_size);
}

/* ================================================================ library / require */

static void parse_library(const char *line, FileEntry *fe) {
    const char *kws[]={"library(","require(","requireNamespace(",NULL};
    for (int i=0;kws[i];i++) {
        const char *p=strstr(line,kws[i]); if(!p) continue;
        /* word boundary */
        if (p>line&&(isalnum((unsigned char)*(p-1))||*(p-1)=='_')) continue;
        char pkg[MAX_SYMBOL]={0};
        if (extract_first_arg(p,pkg,sizeof(pkg))&&pkg[0])
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,pkg);
        return;
    }
}

/* ================================================================ source() */

static void parse_source(const char *line, FileEntry *fe) {
    const char *p=strstr(line,"source(");
    if (!p) return;
    if (p>line&&(isalnum((unsigned char)*(p-1))||*(p-1)=='_')) return;
    char path[MAX_PATH]={0};
    if (!extract_first_arg(p,path,sizeof(path))) return;

    /* basename without extension */
    const char *base=path;
    for (const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
    char no_ext[MAX_SYMBOL]={0};
    strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
    if (no_ext[0]) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* foo <- function( or foo = function( */
    char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
    if (l>0&&nm[0]) {
        const char *after=skip_ws(p+l);
        int is_assign=0;
        if (starts_with(after,"<-")) { after=skip_ws(after+2); is_assign=1; }
        else if (*after=='=') { after=skip_ws(after+1); is_assign=1; }
        if (is_assign&&starts_with(after,"function("))
            add_def(defs,def_count,max_defs,nm,filename);
    }

    /* setGeneric("funcName", ...) */
    const char *s4kws[]={"setGeneric(","setMethod(","setClass(",NULL};
    for (int i=0;s4kws[i];i++) {
        const char *sp=strstr(line,s4kws[i]); if(!sp) continue;
        char name[MAX_SYMBOL]={0};
        if (extract_first_arg(sp,name,sizeof(name))&&name[0])
            add_def(defs,def_count,max_defs,name,filename);
        return;
    }

    /* R6Class("ClassName", ...) */
    const char *r6=strstr(line,"R6Class(");
    if (r6) {
        char name[MAX_SYMBOL]={0};
        if (extract_first_arg(r6,name,sizeof(name))&&name[0])
            add_def(defs,def_count,max_defs,name,filename);
    }
}

/* ================================================================ Call parsing
 * Special: pkg::func() and pkg:::func() → record pkg as import
 */

static const char *R_KW[]={
    "if","else","for","while","repeat","break","next","return","function",
    "in","TRUE","FALSE","NULL","NA","Inf","NaN","T","F",
    "library","require","source","print","cat","message","warning","stop",
    "c","list","data.frame","matrix","vector","array","factor",
    "length","nrow","ncol","dim","names","colnames","rownames",
    "which","any","all","sum","mean","max","min","range","var","sd",
    "paste","paste0","sprintf","format","substr","nchar","gsub","sub",
    "grepl","grep","strsplit","toupper","tolower","trimws",
    "read.csv","write.csv","readRDS","saveRDS","load","save",
    "lapply","sapply","vapply","mapply","tapply","apply",
    "Reduce","Filter","Map","Find","Position",NULL
};
static int is_kw(const char *s){
    for(int i=0;R_KW[i];i++) if(strcmp(R_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_'||*p=='.') {
            if (*p=='.'&&!isalpha((unsigned char)*(p+1))&&*(p+1)!='_') { p++; continue; }
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;

            /* pkg::func() or pkg:::func() → record pkg as import */
            if (p[0]==':'&&p[1]==':') {
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,ident);
                p+=2; if(*p==':') p++;
                char func[MAX_SYMBOL]={0}; int fl=read_ident(p,func,sizeof(func)); p+=fl;
                if (*p=='('&&func[0]&&!is_kw(func))
                    add_sym(fe->calls,&fe->call_count,MAX_CALLS,func);
                continue;
            }
            /* $ accessor: df$col — skip */
            if (*p=='$') { p++; char sk[MAX_SYMBOL]={0}; p+=read_ident(p,sk,sizeof(sk)); continue; }

            if (*p=='('&&!is_kw(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_r_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_r] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* strip # comments */
        char *cmt=strchr(line,'#');
        while (cmt) {
            /* not inside string (simplified) */
            int in_str=0; char sc=0;
            for (const char *c=line;c<cmt;c++){
                if (!in_str&&(*c=='\''||*c=='"')){in_str=1;sc=*c;}
                else if (in_str&&*c==sc) in_str=0;
            }
            if (!in_str){*cmt='\0';break;}
            cmt=strchr(cmt+1,'#');
        }
        const char *t=skip_ws(line); if(!*t) continue;

        parse_library(t,fe);
        parse_source(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
