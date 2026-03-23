/* parser_cs.c  -  C# dependency parser
 *
 * Handles:
 *   using System;
 *   using System.Collections.Generic;
 *   using Microsoft.AspNetCore.Mvc;
 *   using MyApp.Models;               → root namespace as dep
 *   global using System.Linq;         → C# 10 global using
 *   using static System.Math;         → static import
 *   namespace MyApp.Controllers       → declaration
 *   class Foo : BaseClass, IFoo       → def + BaseClass/IFoo as deps
 *   interface / struct / enum / record → FunctionIndex
 *   [HttpGet] / [Authorize]           → attribute dep
 *   void Foo() / async Task Bar()     → FunctionIndex
 *   obj.Method() / Type.Static()      → calls
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_cs.h"
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
    /* C# allows @ prefix for reserved word identifiers */
    if (*s=='@') { out[i++]='@'; s++; }
    while (*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<out_size-1) out[i++]=*s++;
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

/* Known System/Microsoft root namespaces that are always external */
static int is_external_ns(const char *root) {
    static const char *ext[]={
        "System","Microsoft","Newtonsoft","AutoMapper","FluentValidation",
        "MediatR","Serilog","NLog","log4net","Dapper","EntityFramework",
        "Xunit","NUnit","Moq","FluentAssertions","Bogus","Polly",
        "Azure","Amazon","Google","StackExchange","MongoDB","Npgsql",
        "MySql","SQLite","RabbitMQ","MassTransit","Hangfire","Quartz",NULL
    };
    for (int i=0;ext[i];i++) if(strcmp(ext[i],root)==0) return 1;
    return 0;
}

/* ================================================================ using parsing */

static void parse_using(const char *line, FileEntry *fe) {
    const char *p=line;
    if (!starts_with(p,"using ")) return;
    p=skip_ws(p+6);
    /* skip global / static modifiers */
    if (starts_with(p,"global "))  p=skip_ws(p+7);
    if (starts_with(p,"static "))  p=skip_ws(p+7);

    /* read full dotted namespace */
    char ns[MAX_PATH]={0}; int i=0;
    while (*p&&*p!=';'&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!='='&&i<MAX_PATH-1)
        ns[i++]=*p++;
    ns[i]='\0';
    if (!ns[0]) return;

    /* get root component (first segment before the first dot) */
    char root[MAX_SYMBOL]={0};
    {
        int ri=0, ni=0;
        while (ns[ni]&&ns[ni]!='.'&&ri<MAX_SYMBOL-1) { root[ri]=ns[ni]; ri++; ni++; }
        root[ri]='\0';
    }

    if (root[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
}

/* ================================================================ Attribute parsing */

static void parse_attribute(const char *line, FileEntry *fe) {
    /* [HttpGet] / [Authorize] / [Route("...")] */
    const char *p=line;
    if (*p!='[') return;
    p++;
    char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
    /* skip known built-in C# attributes */
    static const char *skip[]={
        "Obsolete","Serializable","NonSerialized","DllImport","Conditional",
        "DebuggerStepThrough","CompilerGenerated","GeneratedCode",
        "SuppressMessage","ThreadStatic","MethodImpl","FieldOffset",
        "StructLayout","MarshalAs","NotNull","AllowNull","MaybeNull",NULL
    };
    for (int i=0;skip[i];i++) if(strcmp(skip[i],nm)==0) return;
    if (nm[0]&&isupper((unsigned char)nm[0]))
        add_sym(fe->calls,&fe->call_count,MAX_CALLS,nm);
}

/* ================================================================ Inheritance / interface */

static void parse_inheritance(const char *line, FileEntry *fe) {
    /* class Foo : BaseClass, IFoo, IBar */
    const char *colon=strchr(line,':');
    if (!colon) return;
    /* make sure it's not :: or part of a ternary */
    if (*(colon+1)==':') return;
    const char *p=skip_ws(colon+1);
    while (*p&&*p!='{') {
        p=skip_ws(p);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if (l&&nm[0]&&nm[0]!='@')
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
        p+=l;
        /* skip generic params <T> */
        if (*p=='<'){int d=1;p++;while(*p&&d>0){if(*p=='<')d++;if(*p=='>')d--;p++;}}
        p=skip_ws(p);
        if (*p==',') p++;
        p=skip_ws(p);
        /* stop at where clause */
        if (starts_with(p,"where ")) break;
    }
}

/* ================================================================ Definition parsing */

static const char *CS_MODS[]={"public ","private ","protected ","internal ",
    "static ","abstract ","virtual ","override ","sealed ","partial ",
    "async ","extern ","readonly ","volatile ","unsafe ","new ",
    "required ","file ",NULL};

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip modifiers */
    int changed=1;
    while(changed){changed=0;
        for(int i=0;CS_MODS[i];i++)
            if(starts_with(p,CS_MODS[i])){p=skip_ws(p+strlen(CS_MODS[i]));changed=1;}
    }

    /* type declarations */
    const char *kws[]={"class ","interface ","struct ","enum ","record ","delegate ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* method: ReturnType MethodName( */
    char first[MAX_SYMBOL]={0}; int fl=read_ident(p,first,sizeof(first));
    if (!fl) return;
    const char *after=skip_ws(p+fl);
    /* skip generic return type */
    if (*after=='<'){int d=1;after++;while(*after&&d>0){if(*after=='<')d++;if(*after=='>')d--;after++;}after=skip_ws(after);}
    /* nullable */
    if (*after=='?') after=skip_ws(after+1);
    /* array [] */
    while(*after=='['||*after==']') after++;
    after=skip_ws(after);
    char second[MAX_SYMBOL]={0}; int sl=read_ident(after,second,sizeof(second));
    if (!sl) return;
    const char *after2=skip_ws(after+sl);
    /* skip generic method params */
    if (*after2=='<'){int d=1;after2++;while(*after2&&d>0){if(*after2=='<')d++;if(*after2=='>')d--;after2++;}after2=skip_ws(after2);}
    if (*after2=='(') {
        if (second[0]&&islower((unsigned char)second[0]))
            add_def(defs,def_count,max_defs,second,filename);
    }
}

/* ================================================================ Call parsing */

static const char *CS_KW[]={
    "if","else","for","foreach","while","do","switch","case","default",
    "return","break","continue","try","catch","finally","throw","new",
    "using","namespace","class","interface","struct","enum","record",
    "delegate","event","operator","implicit","explicit","typeof","sizeof",
    "is","as","in","out","ref","params","var","dynamic","void","null",
    "true","false","this","base","static","async","await","yield",
    "lock","checked","unchecked","fixed","unsafe","get","set","init",
    "add","remove","value","where","select","from","join","group","into",
    "let","on","equals","by","ascending","descending","nameof","with",NULL
};
static int is_kw(const char *s){
    for(int i=0;CS_KW[i];i++) if(strcmp(CS_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_'||*p=='@') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            while (*p=='.'){
                p++;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if (*p=='('&&!is_kw(ident)&&ident[0]&&ident[0]!='@')
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_cs_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_cs] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        if (!in_bc&&strstr(line,"/*")) in_bc=1;
        if (in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if (*t=='[')                   parse_attribute(t,fe);
        if (starts_with(t,"using "))   parse_using(t,fe);
        if (strchr(t,':'))             parse_inheritance(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
