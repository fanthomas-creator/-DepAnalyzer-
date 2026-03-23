/* parser_v.c  -  V (Vlang) dependency parser
 *
 * V uses dot-separated module paths where dots represent directories.
 *
 * Handles:
 *   import os                          → external stdlib "os"
 *   import net.http                    → external "net"
 *   import myapp.models                → internal candidate "myapp"
 *   import myapp.utils { helper }      → internal
 *   #include <stdio.h>                 → external C header
 *   fn my_func(x int) string          → FunctionIndex
 *   pub fn my_func(x int) string      → FunctionIndex
 *   fn (r &MyStruct) method() void    → FunctionIndex
 *   struct MyStruct { }               → FunctionIndex
 *   interface MyInterface { }         → FunctionIndex
 *   enum MyEnum { }                   → FunctionIndex
 *   type MyAlias = SomeType           → FunctionIndex
 *   const my_const = value            → FunctionIndex
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_v.h"
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

/* V stdlib modules */
static int is_stdlib(const char *m) {
    static const char *std[]={
        "os","io","net","http","json","math","rand","time","sync",
        "strings","strconv","arrays","maps","flag","log","term","cli",
        "crypto","hash","encoding","compress","regex","sqlite","orm",
        "db","vweb","gg","gl","gx","sokol","stbi","fontstash",
        "eventbus","filepath","builtin","v","runtime","semver",NULL
    };
    for(int i=0;std[i];i++) if(strcmp(std[i],m)==0) return 1;
    return 0;
}

/* ================================================================ import */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"import ")) return;
    p=skip_ws(p+7);

    /* read dotted path: myapp.models.user */
    char path[MAX_PATH]={0}; int i=0;
    while(*p&&*p!=' '&&*p!='\t'&&*p!='{'&&*p!='\n'&&i<MAX_PATH-1)
        path[i++]=*p++;
    path[i]='\0';
    if(!path[0]) return;

    /* get root component */
    char root[MAX_SYMBOL]={0}; int ri=0;
    const char *rp=path;
    while(*rp&&*rp!='.'&&ri<MAX_SYMBOL-1) root[ri++]=*rp++;
    root[ri]='\0';

    if(is_stdlib(root)) {
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
    } else {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",root);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* C includes: #include <stdio.h> or #include "my_header.h" */
static void parse_cinclude(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"#include")) return;
    p=skip_ws(p+8);
    int is_local=(*p=='"');
    if(*p=='<'||*p=='"') p++;
    char hdr[MAX_SYMBOL]={0}; int i=0;
    while(*p&&*p!='>'&&*p!='"'&&*p!='.'&&i<MAX_SYMBOL-1) hdr[i++]=*p++;
    hdr[i]='\0';
    if(hdr[0]) {
        if(is_local) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",hdr);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        } else {
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,hdr);
        }
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip pub / [attribute] */
    if(starts_with(p,"pub "))  p=skip_ws(p+4);
    if(starts_with(p,"["))     { while(*p&&*p!=']') p++; if(*p)p++; p=skip_ws(p); }
    if(starts_with(p,"pub "))  p=skip_ws(p+4);

    /* fn [receiver] name(...) */
    if(starts_with(p,"fn ")) {
        p=skip_ws(p+3);
        /* receiver: (r &MyStruct) */
        if(*p=='(') {
            while(*p&&*p!=')') p++;
            if(*p) p++;
            p=skip_ws(p);
        }
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* struct/interface/enum */
    const char *kws[]={"struct ","interface ","enum ","union ",NULL};
    for(int i=0;kws[i];i++) {
        if(!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* type alias */
    if(starts_with(p,"type ")) {
        p=skip_ws(p+5);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *V_KW[]={
    "if","else","for","in","match","return","break","continue","go","spawn",
    "defer","or","and","not","is","as","mut","const","var","fn","struct",
    "interface","enum","type","import","module","pub","__global","unsafe",
    "asm","nil","none","true","false","dump","println","print","eprintln",
    "exit","panic","error","assert","sizeof","typeof","isreftype",NULL
};
static int is_kw(const char *s) {
    for(int i=0;V_KW[i];i++) if(strcmp(V_KW[i],s)==0) return 1;
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

void parser_v_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_v] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0;

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        if(!in_bc&&strstr(line,"/*")) in_bc=1;
        if(in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if(starts_with(t,"#include")) parse_cinclude(t,fe);
        parse_import(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
