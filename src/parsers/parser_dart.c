/* parser_dart.c  -  Dart / Flutter dependency parser
 *
 * Import forms:
 *   import 'dart:core';                      SDK (external)
 *   import 'dart:async';                     SDK (external)
 *   import 'package:flutter/material.dart';  pub package (external)
 *   import 'package:http/http.dart' as http; pub package (external)
 *   import '../models/user.dart';            relative (internal)
 *   import './utils/helpers.dart';           relative (internal)
 *   export 'package:foo/bar.dart';           re-export (external)
 *   part 'user.g.dart';                      code-gen part (internal)
 *   part of '../base.dart';                  part of (internal)
 *
 * Definitions: class, abstract class, mixin, extension, enum,
 *              typedef, void func(), Future<T> func()
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_dart.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s,pre,strlen(pre))==0;
}
static const char *skip_ws(const char *s) {
    while (*s==' '||*s=='\t') s++; return s;
}
static int read_ident(const char *s, char *out, int out_size) {
    int i=0;
    while (*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='$')&&i<out_size-1)
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

/* ================================================================ Import parsing */

/* Extract quoted path from:  'path/to/file.dart'  or  "path/to/file.dart" */
static int extract_path(const char *s, char *out, int out_size) {
    while (*s&&*s!='\''&&*s!='"') s++;
    if (!*s) { out[0]='\0'; return 0; }
    char q=*s++; int i=0;
    while (*s&&*s!=q&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
}

static void parse_import(const char *line, FileEntry *fe) {
    /* handles: import / export / part / part of */
    const char *p=line;
    int is_part=0;

    if      (starts_with(p,"import "))   p=skip_ws(p+7);
    else if (starts_with(p,"export "))   p=skip_ws(p+7);
    else if (starts_with(p,"part of "))  { p=skip_ws(p+8); is_part=1; }
    else if (starts_with(p,"part "))     { p=skip_ws(p+5); is_part=1; }
    else return;

    char path[MAX_PATH]={0};
    if (!extract_path(p,path,sizeof(path))) return;

    if (starts_with(path,"dart:")) {
        /* dart:core, dart:async, dart:io, dart:convert → external */
        char mod[MAX_SYMBOL]={0};
        strncpy(mod, path+5, MAX_SYMBOL-1);
        /* keep only first segment: dart:html → "html" */
        char *slash=strchr(mod,'/'); if(slash)*slash='\0';
        char full[MAX_SYMBOL]={0};
        snprintf(full,sizeof(full),"dart:%s",mod);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,full);
    }
    else if (starts_with(path,"package:")) {
        /* package:flutter/material.dart → "flutter" */
        const char *pkg=path+8;
        char name[MAX_SYMBOL]={0}; int i=0;
        while (*pkg&&*pkg!='/'&&i<MAX_SYMBOL-1) name[i++]=*pkg++;
        name[i]='\0';
        if (name[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,name);
    }
    else {
        /* relative path → internal */
        const char *base=path;
        for (const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char no_ext[MAX_SYMBOL]={0};
        strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot) *dot='\0';
        if (no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
    }
    (void)is_part;
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip modifiers */
    const char *mods[]={"abstract ","final ","const ","static ","late ",
                        "external ","covariant ","required ","@override ",
                        "sealed ","base ","interface ",NULL};
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++)
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
    }

    /* class / mixin / extension / enum */
    const char *kws[]={"class ","mixin ","extension ","enum ","typedef ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* function: ReturnType funcName( or funcName( */
    /* heuristic: ident followed by optional <generics> then ( */
    char first[MAX_SYMBOL]={0}; int fl=read_ident(p,first,sizeof(first));
    if (!fl) return;
    const char *after=skip_ws(p+fl);
    /* skip generics */
    if (*after=='<'){int d=1;after++;while(*after&&d>0){if(*after=='<')d++;if(*after=='>')d--;after++;}after=skip_ws(after);}
    /* skip ? for nullable return type */
    if (*after=='?') after++;
    after=skip_ws(after);
    /* could be: ReturnType funcName( */
    char second[MAX_SYMBOL]={0}; int sl=read_ident(after,second,sizeof(second));
    if (sl>0) {
        const char *after2=skip_ws(after+sl);
        if (*after2=='(') {
            /* second is func name */
            if (second[0]&&islower((unsigned char)second[0]))
                add_def(defs,def_count,max_defs,second,filename);
        }
    } else if (*after=='(') {
        /* first is func name */
        if (first[0]&&islower((unsigned char)first[0]))
            add_def(defs,def_count,max_defs,first,filename);
    }
}

/* ================================================================ Call parsing */

static const char *DART_KW[]={
    "if","else","for","while","do","switch","case","default","return",
    "break","continue","try","catch","finally","throw","rethrow","new",
    "const","final","var","late","required","class","abstract","mixin",
    "extension","enum","typedef","import","export","part","library",
    "show","hide","as","is","in","null","true","false","this","super",
    "void","dynamic","Never","Object","Function","Type","Symbol","Null",
    "print","debugPrint","setState","build","initState","dispose",
    "mounted","context","widget","Navigator","Theme","Scaffold",NULL
};
static int is_kw(const char *s){
    for(int i=0;DART_KW[i];i++) if(strcmp(DART_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_'||*p=='$') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* skip ?. and . chains */
            while (*p=='.'||(*p=='?'&&*(p+1)=='.')) {
                p+=(*p=='?')?2:1;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if (*p=='('&&!is_kw(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_dart_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_dart] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        if (!in_bc&&strstr(line,"/*")) in_bc=1;
        if (in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if (starts_with(t,"import ")||starts_with(t,"export ")||
            starts_with(t,"part "))          parse_import(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
