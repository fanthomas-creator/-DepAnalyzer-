/* parser_nim.c  -  Nim dependency parser
 *
 * Handles:
 *   import strutils, sequtils, json    → external stdlib
 *   import ./utils                     → internal (dot-relative)
 *   import myapp/models                → internal (slash path)
 *   from strutils import split, join   → external
 *   include ./common                   → internal
 *   proc myProc(x: int): string        → FunctionIndex
 *   func myFunc(x: T): U               → FunctionIndex
 *   method myMethod(x: T): U           → FunctionIndex
 *   iterator myIter(x: T): U           → FunctionIndex
 *   macro myMacro(x: untyped): untyped → FunctionIndex
 *   template myTpl(x: untyped): untyped → FunctionIndex
 *   type MyType = object               → FunctionIndex
 *   type MyType = ref object           → FunctionIndex
 *   const MY_CONST =                   → FunctionIndex
 *   obj.method() / Mod.func()          → calls
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_nim.h"
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
    while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<out_size-1) out[i++]=*s++;
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

/* Store a single import token (handles internal vs external) */
static void store_import(const char *raw, FileEntry *fe) {
    if(!raw||!raw[0]) return;
    /* relative path: ./utils or myapp/models */
    if(raw[0]=='.'||strchr(raw,'/')) {
        const char *base=raw;
        for(const char *c=raw;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        /* strip leading dots */
        while(*base=='.') base++;
        char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
        if(no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
    } else {
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,raw);
    }
}

/* ================================================================ import / from / include */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    int is_from=0;

    if(starts_with(p,"import "))        p=skip_ws(p+7);
    else if(starts_with(p,"from "))   { p=skip_ws(p+5); is_from=1; }
    else if(starts_with(p,"include ")) {
        /* include ./common → internal */
        p=skip_ws(p+8);
        char mod[MAX_SYMBOL]={0}; int i=0;
        while(*p&&*p!=','&&*p!=' '&&*p!='\t'&&*p!='\n'&&i<MAX_SYMBOL-1) mod[i++]=*p++;
        mod[i]='\0';
        if(mod[0]) store_import(mod,fe);
        return;
    }
    else return;

    if(is_from) {
        /* from strutils import split → just record "strutils" */
        char mod[MAX_SYMBOL]={0}; int i=0;
        while(*p&&*p!=' '&&i<MAX_SYMBOL-1) mod[i++]=*p++;
        mod[i]='\0';
        store_import(mod,fe);
        return;
    }

    /* import a, b, c  (comma-separated, may have aliases: a as b) */
    while(*p&&*p!='\n'&&*p!='#') {
        p=skip_ws(p);
        char mod[MAX_SYMBOL]={0}; int i=0;
        while(*p&&*p!=','&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!='#'&&i<MAX_SYMBOL-1)
            mod[i++]=*p++;
        mod[i]='\0';
        if(mod[0]) store_import(mod,fe);
        /* skip alias: as X */
        p=skip_ws(p);
        if(starts_with(p,"as ")) { p+=3; while(*p&&*p!=' '&&*p!=',') p++; }
        p=skip_ws(p);
        if(*p==',') { p++; continue; }
        if(*p=='/') { /* import a / b (except clause) */ while(*p&&*p!='\n') p++; }
        break;
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* proc/func/method/iterator/macro/template */
    const char *kws[]={"proc ","func ","method ","iterator ","macro ","template ",NULL};
    for(int i=0;kws[i];i++) {
        if(!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        /* backtick operators: proc `+`(a, b) */
        if(*p=='`') { p++; while(*p&&*p!='`') p++; return; }
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* type MyType = ... */
    if(starts_with(p,"type ")) {
        p=skip_ws(p+5);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* const MY_CONST = (only uppercase names → avoid noise) */
    if(starts_with(p,"const ")) {
        p=skip_ws(p+6);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]&&isupper((unsigned char)nm[0]))
            add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *NIM_KW[]={
    "if","elif","else","case","of","while","for","in","do","try",
    "except","finally","raise","return","break","continue","yield",
    "proc","func","method","iterator","macro","template","converter",
    "type","const","let","var","block","import","from","export",
    "include","module","when","static","discard","addr","nil","true",
    "false","and","or","not","xor","shl","shr","div","mod","is","isnot",
    "in","notin","of","as","bind","mixin","defer","asm","cast",
    "echo","write","writeLine","read","readLine",NULL
};
static int is_kw(const char *s) {
    for(int i=0;NIM_KW[i];i++) if(strcmp(NIM_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while(*p) {
        if(isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
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

void parser_nim_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_nim] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* strip # comments */
        char *cmt=strchr(line,'#'); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        parse_import(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
