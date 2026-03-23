/* parser_cr.c  -  Crystal dependency parser
 *
 * Crystal is Ruby-like but compiled. Same require syntax as Ruby.
 *
 * Handles:
 *   require "http/server"              → external stdlib "http"
 *   require "json"                     → external "json"
 *   require "./models/user"            → internal (relative)
 *   require "../lib/utils"             → internal
 *   class Foo < Bar                    → def Foo, dep Bar
 *   struct Foo                         → FunctionIndex
 *   module Foo                         → FunctionIndex
 *   abstract class / abstract struct   → FunctionIndex
 *   def my_method(args)                → FunctionIndex
 *   def self.class_method(args)        → FunctionIndex
 *   include Comparable / Enumerable    → dep
 *   extend MyModule                    → dep
 *   annotation MyAnnotation            → FunctionIndex
 *   alias MyType = SomeType            → FunctionIndex
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_cr.h"
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
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='?'||*s=='!')&&i<out_size-1)
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

/* Crystal stdlib top-level shard names */
static int is_stdlib(const char *m) {
    static const char *std[]={
        "ameba","benchmark","compress","crystal","csv","db","digest",
        "ecr","email","fiber","file","http","ini","io","json","levenshtein",
        "log","mime","mutex","oauth","oauth2","openssl","option_parser",
        "pretty_print","process","random","regex","set","signal","socket",
        "spec","string_scanner","system","tempfile","time","toml",
        "uri","uuid","weak_ref","xml","yaml","zlib",NULL
    };
    for(int i=0;std[i];i++) if(strcmp(std[i],m)==0) return 1;
    return 0;
}

/* ================================================================ require */

static void parse_require(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"require ")) return;
    p=skip_ws(p+8);

    /* find quoted string */
    while(*p&&*p!='"'&&*p!='\'') p++;
    if(!*p) return;
    char q=*p++; char path[MAX_PATH]={0}; int i=0;
    while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
    path[i]='\0';
    if(!path[0]) return;

    /* relative path → internal */
    if(path[0]=='.'||path[0]=='/') {
        const char *base=path;
        for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
        if(no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
        return;
    }

    /* get first path component */
    char root[MAX_SYMBOL]={0}; int ri=0;
    const char *rp=path;
    while(*rp&&*rp!='/'&&ri<MAX_SYMBOL-1) root[ri++]=*rp++;
    root[ri]='\0';

    add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
    (void)is_stdlib;
}

/* ================================================================ mixin */

static void parse_mixin(const char *line, FileEntry *fe) {
    const char *kws[]={"include ","extend ","prepend ",NULL};
    const char *p=line;
    for(int i=0;kws[i];i++) {
        if(!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0};
        int j=0;
        while(*p&&(isalnum((unsigned char)*p)||*p=='_'||*p==':')&&j<MAX_SYMBOL-1)
            nm[j++]=*p++;
        nm[j]='\0';
        if(nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
        return;
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename, FileEntry *fe) {
    const char *p=skip_ws(line);

    /* skip modifiers */
    const char *mods[]={"abstract ","private ","protected ","public ",
                        "macro ","record ",NULL};
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++)
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
    }

    /* class/struct/module */
    const char *kws[]={"class ","struct ","module ","annotation ",NULL};
    for(int i=0;kws[i];i++) {
        if(!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        /* inheritance: class Foo < Bar */
        p+=strlen(nm); p=skip_ws(p);
        if(*p=='<') {
            p=skip_ws(p+1);
            char parent[MAX_SYMBOL]={0}; read_ident(p,parent,sizeof(parent));
            if(parent[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,parent);
        }
        return;
    }

    /* def my_method or def self.method */
    if(starts_with(p,"def ")) {
        p=skip_ws(p+4);
        if(starts_with(p,"self.")) p+=5;
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }

    /* alias */
    if(starts_with(p,"alias ")) {
        p=skip_ws(p+6);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *CR_KW[]={
    "if","unless","elsif","else","case","when","then","while","until",
    "for","in","do","end","return","break","next","begin","rescue",
    "ensure","raise","require","include","extend","prepend","class",
    "struct","module","def","macro","annotation","alias","abstract",
    "nil","true","false","self","super","typeof","sizeof","instance_sizeof",
    "offsetof","pointerof","as","as?","is_a?","responds_to?","nil?",
    "puts","print","p","pp","gets","exit","abort","sleep",NULL
};
static int is_kw(const char *s) {
    for(int i=0;CR_KW[i];i++) if(strcmp(CR_KW[i],s)==0) return 1;
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

void parser_cr_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_cr] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_heredoc=0;

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* heredoc: <<-HEREDOC ... HEREDOC */
        if(!in_heredoc&&strstr(line,"<<-")) { in_heredoc=1; continue; }
        if(in_heredoc) {
            const char *t=skip_ws(line);
            int all=1; for(const char *c=t;*c;c++) if(!isalnum((unsigned char)*c)&&*c!='_'){all=0;break;}
            if(all&&*t) in_heredoc=0;
            continue;
        }
        /* strip # comments */
        char *cmt=strchr(line,'#');
        while(cmt) {
            int in_str=0; char sc=0;
            for(const char *c=line;c<cmt;c++){
                if(!in_str&&(*c=='"'||*c=='\'')){in_str=1;sc=*c;}
                else if(in_str&&*c==sc) in_str=0;
            }
            if(!in_str){*cmt='\0';break;}
            cmt=strchr(cmt+1,'#');
        }
        const char *t=skip_ws(line); if(!*t) continue;

        parse_require(t,fe);
        parse_mixin(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name,fe);
        parse_calls(t,fe);
    }
    fclose(f);
}
