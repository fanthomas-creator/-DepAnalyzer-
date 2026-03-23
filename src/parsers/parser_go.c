/* parser_go.c  -  Go dependency parser
 *
 * Handles:
 *   import "fmt"
 *   import "github.com/gin-gonic/gin"
 *   import (
 *       "fmt"
 *       "os"
 *       mypkg "github.com/foo/bar"
 *       _ "github.com/lib/pq"
 *   )
 *   func FuncName(args) rettype { }
 *   func (r *Receiver) Method(args) rettype { }
 *   type Name struct / interface / chan / map
 *   pkg.Call()
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_go.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}
static const char *skip_ws(const char *s) {
    while (*s==' '||*s=='\t') s++; return s;
}
static int read_ident(const char *s, char *out, int out_size) {
    int i=0;
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
    if (*count<max){
        strncpy(defs[*count].function,func,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* Extract last component of import path:
 * "github.com/gin-gonic/gin" → "gin"
 * "fmt"                      → "fmt"
 * "./utils"                  → "utils"  (marks as local)
 */
static void extract_import(const char *line, char *out, int out_size, int *is_local) {
    /* find quoted string */
    const char *p=line;
    while (*p&&*p!='"') p++;
    if (!*p){out[0]='\0';return;}
    p++;
    char path[MAX_PATH]={0}; int i=0;
    while (*p&&*p!='"'&&i<MAX_PATH-1) path[i++]=*p++;
    path[i]='\0';
    if (!path[0]){out[0]='\0';return;}

    /* check if relative */
    *is_local = (path[0]=='.');

    /* get last path component */
    const char *last=path;
    for (const char *c=path;*c;c++) if(*c=='/') last=c+1;

    /* strip leading ./ or ../ */
    while (starts_with(last,"./")) last+=2;
    while (starts_with(last,"../")) last+=3;

    strncpy(out,last,out_size-1);
    out[out_size-1]='\0';

    /* strip -vX version suffixes common in Go modules: gin-gonic/gin.v2 → gin */
    char *dot=strrchr(out,'.'); if(dot) *dot='\0';
    /* strip hyphen suffixes: gin-gonic → gin */
    /* keep as-is, more useful to keep full last component */
}

/* ================================================================ Import parsing */

static void parse_import_line(const char *line, FileEntry *fe) {
    /* single-line:  import "pkg"  or  import alias "pkg" */
    int is_local=0;
    char mod[MAX_SYMBOL]={0};
    extract_import(line,mod,sizeof(mod),&is_local);
    if (!mod[0]) return;

    if (is_local) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",mod);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    } else {
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* func FuncName(  or  func (recv) FuncName( */
    if (starts_with(p,"func ")) {
        p=skip_ws(p+5);
        /* receiver: (r *Type) */
        if (*p=='(') {
            /* skip receiver block */
            int depth=1; p++;
            while (*p&&depth>0){if(*p=='(')depth++;if(*p==')')depth--;p++;}
            p=skip_ws(p);
        }
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* type Name struct / interface / chan / map / func */
    if (starts_with(p,"type ")) {
        p=skip_ws(p+5);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *GO_KW[]={
    "if","else","for","range","switch","case","default","return",
    "break","continue","goto","fallthrough","defer","go","select",
    "var","const","type","struct","interface","map","chan","func",
    "package","import","make","new","len","cap","append","copy",
    "delete","close","panic","recover","print","println",NULL
};
static int is_kw(const char *s){
    for(int i=0;GO_KW[i];i++) if(strcmp(GO_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* pkg.Func pattern */
            while (*p=='.'){
                p++;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if (*p=='('&&!is_kw(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_go_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_go] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_import_block=0;
    int in_block_comment=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* block comments */
        if (!in_block_comment&&strstr(line,"/*")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"*/"))in_block_comment=0;continue;}

        /* strip // */
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        /* import block start */
        if (starts_with(t,"import (")) { in_import_block=1; continue; }
        if (in_import_block) {
            if (*t==')') { in_import_block=0; continue; }
            parse_import_line(t, fe);
            continue;
        }

        /* single import */
        if (starts_with(t,"import ")) {
            parse_import_line(t, fe);
            continue;
        }

        parse_def(t, defs, def_count, max_defs, fe->name);
        parse_calls(t, fe);
    }
    fclose(f);
}
