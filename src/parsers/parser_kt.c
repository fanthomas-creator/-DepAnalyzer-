/* parser_kt.c  -  Kotlin dependency parser
 *
 * Handles:
 *   import com.example.MyClass
 *   import org.springframework.boot.*
 *   class Foo / data class / sealed class / abstract class
 *   object MyObject / companion object
 *   interface Foo
 *   fun myFunc(...) / suspend fun / inline fun / operator fun
 *   @Annotation / @Annotation(params)
 *   obj.method() / Companion.method() / Class::method
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_kt.h"
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
    /* Kotlin backtick identifiers: `my ident` */
    if (*s=='`') {
        s++;
        while (*s&&*s!='`'&&i<out_size-1) out[i++]=*s++;
        out[i]='\0'; return i+2;
    }
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

/* ================================================================ Import parsing */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    if (!starts_with(p,"import ")) return;
    p=skip_ws(p+7);

    char path[MAX_PATH]={0}; int i=0;
    while (*p&&*p!=' '&&*p!='\t'&&*p!='\n'&&i<MAX_PATH-1) path[i++]=*p++;
    path[i]='\0';

    /* strip wildcard */
    if (i>=2&&strcmp(path+i-2,".*")==0) path[i-2]='\0';

    /* get last component */
    const char *last=path;
    for (const char *c=path;*c;c++) if(*c=='.') last=c+1;

    if (last[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,last);
}

/* ================================================================ Annotation */

static void parse_annotation(const char *line, FileEntry *fe) {
    const char *p=line;
    if (*p!='@') return;
    p++;
    /* skip target qualifiers like @get: @set: */
    const char *colon=strchr(p,':');
    if (colon&&colon-p<10) p=skip_ws(colon+1);
    if (*p=='@') p++;
    char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
    /* skip kotlin built-ins */
    if (strcmp(nm,"JvmStatic")==0||strcmp(nm,"JvmField")==0||
        strcmp(nm,"JvmOverloads")==0||strcmp(nm,"Throws")==0||
        strcmp(nm,"Suppress")==0||strcmp(nm,"Deprecated")==0) return;
    if (nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip modifiers */
    const char *mods[]={"public ","private ","protected ","internal ","open ",
        "abstract ","sealed ","data ","inline ","operator ","infix ","tailrec ",
        "override ","external ","actual ","expect ","suspend ","companion ",NULL};
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++){
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
        }
    }

    /* class / interface / object / enum class */
    const char *kws[]={"class ","interface ","object ","enum class ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* fun funcName */
    if (starts_with(p,"fun ")) {
        p=skip_ws(p+4);
        /* skip generic: <T> */
        if (*p=='<'){while(*p&&*p!='>') p++;if(*p) p++;p=skip_ws(p);}
        /* skip receiver type: Foo.funcName */
        const char *dot=NULL;
        const char *q=p;
        while (*q&&(isalnum((unsigned char)*q)||*q=='_'||*q=='.'||*q=='?')) {
            if (*q=='.') dot=q;
            q++;
        }
        if (dot&&*(q)=='(') {
            /* receiver.funcName( */
            p=dot+1;
        }
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *KT_KW[]={
    "if","else","when","for","while","do","return","break","continue",
    "try","catch","finally","throw","is","as","in","!in","!is","null",
    "true","false","this","super","object","class","interface","fun",
    "val","var","by","get","set","field","init","constructor","it",
    "let","run","also","apply","with","to","and","or","not",
    "import","package","typealias","typeof","reified","crossinline",
    "noinline","vararg","lateinit","const","external","actual","expect",
    "print","println","readLine","TODO","error","check","require",NULL
};
static int is_kw(const char *s){
    for(int i=0;KT_KW[i];i++) if(strcmp(KT_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* :: reference */
            while (p[0]==':'&&p[1]==':'){
                p+=2;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            /* . chaining */
            while (*p=='.'||(*p=='?'&&*(p+1)=='.')){
                p+=(*p=='?')?2:1;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if (*p=='('&&!is_kw(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_kt_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_kt] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        if (!in_block_comment&&strstr(line,"/*")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"*/"))in_block_comment=0;continue;}

        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        if (*t=='@')                  parse_annotation(t,fe);
        if (starts_with(t,"import ")) parse_import(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
