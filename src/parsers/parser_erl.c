/* parser_erl.c  -  Erlang dependency parser
 *
 * Handles:
 *   -module(my_module).
 *   -behaviour(gen_server).           -> OTP behaviour dep
 *   -include("header.hrl").           -> internal
 *   -include_lib("crypto/include/crypto.hrl"). -> external "crypto"
 *   -import(lists, [map/2]).          -> external dep
 *   -export([func/1, func2/2]).       -> exported functions def
 *   func(Args) -> Body.               -> FunctionIndex
 *   -record(name, {...}).             -> FunctionIndex
 *   -type name() :: ...               -> FunctionIndex
 *   Module:function(args)             -> call + dep "Module"
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_erl.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
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

/* Extract atom (unquoted or 'quoted') from s into o */
static int read_atom(const char *s, char *o, int n) {
    if (*s == '\'') {
        s++; int i=0;
        while(*s&&*s!='\''&&i<n-1)o[i++]=*s++;
        o[i]=0; return i;
    }
    int i=0;
    while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<n-1)o[i++]=*s++;
    o[i]=0; return i;
}

/* Known Erlang/OTP stdlib apps (always external) */
static int is_otp(const char *m) {
    static const char *otp[]={
        "gen_server","gen_statem","gen_event","gen_fsm","supervisor",
        "application","proc_lib","sys","timer","lists","maps","sets",
        "dict","gb_trees","gb_sets","array","queue","ordsets","orddict",
        "string","binary","unicode","io","io_lib","file","filename",
        "filelib","os","erlang","math","crypto","ssl","ssh","inet",
        "gen_tcp","gen_udp","ets","dets","mnesia","logger","error_logger",
        "code","rpc","net_adm","global","kernel","stdlib","sasl",
        "wx","wx_object","httpc","httpc","xmerl","re","rand","base64",
        "uri_string","calendar","proplists","lists","maps","ets",NULL
    };
    for(int i=0;otp[i];i++)if(!strcmp(otp[i],m))return 1;
    return 0;
}

/* ================================================================ Attribute parsing */

static void parse_attribute(const char *line, FileEntry *fe,
                             FunctionDef *defs, int *def_count, int max_defs) {
    const char *p=line+1; /* skip - */

    /* -behaviour(gen_server). / -behavior(...). */
    if(sw(p,"behaviour(")||sw(p,"behavior(")) {
        p=strchr(p,'(')+1;
        char nm[MAX_SYMBOL]={0};read_atom(p,nm,sizeof(nm));
        if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
        return;
    }

    /* -include("file.hrl"). -> internal */
    if(sw(p,"include(\"")) {
        p+=9;
        char path[MAX_PATH]={0};int i=0;
        while(*p&&*p!='"'&&i<MAX_PATH-1)path[i++]=*p++;path[i]=0;
        const char *base=path;
        for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
        char no_ext[MAX_SYMBOL]={0};strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.');if(dot)*dot=0;
        if(no_ext[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",no_ext);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
        return;
    }

    /* -include_lib("app/include/file.hrl"). -> external "app" */
    if(sw(p,"include_lib(\"")) {
        p+=13;
        char root[MAX_SYMBOL]={0};int i=0;
        while(*p&&*p!='/'&&*p!='"'&&i<MAX_SYMBOL-1)root[i++]=*p++;root[i]=0;
        if(root[0]&&is_otp(root))as(fe->imports,&fe->import_count,MAX_IMPORTS,root);
        else if(root[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,root);
        return;
    }

    /* -import(Module, [func/arity, ...]). */
    if(sw(p,"import(")) {
        p+=7;
        char mod[MAX_SYMBOL]={0};read_atom(p,mod,sizeof(mod));
        if(mod[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
        return;
    }

    /* -export([func/1, func2/2]). -> defs */
    if(sw(p,"export(")) {
        const char *q=strchr(p,'[');if(!q)return;q++;
        while(*q&&*q!=']'){
            q=ws(q);
            char nm[MAX_SYMBOL]={0};int l=read_atom(q,nm,sizeof(nm));q+=l;
            /* skip /arity */
            if(*q=='/')while(isdigit((unsigned char)*q)||*q=='/')q++;
            if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);
            while(*q&&*q!=','&&*q!=']')q++;
            if(*q==',')q++;
        }
        return;
    }

    /* -record(name, {...}). */
    if(sw(p,"record(")) {
        p+=7;
        char nm[MAX_SYMBOL]={0};read_atom(p,nm,sizeof(nm));
        if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);
        return;
    }

    /* -type name() :: ... / -spec name(...) -> ... */
    if(sw(p,"type ")||sw(p,"spec ")) {
        p+=5;p=ws(p);
        char nm[MAX_SYMBOL]={0};read_atom(p,nm,sizeof(nm));
        if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);
    }
}

/* ================================================================ Function def parsing */

/* Erlang function: starts at column 0 with lowercase atom followed by ( */
static void parse_funcdef(const char *line, FunctionDef *defs,
                           int *def_count, int max_defs, const char *filename) {
    if(line[0]==' '||line[0]=='\t'||line[0]=='-'||line[0]=='%') return;
    char nm[MAX_SYMBOL]={0}; int l=ri(line,nm,sizeof(nm));
    if(!l||!nm[0]||!islower((unsigned char)nm[0])) return;
    const char *p=ws(line+l);
    if(*p=='(') ad(defs,def_count,max_defs,nm,filename);
}

/* ================================================================ Call parsing */

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while(*p) {
        if(isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=ri(p,ident,sizeof(ident)); p+=l;
            /* Module:function(args) -> dep + call */
            if(*p==':') {
                p++;
                char func[MAX_SYMBOL]={0}; int fl=ri(p,func,sizeof(func)); p+=fl;
                if(ident[0]&&!is_otp(ident))
                    as(fe->imports,&fe->import_count,MAX_IMPORTS,ident);
                if(func[0]&&*p=='(')
                    as(fe->calls,&fe->call_count,MAX_CALLS,func);
            } else if(*p=='('&&ident[0]) {
                as(fe->calls,&fe->call_count,MAX_CALLS,ident);
            }
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_erl_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r"); if(!f)return;
    char line[MAX_LINE];

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* strip % comments */
        char *cm=strchr(line,'%'); if(cm)*cm=0;
        const char *t=ws(line); if(!*t)continue;

        if(*t=='-') parse_attribute(t,fe,defs,def_count,max_defs);
        else { parse_funcdef(t,defs,def_count,max_defs,fe->name); parse_calls(t,fe); }
    }
    fclose(f);
}
