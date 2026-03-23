/* parser_ex.c  -  Elixir dependency parser
 *
 * Handles:
 *   use Phoenix.Controller            → "Phoenix" external dep
 *   use MyApp.Web, :controller        → "MyApp" internal dep
 *   alias MyApp.Accounts.User         → "MyApp" internal dep
 *   alias MyApp.{User, Post}          → grouped alias
 *   import Ecto.Query                  → external dep
 *   require Logger                     → dep
 *   @behaviour MyBehaviour             → dep
 *   defmodule MyApp.UserController    → FunctionIndex
 *   def my_func / defp priv / defmacro → FunctionIndex
 *   Module.function() / obj.method()   → calls
 *   |> SomeMod.func()                  → pipe call
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_ex.h"
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
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='?'||*s=='!')&&i<out_size-1)
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

/* Known Elixir/Erlang/OTP base modules (always external) */
static int is_stdlib(const char *root) {
    static const char *base[]={
        "Kernel","Process","Agent","Task","GenServer","GenStage","Supervisor",
        "Application","Module","Code","Enum","Stream","Map","MapSet","List",
        "String","Integer","Float","Atom","Tuple","IO","File","Path","System",
        "Logger","Registry","ETS","DETS","Node","Port","Timer","Calendar",
        "Date","Time","DateTime","NaiveDateTime","Duration","Regex","URI",
        "Base","Bitwise","Exception","Protocol","Macro","Quote","Access",
        "Collectable","Enumerable","Inspect","Range","Record","Keyword",NULL
    };
    for (int i=0;base[i];i++) if(strcmp(base[i],root)==0) return 1;
    return 0;
}

/* Get root module name: "MyApp.Accounts.User" → "MyApp" */
static void get_root(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&*s!='.'&&*s!=','&&*s!=' '&&*s!='{'&&*s!='\n'&&i<out_size-1)
        out[i++]=*s++;
    out[i]='\0';
}

/* ================================================================ Directive parsing */

static void parse_directive(const char *line, FileEntry *fe) {
    const char *p=line;
    const char *kws[]={"use ","alias ","import ","require ",NULL};
    int is_internal[]={0,1,0,0}; /* alias → likely internal MyApp.* */

    for (int ki=0;kws[ki];ki++) {
        if (!starts_with(p,kws[ki])) continue;
        p=skip_ws(p+strlen(kws[ki]));

        /* grouped alias: alias MyApp.{User, Post} */
        char root[MAX_SYMBOL]={0};
        get_root(p,root,sizeof(root));
        if (!root[0]) return;

        int internal=is_internal[ki];
        /* heuristic: if root matches known stdlib → external */
        if (is_stdlib(root)) internal=0;

        char tagged[MAX_SYMBOL]={0};
        if (internal) snprintf(tagged,sizeof(tagged),"local:%s",root);
        else strncpy(tagged,root,MAX_SYMBOL-1);

        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);

        /* grouped: alias MyApp.{User, Post} */
        const char *brace=strchr(p,'{');
        if (brace) {
            brace++;
            while(*brace&&*brace!='}') {
                brace=skip_ws(brace);
                char sub[MAX_SYMBOL]={0}; int l=read_ident(brace,sub,sizeof(sub));
                if (l&&sub[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,sub);
                brace+=l;
                while(*brace&&*brace!=','&&*brace!='}') brace++;
                if (*brace==',') brace++;
            }
        }
        return;
    }

    /* @behaviour MyBehaviour */
    if (starts_with(p,"@behaviour ")) {
        p=skip_ws(p+11);
        char nm[MAX_SYMBOL]={0}; get_root(p,nm,sizeof(nm));
        if (nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* defmodule MyApp.UserController do */
    if (starts_with(p,"defmodule ")) {
        p=skip_ws(p+10);
        char nm[MAX_PATH]={0}; int i=0;
        while(*p&&(isalnum((unsigned char)*p)||*p=='.'||*p=='_')&&i<MAX_PATH-1)
            nm[i++]=*p++;
        nm[i]='\0';
        /* get last component */
        const char *last=nm;
        for (const char *c=nm;*c;c++) if(*c=='.') last=c+1;
        if (last[0]) add_def(defs,def_count,max_defs,last,filename);
        return;
    }

    /* def / defp / defmacro / defmacrop / defdelegate / defguard */
    const char *defkws[]={"defmacrop ","defmacro ","defdelegate ","defguardp ",
                           "defguard ","defp ","def ",NULL};
    for (int i=0;defkws[i];i++) {
        if (!starts_with(p,defkws[i])) continue;
        p=skip_ws(p+strlen(defkws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }
}

/* ================================================================ Call parsing */

static const char *EX_KW[]={
    "if","unless","else","cond","case","with","for","receive","after",
    "try","rescue","catch","raise","reraise","throw","exit","do","end",
    "def","defp","defmodule","defmacro","defstruct","defprotocol",
    "defimpl","defdelegate","defguard","defoverridable","use","alias",
    "import","require","when","and","or","not","in","is_nil","is_atom",
    "is_binary","is_boolean","is_integer","is_float","is_list","is_map",
    "is_tuple","is_pid","is_port","is_reference","is_function",
    "nil","true","false","__MODULE__","__CALLER__","__DIR__",NULL
};
static int is_kw(const char *s){
    for(int i=0;EX_KW[i];i++) if(strcmp(EX_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* Module.func() */
            while (*p=='.') {
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

void parser_ex_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_ex] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* Elixir block comments (none officially, but @doc ~s""" ... """ exists) */
        /* strip # comments */
        char *cmt=strchr(line,'#');
        while (cmt) {
            int in_str=0; char sc=0;
            for (const char *c=line;c<cmt;c++) {
                if (!in_str&&(*c=='"'||*c=='\'')){in_str=1;sc=*c;}
                else if (in_str&&*c==sc) in_str=0;
            }
            if (!in_str){*cmt='\0';break;}
            cmt=strchr(cmt+1,'#');
        }

        (void)in_block_comment;
        const char *t=skip_ws(line); if(!*t) continue;

        parse_directive(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
