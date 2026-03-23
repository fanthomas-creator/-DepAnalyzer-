/* parser_java.c  -  Java dependency parser
 *
 * Handles:
 *   package com.example.app
 *   import com.example.MyClass
 *   import static java.lang.Math.PI
 *   import java.util.*
 *   class Foo / interface Foo / enum Foo / record Foo
 *   @SpringBootApplication / @Autowired → annotations as deps
 *   public void myMethod() / static int myFunc()
 *   obj.method() / MyClass.staticMethod()
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_java.h"
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
    while (*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='$')&&i<out_size-1) out[i++]=*s++;
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

/* ================================================================ Import parsing
 * "import com.example.MyClass;"
 * → root package = "com" (external if java/javax/org/com, internal if matches project)
 * → last component = "MyClass" (stored as the symbol)
 */
static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    if (!starts_with(p,"import ")) return;
    p=skip_ws(p+7);
    /* skip "static" modifier */
    if (starts_with(p,"static ")) p=skip_ws(p+7);

    /* read full dotted path */
    char path[MAX_PATH]={0}; int i=0;
    while (*p&&*p!=';'&&*p!=' '&&*p!='\t'&&i<MAX_PATH-1) path[i++]=*p++;
    path[i]='\0';
    /* strip trailing .* */
    if (i>=2&&strcmp(path+i-2,".*")==0) path[i-2]='\0';
    if (!path[0]) return;

    /* get root package */
    char root[MAX_SYMBOL]={0}; int j=0;
    for (j=0;path[j]&&path[j]!='.'&&j<MAX_SYMBOL-1;j++) root[j]=path[j];
    root[j]='\0';

    /* get last component (class name) */
    const char *last=path;
    for (const char *c=path;*c;c++) if(*c=='.') last=c+1;

    /* store the last component as import symbol */
    add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,last[0]?last:root);
}

/* ================================================================ Annotation parsing */

static void parse_annotation(const char *line, FileEntry *fe) {
    /* @AnnotationName or @AnnotationName(...) */
    const char *p=line;
    if (*p!='@') return;
    p++;
    char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
    /* ignore java built-in annotations */
    if (strcmp(nm,"Override")==0||strcmp(nm,"Deprecated")==0||
        strcmp(nm,"SuppressWarnings")==0||strcmp(nm,"SafeVarargs")==0||
        strcmp(nm,"FunctionalInterface")==0) return;
    if (nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
}

/* ================================================================ Definition parsing */

static const char *JAVA_MODS[]={"public ","private ","protected ","static ",
    "final ","abstract ","native ","synchronized ","transient ","volatile ",
    "default ","strictfp ","sealed ","non-sealed ",NULL};

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip modifiers */
    int changed=1;
    while(changed){changed=0;
        for(int i=0;JAVA_MODS[i];i++){
            if(starts_with(p,JAVA_MODS[i])){p=skip_ws(p+strlen(JAVA_MODS[i]));changed=1;}
        }
    }

    /* class / interface / enum / record */
    const char *kws[]={"class ","interface ","enum ","record ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* method: ReturnType methodName(  — heuristic: two idents then ( */
    char first[MAX_SYMBOL]={0}; int fl=read_ident(p,first,sizeof(first));
    if (!fl) return;
    const char *after=skip_ws(p+fl);
    /* skip generic return type: List<Foo> */
    if (*after=='<'){while(*after&&*after!='>') after++;if(*after) after++;}
    after=skip_ws(after);
    /* skip array [] */
    while(*after=='['||*after==']') after++;
    after=skip_ws(after);
    char second[MAX_SYMBOL]={0}; int sl=read_ident(after,second,sizeof(second));
    if (!sl) return;
    const char *after2=skip_ws(after+sl);
    if (*after2=='(') {
        /* second is the method name */
        if (second[0]&&!starts_with(second,"return")&&islower((unsigned char)second[0]))
            add_def(defs,def_count,max_defs,second,filename);
    }
}

/* ================================================================ Call parsing */

static const char *JAVA_KW[]={
    "if","else","for","while","do","switch","case","default","return",
    "break","continue","try","catch","finally","throw","throws","new",
    "class","interface","enum","record","extends","implements","import",
    "package","static","final","abstract","public","private","protected",
    "void","int","long","short","byte","char","float","double","boolean",
    "null","true","false","this","super","instanceof","assert","synchronized",
    "volatile","transient","native","strictfp","sealed","permits","yield",NULL
};
static int is_kw(const char *s){
    for(int i=0;JAVA_KW[i];i++) if(strcmp(JAVA_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_'||*p=='$') {
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

void parser_java_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_java] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        if (!in_block_comment&&strstr(line,"/*")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"*/"))in_block_comment=0;continue;}

        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        if (*t=='@')              parse_annotation(t,fe);
        if (starts_with(t,"import ")) parse_import(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
