/* parser_rb.c  –  Ruby dependency parser
 *
 * Handles:
 *   require 'net/http'          → external import
 *   require_relative './utils'  → internal import (relative)
 *   require 'my_module'         → import (resolver decides int/ext)
 *   include ModuleName          → import (mixin)
 *   extend  ModuleName          → import (mixin)
 *   prepend ModuleName          → import (mixin)
 *   class Foo < Bar             → def Foo, import Bar
 *   module Foo                  → FunctionIndex
 *   def foo / def self.foo      → FunctionIndex
 *   Gem::Specification          → call
 *   foo.bar / Foo::method       → call
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_rb.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================
 * Helpers
 * ================================================================ */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s,pre,strlen(pre))==0;
}
static const char *skip_ws(const char *s) {
    while(*s==' '||*s=='\t') s++; return s;
}
static int read_ident(const char *s, char *out, int out_size) {
    int i=0;
    while (*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='?'||*s=='!')&&i<out_size-1)
        out[i++]=*s++;
    out[i]='\0'; return i;
}
/* Ruby constant/class names start with uppercase */
static int read_const(const char *s, char *out, int out_size) {
    if (!isupper((unsigned char)*s)) return 0;
    return read_ident(s,out,out_size);
}

static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->import_count;i++) if(strcmp(fe->imports[i],val)==0) return;
    if (fe->import_count<MAX_IMPORTS) strncpy(fe->imports[fe->import_count++],val,MAX_SYMBOL-1);
}
static void add_call(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->call_count;i++) if(strcmp(fe->calls[i],val)==0) return;
    if (fe->call_count<MAX_CALLS) strncpy(fe->calls[fe->call_count++],val,MAX_SYMBOL-1);
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

/* Extract quoted string  'foo/bar'  or  "foo/bar" */
static int extract_quoted(const char *s, char *out, int out_size) {
    while (*s&&*s!='\''&&*s!='"') s++;
    if (!*s) return 0;
    char q=*s++;
    int i=0;
    while (*s&&*s!=q&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
}

/* ================================================================
 * require / require_relative
 * ================================================================ */

static void parse_require(const char *line, FileEntry *fe) {
    const char *p=line;
    int is_relative=0;

    if (starts_with(p,"require_relative")) {
        p+=16; is_relative=1;
    } else if (starts_with(p,"require")) {
        p+=7;
    } else return;

    /* optional space/tab/( */
    p=skip_ws(p); if(*p=='(') p++;

    char path[MAX_PATH]={0};
    if (!extract_quoted(p,path,sizeof(path))) return;

    /* Get the last component as the module name */
    const char *base=path;
    const char *s=path+strlen(path);
    while (s>path&&*(s-1)!='/'&&*(s-1)!='\\') s--;
    base=s;

    /* Strip leading ./ or ../ */
    while (starts_with(base,"./")||starts_with(base,"../")) {
        if (base[1]=='/') base+=2;
        else base+=3;
    }

    char mod[MAX_SYMBOL]={0};
    strncpy(mod,base,MAX_SYMBOL-1);

    /* Keep only first path component for external gems:
     * 'net/http' → 'net'  */
    char *slash=strchr(mod,'/');
    if (slash&&!is_relative) *slash='\0';

    /* Tag relative requires so resolver can identify them */
    if (is_relative) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",mod);
        add_import(fe,tagged);
    } else {
        add_import(fe,mod);
    }
}

/* ================================================================
 * include / extend / prepend  ModuleName
 * ================================================================ */

static void parse_mixin(const char *line, FileEntry *fe) {
    const char *kws[]={"include ","extend ","prepend ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(line,kws[i])) continue;
        const char *p=skip_ws(line+strlen(kws[i]));
        /* must start with uppercase (constant) */
        char mod[MAX_SYMBOL]={0};
        if (!read_const(p,mod,sizeof(mod))) return;
        if (mod[0]) add_import(fe,mod);
        return;
    }
}

/* ================================================================
 * Definition parsing
 * ================================================================ */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=line;

    /* module Foo  or  module Foo::Bar */
    if (starts_with(p,"module ")) {
        p=skip_ws(p+7);
        char nm[MAX_SYMBOL]={0}; read_const(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* class Foo  or  class Foo < Bar
     * inheritance import handled by parse_class_def (which has fe) */
    if (starts_with(p,"class ")) {
        p=skip_ws(p+6);
        char nm[MAX_SYMBOL]={0}; read_const(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* def foo  /  def self.foo  /  def initialize */
    if (starts_with(p,"def ")) {
        p=skip_ws(p+4);
        /* self.method */
        if (starts_with(p,"self.")) p+=5;
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* Separate helper for class with parent (needs fe) */
static void parse_class_def(const char *line, FileEntry *fe,
                              FunctionDef *defs, int *def_count, int max_defs) {
    if (!starts_with(line,"class ")) return;
    const char *p=skip_ws(line+6);
    char nm[MAX_SYMBOL]={0}; read_const(p,nm,sizeof(nm));
    if (nm[0]) add_def(defs,def_count,max_defs,nm,fe->name);

    p+=strlen(nm); p=skip_ws(p);
    if (*p=='<') {
        p=skip_ws(p+1);
        char parent[MAX_SYMBOL]={0}; read_const(p,parent,sizeof(parent));
        if (parent[0]) add_import(fe,parent);
    }
}

/* ================================================================
 * Call parsing
 * ================================================================ */

static const char *RB_KW[]={
    "if","elsif","else","unless","while","until","for","do","end",
    "begin","rescue","ensure","raise","return","yield","next","break",
    "case","when","then","true","false","nil","self","super",
    "require","require_relative","include","extend","prepend",
    "class","module","def","attr_reader","attr_writer","attr_accessor",
    "puts","print","p","pp","gets","exit","abort","sleep",NULL
};
static int is_kw(const char *s){
    for(int i=0;RB_KW[i];i++) if(strcmp(RB_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;

            /* Constant::method */
            while (p[0]==':'&&p[1]==':') {
                p+=2;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            /* obj.method */
            while (*p=='.') {
                p++;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }

            if (!is_kw(ident)&&ident[0])
                add_call(fe,ident);
        } else p++;
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void parser_rb_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_rb] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_multiline=0;   /* =begin ... =end */
    int in_heredoc=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* =begin / =end block comments */
        if (starts_with(line,"=begin")) { in_multiline=1; continue; }
        if (starts_with(line,"=end"))   { in_multiline=0; continue; }
        if (in_multiline) continue;

        /* heredoc (simplified) */
        if (!in_heredoc&&strstr(line,"<<~")){ in_heredoc=1; continue; }
        if (!in_heredoc&&strstr(line,"<<-")){ in_heredoc=1; continue; }
        if (!in_heredoc&&strstr(line,"<<")&&!starts_with(line,"<<")) {}
        /* simplistic end: if previous line started heredoc,
           a line of just an identifier ends it */
        if (in_heredoc) {
            const char *t=skip_ws(line);
            int all=1; for(const char *c=t;*c;c++) if(!isalnum((unsigned char)*c)&&*c!='_'){all=0;break;}
            if (all&&*t) in_heredoc=0;
            continue;
        }

        /* strip # comments */
        char *cmt=strchr(line,'#');
        while (cmt) {
            /* make sure it's not inside a string (simplified) */
            int in_str=0; char str_char=0;
            for (const char *c=line;c<cmt;c++) {
                if (!in_str&&(*c=='\''||*c=='"')){in_str=1;str_char=*c;}
                else if (in_str&&*c==str_char) in_str=0;
            }
            if (!in_str) { *cmt='\0'; break; }
            cmt=strchr(cmt+1,'#');
        }

        const char *t=skip_ws(line); if(!*t) continue;

        parse_require(t, fe);
        parse_mixin(t, fe);
        parse_class_def(t, fe, defs, def_count, max_defs);
        parse_def(t, defs, def_count, max_defs, fe->name);
        parse_calls(t, fe);
    }
    fclose(f);
}
