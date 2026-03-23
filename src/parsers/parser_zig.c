/* parser_zig.c  -  Zig dependency parser
 *
 * Handles:
 *   const std = @import("std")              → external "std"
 *   const utils = @import("./utils.zig")    → internal
 *   const mem = @import("std").mem          → external "std"
 *   @cImport({ @cInclude("stdio.h"); })     → external C dep
 *   pub fn myFunc(x: i32) void             → FunctionIndex
 *   fn helper(args: T) RetType             → FunctionIndex
 *   const MyStruct = struct { ... }        → FunctionIndex
 *   const MyEnum = enum { ... }            → FunctionIndex
 *   const MyUnion = union { ... }          → FunctionIndex
 *   const MyError = error { ... }          → FunctionIndex
 *   obj.method() / Ns.func()               → calls
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_zig.h"
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

/* Known Zig standard library root identifiers */
static int is_zig_stdlib(const char *m) {
    static const char *std[]={"std","builtin","root",NULL};
    for(int i=0;std[i];i++) if(strcmp(std[i],m)==0) return 1;
    return 0;
}

/* ================================================================ @import parsing */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;

    /* find @import("...") anywhere on line */
    while((p=strstr(p,"@import("))!=NULL) {
        p+=8;
        /* find quoted string */
        while(*p&&*p!='"'&&*p!='\'') p++;
        if(!*p) return;
        char q=*p++; char path[MAX_PATH]={0}; int i=0;
        while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
        path[i]='\0';
        if(!path[0]) continue;

        if(is_zig_stdlib(path)) {
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,path);
        } else if(path[0]=='.'||path[0]=='/') {
            /* relative path → internal */
            const char *base=path;
            for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
            char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
            char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
            if(no_ext[0]) {
                char tagged[MAX_SYMBOL]={0};
                snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
            }
        } else {
            /* package name (build.zig.zon deps) */
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,path);
        }
    }
}

/* @cImport / @cInclude → C dep */
static void parse_cimport(const char *line, FileEntry *fe) {
    const char *p=line;
    while((p=strstr(p,"@cInclude("))!=NULL) {
        p+=10;
        while(*p&&*p!='"'&&*p!='\'') p++;
        if(!*p) return;
        char q=*p++; char hdr[MAX_SYMBOL]={0}; int i=0;
        while(*p&&*p!=q&&*p!='.'&&i<MAX_SYMBOL-1) hdr[i++]=*p++;
        hdr[i]='\0';
        if(hdr[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,hdr);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip pub / export / extern / inline / noinline */
    const char *mods[]={"pub ","export ","extern ","inline ","noinline ",
                        "comptime ","threadlocal ",NULL};
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++)
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
    }

    /* fn myFunc(...) */
    if(starts_with(p,"fn ")) {
        p=skip_ws(p+3);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* const Name = struct/enum/union/error */
    if(starts_with(p,"const ")||starts_with(p,"var ")) {
        p=skip_ws(p+6);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if(!l) return;
        p=skip_ws(p+l);
        if(*p!='=') return;
        const char *rhs=skip_ws(p+1);
        /* struct/enum/union/error type definition */
        if(starts_with(rhs,"struct ")||starts_with(rhs,"enum ")||
           starts_with(rhs,"union ")||starts_with(rhs,"error ")||
           starts_with(rhs,"struct{")||starts_with(rhs,"enum{")||
           starts_with(rhs,"union{")) {
            if(nm[0]&&isupper((unsigned char)nm[0]))
                add_def(defs,def_count,max_defs,nm,filename);
        }
    }
}

/* ================================================================ Call parsing */

static const char *ZIG_KW[]={
    "if","else","while","for","switch","return","break","continue",
    "defer","errdefer","try","catch","unreachable","undefined","null",
    "true","false","comptime","const","var","fn","pub","extern","export",
    "inline","noinline","struct","enum","union","error","anytype","void",
    "bool","i8","i16","i32","i64","i128","u8","u16","u32","u64","u128",
    "f16","f32","f64","f128","usize","isize","anyerror","noreturn",
    "type","anyframe","async","await","suspend","resume","nosuspend",NULL
};
static int is_kw(const char *s) {
    for(int i=0;ZIG_KW[i];i++) if(strcmp(ZIG_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while(*p) {
        /* skip builtins: @builtin */
        if(*p=='@'){while(*p&&*p!='(')p++;continue;}
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

void parser_zig_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_zig] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0;

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* Zig uses // comments only, no block comments */
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        (void)in_bc;
        const char *t=skip_ws(line); if(!*t) continue;

        parse_import(t,fe);
        parse_cimport(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
