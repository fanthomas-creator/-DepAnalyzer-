/* parser_toml.c  -  TOML dependency parser
 *
 * Handles Cargo.toml, pyproject.toml, package.toml:
 *
 *   [dependencies]
 *   serde = "1.0"
 *   tokio = { version = "1", features = ["full"] }
 *   my-lib = { path = "../my-lib" }          → internal
 *
 *   [dev-dependencies]
 *   [build-dependencies]
 *
 *   [tool.poetry.dependencies]
 *   requests = "^2.28"
 *
 *   [project]
 *   dependencies = ["requests>=2.0", "flask"]
 *
 *   [workspace]
 *   members = ["crate-a", "crate-b"]         → internal
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_toml.h"
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
static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    /* strip version suffix: "requests>=2.0" → "requests" */
    char clean[MAX_SYMBOL]={0}; int i=0;
    const char *p=val;
    if (*p=='"'||*p=='\'') p++;
    while (*p&&*p!='"'&&*p!='\''&&*p!='>'&&*p!='<'&&*p!='='&&
           *p!=','&&*p!=']'&&*p!=' '&&i<MAX_SYMBOL-1)
        clean[i++]=*p++;
    clean[i]='\0';
    /* strip trailing [ or ( */
    while(i>0&&(clean[i-1]=='['||clean[i-1]=='(')) clean[--i]='\0';
    if (!clean[0]||clean[0]=='#') return;
    for (int j=0;j<fe->import_count;j++) if(strcmp(fe->imports[j],clean)==0) return;
    if (fe->import_count<MAX_IMPORTS)
        strncpy(fe->imports[fe->import_count++],clean,MAX_SYMBOL-1);
}
static void add_internal(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    char clean[MAX_SYMBOL]={0}; int i=0;
    const char *p=val;
    if (*p=='"'||*p=='\'') p++;
    /* get basename */
    const char *base=p;
    while(*p&&*p!='"'&&*p!='\'') { if(*p=='/'||*p=='\\') base=p+1; p++; }
    p=base;
    while(*p&&*p!='"'&&*p!='\''&&i<MAX_SYMBOL-1) clean[i++]=*p++;
    clean[i]='\0';
    if (!clean[0]) return;
    char tagged[MAX_SYMBOL]={0};
    snprintf(tagged,sizeof(tagged),"local:%s",clean);
    for (int j=0;j<fe->import_count;j++) if(strcmp(fe->imports[j],tagged)==0) return;
    if (fe->import_count<MAX_IMPORTS)
        strncpy(fe->imports[fe->import_count++],tagged,MAX_SYMBOL-1);
}

/* ================================================================ Section detection */

typedef enum {
    SEC_NONE,
    SEC_DEPS,          /* [dependencies] / [dev-dependencies] / [build-dependencies] */
    SEC_POETRY_DEPS,   /* [tool.poetry.dependencies] / [tool.poetry.dev-dependencies] */
    SEC_PROJECT,       /* [project] */
    SEC_WORKSPACE,     /* [workspace] */
} Section;

static Section detect_section(const char *line) {
    if (!starts_with(line,"[")) return SEC_NONE;
    /* strip brackets */
    const char *p=line+1;
    while(*p=='[') p++; /* handle [[ ]] */

    if (starts_with(p,"dependencies")    ||
        starts_with(p,"dev-dependencies")||
        starts_with(p,"build-dependencies")) return SEC_DEPS;
    if (starts_with(p,"tool.poetry.dependencies")||
        starts_with(p,"tool.poetry.dev-dependencies")) return SEC_POETRY_DEPS;
    if (starts_with(p,"project"))        return SEC_PROJECT;
    if (starts_with(p,"workspace"))      return SEC_WORKSPACE;
    return SEC_NONE;
}

/* Parse inline list: ["a", "b>=1.0", "c"] */
static void parse_inline_list(const char *p, FileEntry *fe, int is_internal) {
    while (*p&&*p!='[') p++;
    if (!*p) return;
    p++;
    while (*p&&*p!=']') {
        p=skip_ws(p);
        if (*p=='"'||*p=='\'') {
            char item[MAX_SYMBOL]={0}; int i=0;
            char q=*p++;
            while(*p&&*p!=q&&i<MAX_SYMBOL-1) item[i++]=*p++;
            item[i]='\0';
            if (item[0]) {
                if (is_internal) add_internal(fe,item);
                else add_import(fe,item);
            }
            if (*p) p++;
        }
        while(*p&&*p!=','&&*p!=']') p++;
        if (*p==',') p++;
    }
}

/* ================================================================ Public API */

void parser_toml_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;

    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_toml] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    Section sec=SEC_NONE;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* strip # comments */
        char *cmt=strchr(line,'#'); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        /* section header */
        if (*t=='[') {
            Section ns=detect_section(t);
            if (ns!=SEC_NONE) { sec=ns; continue; }
            /* any other section resets to NONE */
            sec=SEC_NONE; continue;
        }

        if (sec==SEC_NONE) continue;

        /* key = value */
        const char *eq=strchr(t,'=');
        if (!eq) continue;

        char key[MAX_SYMBOL]={0};
        int klen=(int)(eq-t);
        if (klen<=0||klen>=MAX_SYMBOL) continue;
        strncpy(key,t,klen);
        /* trim key */
        int ki=klen;
        while(ki>0&&(key[ki-1]==' '||key[ki-1]=='\t')) key[--ki]='\0';

        const char *val=skip_ws(eq+1);

        if (sec==SEC_DEPS||sec==SEC_POETRY_DEPS) {
            /* key is the package name */
            if (strcmp(key,"version")==0||strcmp(key,"features")==0||
                strcmp(key,"optional")==0||strcmp(key,"default-features")==0) continue;

            /* check for path = "..." inside inline table → internal */
            if (strstr(val,"path")) {
                const char *pp=strstr(val,"path");
                pp=skip_ws(pp+4);
                if (*pp=='=') {
                    pp=skip_ws(pp+1);
                    add_internal(fe,pp);
                }
            } else {
                add_import(fe,key);
            }
        }

        if (sec==SEC_PROJECT) {
            /* dependencies = ["pkg1", "pkg2>=1.0"] */
            if (strcmp(key,"dependencies")==0)
                parse_inline_list(val,fe,0);
        }

        if (sec==SEC_WORKSPACE) {
            /* members = ["crate-a", "crate-b"] */
            if (strcmp(key,"members")==0)
                parse_inline_list(val,fe,1);
            /* path = "../other" */
            if (strcmp(key,"path")==0)
                add_internal(fe,val);
        }
    }
    fclose(f);
}
