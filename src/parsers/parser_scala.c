/* parser_scala.c  -  Scala dependency parser
 *
 * Handles:
 *   import com.example.MyClass        → last component "MyClass"
 *   import scala.collection._         → "scala" (external)
 *   import java.util.{List, Map}       → grouped imports
 *   import akka.actor.ActorSystem      → "ActorSystem"
 *   class / case class / object        → FunctionIndex
 *   trait / abstract class / sealed    → FunctionIndex
 *   def method / val fn = (x) =>       → FunctionIndex
 *   extends X / with X                 → X as dependency
 *   @Annotation                        → dependency hint
 *   obj.method() / Class.method()      → calls
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_scala.h"
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
    /* Scala allows backtick identifiers */
    if (*s=='`'){s++;while(*s&&*s!='`'&&i<out_size-1)out[i++]=*s++;out[i]='\0';return i+2;}
    while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<out_size-1) out[i++]=*s++;
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

/* ================================================================ Import parsing */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    if (!starts_with(p,"import ")) return;
    p=skip_ws(p+7);

    /* read full dotted path */
    char path[MAX_PATH]={0}; int i=0;
    while (*p&&*p!='{'&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!=';'&&i<MAX_PATH-1)
        path[i++]=*p++;
    path[i]='\0';

    /* strip trailing ._ or .* (wildcard) */
    if (i>=2&&(strcmp(path+i-2,"._")==0||strcmp(path+i-2,".*")==0))
        path[i-2]='\0';

    /* get last component */
    const char *last=path;
    for (const char *c=path;*c;c++) if(*c=='.') last=c+1;

    /* Store root package (first component) as the external dep identifier.
     * "import akka.actor.ActorSystem" → "akka"
     * "import scala.collection._"     → "scala"
     * "import java.util.List"         → "java"
     * Single-component imports (no dot): store as-is */
    char root[MAX_SYMBOL]={0}; int ri=0;
    const char *rp=path;
    while (*rp&&*rp!='.'&&ri<MAX_SYMBOL-1) root[ri++]=*rp++;
    root[ri]='\0';
    if (root[0]&&root[0]!='_'&&root[0]!='*')
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root);

    /* grouped: import pkg.{A, B, C} */
    if (*p=='{') {
        p++;
        while (*p&&*p!='}') {
            p=skip_ws(p);
            char sub[MAX_SYMBOL]={0}; int l=read_ident(p,sub,sizeof(sub)); p+=l;
            /* handle alias: A => B */
            const char *arrow=strstr(p,"=>");
            if (arrow&&arrow-p<10) { p=skip_ws(arrow+2); p+=read_ident(p,sub,sizeof(sub)); }
            if (sub[0]&&sub[0]!='_') add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,sub);
            while (*p&&*p!=','&&*p!='}') p++;
            if (*p==',') p++;
        }
    }
}

/* ================================================================ Inheritance / mixin */

static void parse_extends(const char *line, FileEntry *fe) {
    /* extends Foo with Bar with Baz */
    const char *p=strstr(line,"extends ");
    while (p) {
        if (p>line&&isalnum((unsigned char)*(p-1))) { p=strstr(p+1,"extends "); continue; }
        p=skip_ws(p+8);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
        break;
    }
    /* with Trait */
    p=line;
    while ((p=strstr(p," with "))!=NULL) {
        p=skip_ws(p+6);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip modifiers */
    const char *mods[]={"public ","private ","protected ","override ",
        "abstract ","final ","sealed ","lazy ","implicit ","inline ",
        "transparent ","opaque ","open ","case ",NULL};
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++)
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
    }

    /* type declarations */
    const char *kws[]={"class ","object ","trait ","enum ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* def method */
    if (starts_with(p,"def ")) {
        p=skip_ws(p+4);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* val/var fn = (x) => or fn = _ => */
    if (starts_with(p,"val ")||starts_with(p,"var ")) {
        p=skip_ws(p+4);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if (!l) return;
        p=skip_ws(p+l);
        if (*p==':'){while(*p&&*p!='=') p++;} /* skip type annotation */
        if (*p=='=') {
            const char *rhs=skip_ws(p+1);
            if (*rhs=='('||strstr(rhs,"=>")) add_def(defs,def_count,max_defs,nm,filename);
        }
    }
}

/* ================================================================ Call parsing */

static const char *SCALA_KW[]={
    "if","else","for","while","do","match","case","return","throw","try",
    "catch","finally","new","import","package","object","class","trait",
    "def","val","var","type","extends","with","override","abstract",
    "final","sealed","lazy","implicit","null","true","false","this",
    "super","yield","given","using","enum","export","then","end",
    "println","print","printf","require","assert","assume",NULL
};
static int is_kw(const char *s){
    for(int i=0;SCALA_KW[i];i++) if(strcmp(SCALA_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
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

void parser_scala_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_scala] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_bc=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        if (!in_bc&&strstr(line,"/*")) in_bc=1;
        if (in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if (*t=='@') {
            /* annotation: @Annotation */
            char nm[MAX_SYMBOL]={0}; read_ident(t+1,nm,sizeof(nm));
            if (nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
            continue;
        }
        if (starts_with(t,"import ")) parse_import(t,fe);
        parse_extends(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
