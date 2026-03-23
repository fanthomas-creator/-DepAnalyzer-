/* parser_proto.c  -  Protocol Buffer (.proto) dependency parser
 *
 * Handles proto2 and proto3:
 *   import "google/protobuf/timestamp.proto"  → external (google/*)
 *   import "other_service.proto"              → internal
 *   import weak / import public               → internal
 *   package com.example.service               → namespace
 *   message MyMessage { }                     → FunctionIndex
 *   service MyService { rpc ... }             → FunctionIndex
 *   enum Status { }                           → FunctionIndex
 *   rpc Foo (Request) returns (Response)      → FunctionIndex + calls
 *   extend .google.protobuf.FieldOptions      → dep
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_proto.h"
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
    while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<out_size-1)
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

/* ================================================================ Import parsing */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"import")) return;
    p=skip_ws(p+6);
    /* skip modifiers: weak / public */
    if(starts_with(p,"weak "))   p=skip_ws(p+5);
    if(starts_with(p,"public ")) p=skip_ws(p+7);

    /* find quoted path */
    while(*p&&*p!='"'&&*p!='\'') p++;
    if(!*p) return;
    char q=*p++; int i=0;
    char path[MAX_PATH]={0};
    while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
    path[i]='\0';

    /* check if it's a well-known google proto → external */
    if(starts_with(path,"google/")||starts_with(path,"google/protobuf/")) {
        /* store "google" as external dep */
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,"google");
        return;
    }

    /* relative / local proto file → internal */
    const char *base=path;
    for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
    char no_ext[MAX_SYMBOL]={0};
    strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
    if(no_ext[0]) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename, FileEntry *fe) {
    const char *p=skip_ws(line);

    /* message / service / enum */
    const char *kws[]={"message ","service ","enum ","extend ",NULL};
    for(int i=0;kws[i];i++) {
        if(!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) {
            if(starts_with(kws[i],"extend "))
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,nm);
            else
                add_def(defs,def_count,max_defs,nm,filename);
        }
        return;
    }

    /* rpc MethodName (RequestType) returns (ResponseType) */
    if(starts_with(p,"rpc ")) {
        p=skip_ws(p+4);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        p+=l;
        /* extract request and response types */
        int parens=0;
        while(*p) {
            if(*p=='(') { parens++; p++; p=skip_ws(p);
                char t[MAX_SYMBOL]={0}; read_ident(p,t,sizeof(t));
                /* skip "stream" keyword */
                if(strcmp(t,"stream")==0) { p+=6; p=skip_ws(p); read_ident(p,t,sizeof(t)); }
                if(t[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,t);
            }
            if(*p==')') parens--;
            p++;
        }
    }
}

/* ================================================================ Public API */

void parser_proto_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_proto] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0;

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        if(!in_bc&&strstr(line,"/*")) in_bc=1;
        if(in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if(starts_with(t,"import")) parse_import(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name,fe);
    }
    fclose(f);
}
