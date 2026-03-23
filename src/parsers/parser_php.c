/* parser_php.c  –  PHP dependency parser
 *
 * Handles:
 *   require 'file.php'          → import (internal candidate)
 *   require_once './lib.php'    → import
 *   include 'file.php'          → import
 *   include_once 'file.php'     → import
 *   use Vendor\Package\Class    → import (external if vendor, internal if App\)
 *   use App\Models\User as U    → import
 *   namespace App\Controllers   → recorded as call (namespace declaration)
 *   class Foo                   → FunctionIndex
 *   interface Foo               → FunctionIndex
 *   trait Foo                   → FunctionIndex
 *   function foo()              → FunctionIndex
 *   $var->method()              → call
 *   ClassName::staticMethod()   → call
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_php.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================
 * Helpers
 * ================================================================ */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}
static const char *skip_ws(const char *s) {
    while (*s==' '||*s=='\t') s++;
    return s;
}
static int read_ident(const char *s, char *out, int out_size) {
    int i=0;
    while (*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
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
    if (*count<max) {
        strncpy(defs[*count].function,func,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* Extract quoted string  'file.php'  or  "file.php" */
static int extract_quoted(const char *s, char *out, int out_size) {
    while (*s&&*s!='\''&&*s!='"') s++;
    if (!*s) return 0;
    char q=*s++;
    int i=0;
    while (*s&&*s!=q&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
}

/* ================================================================
 * require / include parsing
 * ================================================================ */

static void parse_include(const char *line, FileEntry *fe) {
    /* detect  require / require_once / include / include_once */
    const char *kws[]={"require_once","require","include_once","include",NULL};
    for (int i=0;kws[i];i++) {
        const char *p=strstr(line,kws[i]);
        if (!p) continue;
        /* word boundary */
        if (p>line&&(isalnum((unsigned char)*(p-1))||*(p-1)=='_')) continue;
        p+=strlen(kws[i]);
        /* skip optional ( */
        p=skip_ws(p); if(*p=='(') p++;
        char path[MAX_PATH]={0};
        if (!extract_quoted(p,path,sizeof(path))) continue;

        /* extract basename without extension */
        const char *base=path;
        const char *s=path+strlen(path);
        while (s>path&&*(s-1)!='/'&&*(s-1)!='\\') s--;
        base=s;
        char no_ext[MAX_SYMBOL]={0};
        strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
        if (no_ext[0]) add_import(fe,no_ext);
        return;
    }
}

/* ================================================================
 * use  Namespace\Class  parsing
 * ================================================================ */

static void parse_use(const char *line, FileEntry *fe) {
    const char *p=line;
    if (!starts_with(p,"use ")) return;
    p=skip_ws(p+4);

    /* use function / use const — skip those */
    if (starts_with(p,"function ")||starts_with(p,"const ")) return;

    /* Extract the first namespace component as the "package" */
    char ns[MAX_SYMBOL]={0}; int i=0;
    while (*p&&(isalnum((unsigned char)*p)||*p=='_'||*p=='\\'||*p=='{')&&i<MAX_SYMBOL-1) {
        if (*p=='{') break;   /* use grouped: use Foo\{Bar, Baz} */
        ns[i++]=*p++;
    }
    ns[i]='\0';
    if (!ns[0]) return;

    /* Get first component (root namespace = vendor or App) */
    char root[MAX_SYMBOL]={0};
    int j=0;
    const char *q=ns;
    while (*q&&*q!='\\'&&j<MAX_SYMBOL-1) root[j++]=*q++;
    root[j]='\0';

    /* Store the full namespace as import */
    add_import(fe,ns);

    /* If grouped use: use Foo\{Bar, Baz} */
    if (*p=='{') {
        p++;
        while (*p&&*p!='}') {
            p=skip_ws(p);
            char sub[MAX_SYMBOL]={0}; int k=0;
            while (*p&&*p!=','&&*p!='}'&&*p!=' '&&k<MAX_SYMBOL-1) sub[k++]=*p++;
            sub[k]='\0';
            if (sub[0]) {
                /* build full: Foo\Bar */
                char full[MAX_SYMBOL]={0};
                snprintf(full,sizeof(full),"%s\\%s",root,sub);
                add_import(fe,full);
            }
            while (*p&&*p!=','&&*p!='}') p++;
            if (*p==',') p++;
        }
    }
}

/* ================================================================
 * Definition parsing
 * ================================================================ */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *kws[]={"class ","interface ","trait ","abstract class ","final class ",NULL};
    for (int i=0;kws[i];i++) {
        const char *p=strstr(line,kws[i]);
        if (!p||(p>line&&isalnum((unsigned char)*(p-1)))) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }

    /* function foo() — may have visibility modifier */
    const char *p=strstr(line,"function ");
    if (p&&(p==line||!isalnum((unsigned char)*(p-1)))) {
        p=skip_ws(p+9);
        if (*p=='&') p++;   /* PHP reference return */
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================
 * Call parsing
 * ================================================================ */

static const char *PHP_KW[]={
    "if","else","elseif","for","foreach","while","do","switch","case",
    "return","echo","print","require","require_once","include","include_once",
    "new","class","interface","trait","function","namespace","use","throw",
    "try","catch","finally","static","abstract","final","public","private",
    "protected","extends","implements","list","array","match","fn",NULL
};
static int is_kw(const char *s){
    for(int i=0;PHP_KW[i];i++) if(strcmp(PHP_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        /* $var->method( */
        if (*p=='$') {
            p++;
            char var[MAX_SYMBOL]={0}; p+=read_ident(p,var,sizeof(var));
            if (starts_with(p,"->")) {
                p+=2;
                char method[MAX_SYMBOL]={0}; read_ident(p,method,sizeof(method));
                if (method[0]&&!is_kw(method)) add_call(fe,method);
            }
            continue;
        }
        /* ClassName::method( */
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            if (p[0]==':'&&p[1]==':') {
                p+=2;
                char method[MAX_SYMBOL]={0}; read_ident(p,method,sizeof(method));
                if (!is_kw(ident)) add_call(fe,ident);
                if (method[0]&&!is_kw(method)) add_call(fe,method);
                continue;
            }
            if (*p=='('&&!is_kw(ident)) add_call(fe,ident);
        } else p++;
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void parser_php_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_php] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;
    int in_heredoc=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* heredoc detection (skip content) */
        if (!in_heredoc&&strncmp(line,"<<<",3)==0) { in_heredoc=1; continue; }
        if (in_heredoc) {
            /* end of heredoc: a line with just an identifier and ; */
            const char *p=skip_ws(line);
            int all_ident=1;
            for (const char *c=p;*c&&*c!=';';c++)
                if (!isalnum((unsigned char)*c)&&*c!='_'){all_ident=0;break;}
            if (all_ident&&*p) in_heredoc=0;
            continue;
        }

        /* block comments */
        if (!in_block_comment&&strstr(line,"/*")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"*/"))in_block_comment=0;continue;}

        /* strip // and # comments */
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        cmt=strchr(line,'#'); if(cmt&&*(cmt+1)!='[')*cmt='\0'; /* keep #[attr] */

        const char *t=skip_ws(line); if(!*t) continue;

        parse_include(t, fe);
        parse_use(t, fe);
        parse_def(t, defs, def_count, max_defs, fe->name);
        parse_calls(t, fe);
    }
    fclose(f);
}
