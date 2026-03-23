/* parser_lua.c  -  Lua dependency parser
 *
 * Handles:
 *   require 'module'                   → external
 *   require "lib.utils"                → "lib" (first component)
 *   require('./relative/mod')          → internal (local:mod)
 *   local x = require('x')            → external
 *   dofile('./other.lua')              → internal
 *   loadfile('./config.lua')           → internal
 *   function foo() ... end             → FunctionIndex
 *   local function foo() ... end       → FunctionIndex
 *   function Module:method() ... end   → FunctionIndex
 *   function Module.func() ... end     → FunctionIndex
 *   obj:method() / lib.func()         → calls
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_lua.h"
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
    while (*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<out_size-1)
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

/* Extract quoted string from  'foo.bar'  or  "foo.bar" */
static int extract_quoted(const char *s, char *out, int out_size) {
    while (*s&&*s!='\''&&*s!='"') s++;
    if (!*s) return 0;
    char q=*s++; int i=0;
    while (*s&&*s!=q&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
}

/* ================================================================ require / dofile */

static void parse_require(const char *line, FileEntry *fe) {
    const char *p=line;

    /* require(...) or require '...' or require "..." */
    const char *rp=strstr(p,"require");
    if (!rp) return;
    /* word boundary */
    if (rp>p&&(isalnum((unsigned char)*(rp-1))||*(rp-1)=='_')) return;
    p=rp+7;
    p=skip_ws(p);
    if (*p=='(') p=skip_ws(p+1);

    char path[MAX_PATH]={0};
    if (!extract_quoted(p,path,sizeof(path))) return;

    /* relative path (starts with . or /) → internal */
    if (path[0]=='.'||path[0]=='/') {
        const char *base=path;
        for (const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char no_ext[MAX_SYMBOL]={0};
        strncpy(no_ext,base,MAX_SYMBOL-1);
        /* strip .lua extension */
        char *dot=strrchr(no_ext,'.'); if(dot) *dot='\0';
        if (no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
        return;
    }

    /* dot-separated module: "lib.utils.helper" → first component "lib" */
    char mod[MAX_SYMBOL]={0}; int i=0;
    const char *q=path;
    while (*q&&*q!='.'&&i<MAX_SYMBOL-1) mod[i++]=*q++;
    mod[i]='\0';
    if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
}

/* dofile('./x.lua') or loadfile('./x.lua') */
static void parse_file_load(const char *line, FileEntry *fe) {
    const char *kws[]={"dofile","loadfile",NULL};
    for (int i=0;kws[i];i++) {
        const char *p=strstr(line,kws[i]); if(!p) continue;
        p+=strlen(kws[i]);
        p=skip_ws(p); if(*p=='(') p=skip_ws(p+1);
        char path[MAX_PATH]={0};
        if (!extract_quoted(p,path,sizeof(path))) continue;
        const char *base=path;
        for (const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char no_ext[MAX_SYMBOL]={0};
        strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
        if (no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
        return;
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* local function foo() */
    if (starts_with(p,"local ")) p=skip_ws(p+6);

    if (!starts_with(p,"function ")) return;
    p=skip_ws(p+9);

    /* function Module:method() or Module.func() */
    char nm[MAX_SYMBOL]={0}; int i=0;
    while (*p&&(isalnum((unsigned char)*p)||*p=='_'||*p=='.'||*p==':')&&i<MAX_SYMBOL-1)
        nm[i++]=*p++;
    nm[i]='\0';

    /* get last component after : or . */
    char *colon=strrchr(nm,':');
    char *dot=strrchr(nm,'.');
    char *sep=(colon&&dot)?(colon>dot?colon:dot):(colon?colon:dot);
    const char *func=sep?sep+1:nm;

    if (func[0]) add_def(defs,def_count,max_defs,func,filename);
}

/* ================================================================ Call parsing */

static const char *LUA_KW[]={
    "and","break","do","else","elseif","end","false","for","function",
    "goto","if","in","local","nil","not","or","repeat","return","then",
    "true","until","while","require","print","tostring","tonumber",
    "type","pairs","ipairs","next","select","unpack","table","string",
    "math","io","os","coroutine","package","debug","utf8","load",
    "dofile","loadfile","pcall","xpcall","error","assert","rawget",
    "rawset","rawequal","rawlen","setmetatable","getmetatable",NULL
};
static int is_kw(const char *s){
    for(int i=0;LUA_KW[i];i++) if(strcmp(LUA_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* . and : chaining */
            while (*p=='.'||*p==':') {
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

void parser_lua_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_lua] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_long_comment=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* Lua long comments: --[[ ... ]] */
        if (!in_long_comment&&strstr(line,"--[[")) { in_long_comment=1; }
        if (in_long_comment) { if(strstr(line,"]]")) in_long_comment=0; continue; }

        /* strip -- line comments */
        char *cmt=strstr(line,"--"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        parse_require(t,fe);
        parse_file_load(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
