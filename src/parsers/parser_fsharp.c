/* parser_fsharp.c  -  F# dependency parser
 *
 * Handles:
 *   open System.Collections.Generic   → root "System" (external)
 *   open MyApp.Models                 → root "MyApp" (local candidate)
 *   #load "./utils.fsx"               → internal file
 *   #r "nuget: Newtonsoft.Json, 13.0" → external NuGet "Newtonsoft.Json"
 *   #r "./bin/MyLib.dll"              → internal binary ref
 *   module MyModule =                 → FunctionIndex
 *   let myFunc args =                 → FunctionIndex
 *   let rec myFunc args =             → FunctionIndex
 *   type MyType =                     → FunctionIndex
 *   exception MyExn                   → FunctionIndex
 *   [<SomeAttribute>]                 → attribute dep
 *   inherit BaseClass()               → dep
 *   interface IFoo with               → dep
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_fsharp.h"
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
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='\'')&&i<out_size-1)
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

/* Known .NET/F# external root namespaces */
static int is_external_ns(const char *root) {
    static const char *ext[]={
        "System","Microsoft","Newtonsoft","FSharp","Xunit","NUnit",
        "Moq","Serilog","Dapper","EntityFramework","Azure","Amazon",
        "Google","StackExchange","MongoDB","Npgsql","MySql","SQLite",
        "RabbitMQ","MassTransit","Polly","AutoMapper","MediatR",
        "FluentValidation","Bogus","Suave","Giraffe","Saturn","Fable",NULL
    };
    for(int i=0;ext[i];i++) if(strcmp(ext[i],root)==0) return 1;
    return 0;
}

/* Get root namespace: "System.Collections.Generic" → "System" */
static void get_root(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&*s!='.'&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
}

/* ================================================================ open */

static void parse_open(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"open ")) return;
    p=skip_ws(p+5);

    char mod[MAX_SYMBOL]={0}; int i=0;
    while(*p&&(isalnum((unsigned char)*p)||*p=='.'||*p=='_')&&i<MAX_SYMBOL-1)
        mod[i++]=*p++;
    mod[i]='\0';
    if(!mod[0]) return;

    char root[MAX_SYMBOL]={0}; get_root(mod,root,sizeof(root));
    if(!root[0]) return;

    if(is_external_ns(root))
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
    else {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",root);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ #load / #r */

static void parse_directive(const char *line, FileEntry *fe) {
    const char *p=line;
    if(*p!='#') return;
    p++;

    /* #load "./utils.fsx" → internal file */
    if(starts_with(p,"load ")) {
        p=skip_ws(p+5);
        while(*p&&*p!='"'&&*p!='\'') p++;
        if(!*p) return;
        char q=*p++; char path[MAX_PATH]={0}; int i=0;
        while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
        path[i]='\0';
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

    /* #r "nuget: PackageName, 1.0" → external NuGet */
    if(starts_with(p,"r ")) {
        p=skip_ws(p+2);
        while(*p&&*p!='"'&&*p!='\'') p++;
        if(!*p) return;
        char q=*p++; char ref[MAX_PATH]={0}; int i=0;
        while(*p&&*p!=q&&i<MAX_PATH-1) ref[i++]=*p++;
        ref[i]='\0';

        if(starts_with(ref,"nuget:")) {
            /* NuGet: "nuget: PackageName, 1.0" → "PackageName" */
            const char *pkg=skip_ws(ref+6);
            char name[MAX_SYMBOL]={0}; int j=0;
            while(*pkg&&*pkg!=','&&*pkg!=' '&&j<MAX_SYMBOL-1) name[j++]=*pkg++;
            name[j]='\0';
            if(name[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,name);
        } else if(ref[0]=='.'||ref[0]=='/') {
            /* local DLL ref */
            const char *base=ref;
            for(const char *c=ref;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
            char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
            char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
            if(no_ext[0]) {
                char tagged[MAX_SYMBOL]={0};
                snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
            }
        } else {
            /* direct assembly name */
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,ref);
        }
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FileEntry *fe, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip access modifiers */
    const char *mods[]={"public ","private ","internal ","protected ","static ",
                        "abstract ","virtual ","override ","inline ","mutable ",
                        "rec ","new ","member ","do ","and ",NULL};
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++)
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
    }

    /* module MyModule = */
    if(starts_with(p,"module ")) {
        p=skip_ws(p+7);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* let [rec] myFunc args = */
    if(starts_with(p,"let ")) {
        p=skip_ws(p+4);
        if(starts_with(p,"rec ")) p=skip_ws(p+4);
        if(starts_with(p,"inline ")) p=skip_ws(p+7);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if(l&&nm[0]&&islower((unsigned char)nm[0]))
            add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* type MyType = */
    if(starts_with(p,"type ")) {
        p=skip_ws(p+5);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* exception MyExn */
    if(starts_with(p,"exception ")) {
        p=skip_ws(p+10);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* inherit BaseClass() → dep */
    if(starts_with(p,"inherit ")) {
        p=skip_ws(p+8);
        char nm[MAX_SYMBOL]={0};
        int i=0;
        while(*p&&(isalnum((unsigned char)*p)||*p=='.'||*p=='_')&&i<MAX_SYMBOL-1)
            nm[i++]=*p++;
        nm[i]='\0';
        char root[MAX_SYMBOL]={0}; get_root(nm,root,sizeof(root));
        if(root[0]) {
            if(is_external_ns(root)) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
            else {
                char tagged[MAX_SYMBOL]={0};
                snprintf(tagged,sizeof(tagged),"local:%s",root);
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
            }
        }
    }

    /* interface IFoo with → dep */
    if(starts_with(p,"interface ")) {
        p=skip_ws(p+10);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
    }
}

/* ================================================================ Attribute parsing */

static void parse_attribute(const char *line, FileEntry *fe) {
    /* [<AttributeName(...)>] */
    const char *p=line;
    if(*p!='['||*(p+1)!='<') return;
    p+=2;
    char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
    /* skip common F#/.NET built-ins */
    static const char *skip_attrs[]={
        "EntryPoint","Literal","DefaultValue","Struct","Measure",
        "AutoOpen","RequireQualifiedAccess","Sealed","AbstractClass",
        "CLIEvent","CLIMutable","AllowNullLiteral","NoEquality","NoComparison",
        "CustomEquality","CustomComparison","Experimental","Obsolete",
        "DllImport","FieldOffset","StructLayout","MarshalAs","ThreadStatic",
        "Serializable","NonSerialized",NULL
    };
    for(int i=0;skip_attrs[i];i++) if(strcmp(skip_attrs[i],nm)==0) return;
    if(nm[0]&&isupper((unsigned char)nm[0]))
        add_sym(fe->calls,&fe->call_count,MAX_CALLS,nm);
}

/* ================================================================ Public API */

void parser_fsharp_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_fsharp] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0; /* (* ... *) */

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* F# block comments: (* ... *) */
        if(!in_bc&&strstr(line,"(*")) in_bc=1;
        if(in_bc){if(strstr(line,"*)"))in_bc=0;continue;}

        /* strip // comments */
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        if(*t=='#')                  parse_directive(t,fe);
        if(*t=='['&&*(t+1)=='<')     parse_attribute(t,fe);
        if(starts_with(t,"open "))   parse_open(t,fe);
        parse_def(t,fe,defs,def_count,max_defs,fe->name);
    }
    fclose(f);
}
