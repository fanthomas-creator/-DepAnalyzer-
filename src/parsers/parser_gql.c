/* parser_gql.c  -  GraphQL schema/operation dependency parser
 *
 * Handles:
 *   #import "./fragments/UserFields.graphql"  → internal
 *   type Query { field: ReturnType }          → def "Query"
 *   type MyType implements MyInterface        → def + call (interface)
 *   interface MyInterface { }                 → def
 *   enum Status { ACTIVE INACTIVE }           → def
 *   input CreateUserInput { }                 → def
 *   union SearchResult = User | Post          → def + calls User, Post
 *   fragment UserFields on User               → def + call User
 *   extend type Query { }                     → call Query
 *   scalar DateTime                           → def
 *   directive @myDir on FIELD_DEFINITION      → def
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_gql.h"
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

/* ================================================================ Import */

static void parse_import(const char *line, FileEntry *fe) {
    /* Apollo: #import "./fragments/UserFields.graphql" */
    const char *p=line+1; /* skip # */
    p=skip_ws(p);
    if(!starts_with(p,"import")) return;
    p=skip_ws(p+6);
    /* find quoted path */
    while(*p&&*p!='"'&&*p!='\'') p++;
    if(!*p) return;
    char q=*p++; int i=0;
    char path[MAX_PATH]={0};
    while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
    path[i]='\0';
    /* basename without extension */
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

    /* extend type/interface/union → call (not def) */
    if(starts_with(p,"extend ")) {
        p=skip_ws(p+7);
        /* skip type/interface keyword */
        while(*p&&*p!=' '&&*p!='\t') p++;
        p=skip_ws(p);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,nm);
        return;
    }

    /* directive @name */
    if(starts_with(p,"directive ")) {
        p=skip_ws(p+10);
        if(*p=='@') p++;
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* type / interface / enum / input / union / scalar / fragment */
    const char *kws[]={"type ","interface ","enum ","input ","union ",
                        "scalar ","fragment ",NULL};
    for(int i=0;kws[i];i++) {
        if(!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));

        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if(!nm[0]) return;
        add_def(defs,def_count,max_defs,nm,filename);
        p+=l; p=skip_ws(p);

        /* "fragment X on TypeName" → call TypeName */
        if(starts_with(kws[i],"fragment ")) {
            if(starts_with(p,"on ")) {
                p=skip_ws(p+3);
                char on[MAX_SYMBOL]={0}; read_ident(p,on,sizeof(on));
                if(on[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,on);
            }
            return;
        }

        /* "type X implements A & B" → calls A, B */
        if(starts_with(p,"implements ")) {
            p=skip_ws(p+11);
            while(*p&&*p!='{'&&*p!='\n') {
                if(*p=='&'){p++;p=skip_ws(p);continue;}
                char iface[MAX_SYMBOL]={0}; int il=read_ident(p,iface,sizeof(iface));
                if(il&&iface[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,iface);
                p+=il; p=skip_ws(p);
            }
        }

        /* "union X = TypeA | TypeB" → calls TypeA, TypeB */
        if(starts_with(kws[i],"union ")) {
            while(*p&&*p!='='&&*p!='\n') p++;
            if(*p=='=') {
                p++;
                while(*p&&*p!='\n') {
                    p=skip_ws(p);
                    if(*p=='|'){p++;continue;}
                    char t[MAX_SYMBOL]={0}; int tl=read_ident(p,t,sizeof(t));
                    if(tl&&t[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,t);
                    p+=tl;
                }
            }
        }
        return;
    }
}

/* ================================================================ Public API */

void parser_gql_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_gql] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        const char *t=skip_ws(line); if(!*t) continue;

        if(*t=='#') { parse_import(t,fe); continue; }
        /* strip inline comments */
        char tmp[MAX_LINE]; strncpy(tmp,line,MAX_LINE-1);
        char *cmt=strstr(tmp,"#"); if(cmt)*cmt='\0';

        parse_def(skip_ws(tmp),defs,def_count,max_defs,fe->name,fe);
    }
    fclose(f);
}
