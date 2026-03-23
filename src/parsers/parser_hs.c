/* parser_hs.c  -  Haskell dependency parser
 *
 * Handles:
 *   import Data.Map
 *   import qualified Data.Map as Map
 *   import Control.Monad (forM_, when)
 *   import Data.Map hiding (lookup)
 *   module MyApp.Server (runServer) where  → module decl
 *   data MyType = Con1 | Con2             → FunctionIndex
 *   newtype Wrapper a = Wrapper { unWrap :: a }
 *   type Alias = String                   → FunctionIndex
 *   myFunc :: Int -> String               → FunctionIndex (sig)
 *   myFunc x = ...                        → FunctionIndex (def)
 *   class Eq a => MyClass a where         → FunctionIndex
 *   instance MyClass Int where            → dep
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_hs.h"
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

/* Known Haskell base packages (always external) */
static int is_base_pkg(const char *root) {
    static const char *base[]={
        "Prelude","Data","Control","System","Text","Network","Database",
        "Graphics","Sound","Test","GHC","Foreign","Numeric","Debug",
        "Codec","Crypto","Web","Language","Algorithm","Unsafe",NULL
    };
    for (int i=0;base[i];i++) if(strcmp(base[i],root)==0) return 1;
    return 0;
}

/* ================================================================ Import parsing */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    if (!starts_with(p,"import ")) return;
    p=skip_ws(p+7);
    /* qualified */
    if (starts_with(p,"qualified ")) p=skip_ws(p+10);
    /* safe */
    if (starts_with(p,"safe ")) p=skip_ws(p+5);

    /* read module path: Foo.Bar.Baz */
    char mod[MAX_PATH]={0}; int i=0;
    while (*p&&(isalnum((unsigned char)*p)||*p=='.')&&i<MAX_PATH-1) mod[i++]=*p++;
    mod[i]='\0';
    if (!mod[0]) return;

    /* get root component */
    char root[MAX_SYMBOL]={0};
    { int ri=0,mi=0; while(mod[mi]&&mod[mi]!='.'&&ri<MAX_SYMBOL-1){root[ri]=mod[mi];ri++;mi++;} root[ri]='\0'; }

    /* store root as the dep */
    if (is_base_pkg(root)) {
        /* external Haskell package */
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);
    } else {
        /* could be internal project module */
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",root);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* data / newtype / type */
    const char *kws[]={"data ","newtype ","type ","class ","instance ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        /* skip context: (Eq a, Show a) => */
        if (strchr(p,'=')&&strstr(p,"=>")) {
            const char *arr=strstr(p,"=>"); p=skip_ws(arr+2);
        }
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]&&isupper((unsigned char)nm[0]))
            add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* function type signature: myFunc :: Type */
    const char *dcolon=strstr(p,"::");
    if (dcolon) {
        char nm[MAX_SYMBOL]={0};
        int l=(int)(dcolon-p);
        if (l>0&&l<MAX_SYMBOL) {
            strncpy(nm,p,l); nm[l]='\0';
            /* trim spaces from nm */
            while(l>0&&(nm[l-1]==' '||nm[l-1]=='\t')) nm[--l]='\0';
            if (nm[0]&&islower((unsigned char)nm[0])&&!strchr(nm,' '))
                add_def(defs,def_count,max_defs,nm,filename);
        }
        return;
    }

    /* function definition: myFunc arg1 arg2 = ... */
    if (islower((unsigned char)*p)||*p=='_') {
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if (l>0) {
            const char *after=skip_ws(p+l);
            /* must not be a keyword */
            if (*after!='='&&*after!='{'&&!starts_with(after,"where")&&
                !starts_with(after,"let")&&!starts_with(after,"in")&&
                nm[0]) {
                add_def(defs,def_count,max_defs,nm,filename);
            } else if (*after=='=') {
                add_def(defs,def_count,max_defs,nm,filename);
            }
        }
    }
}

/* ================================================================ Public API */

void parser_hs_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_hs] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* Haskell block comments: {- ... -} */
        if (!in_block_comment&&strstr(line,"{-")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"-}"))in_block_comment=0;continue;}

        /* literate Haskell: lines not starting with > are comments */
        /* (simplified: we process all lines) */

        /* strip -- comments */
        char *cmt=strstr(line,"--"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        if (starts_with(t,"import ")) parse_import(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
    }
    fclose(f);
}
