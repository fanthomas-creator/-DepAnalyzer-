/* parser_cob.c  -  COBOL dependency parser
 *
 * COBOL has fixed-format (columns 7-72) and free-format.
 * We handle both by stripping the fixed-format indicators.
 *
 * Handles:
 *   PROGRAM-ID. MY-PROGRAM.           -> def
 *   COPY CUSTOMLIB.                   -> internal dep
 *   COPY "UTILS.CPY" OF LIBRARY.      -> internal + external
 *   CALL 'SUB-ROUTINE'                -> call
 *   CALL VARIABLE-NAME                -> call
 *   EXEC SQL INCLUDE SQLCA END-EXEC   -> external "sql"
 *   EXEC CICS LINK PROGRAM(name)      -> call
 *   PERFORM paragraph-name            -> call
 *   paragraph-name.                   -> def (paragraph)
 *   PROCEDURE DIVISION                -> structural
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_cob.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

/* Case-insensitive starts_with */
static int swi(const char *s,const char *p){
    while(*p){if(tolower((unsigned char)*s)!=tolower((unsigned char)*p))return 0;s++;p++;}return 1;
}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}

static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){
    if(!v||!v[0])return;
    for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;
    if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);
}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){
    if(!f||!f[0])return;
    for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;
    if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}
}

/* Read COBOL identifier: letters, digits, hyphens */
static int read_cob_ident(const char *s, char *o, int n) {
    int i=0;
    while(*s&&(isalnum((unsigned char)*s)||*s=='-')&&i<n-1)o[i++]=*s++;
    o[i]=0; return i;
}

/* Strip fixed-format column indicator (col 7 = * for comment, - for continuation) */
static const char *strip_fixed(const char *line) {
    /* If line is long enough for fixed format, check col 6 (0-indexed) */
    int len=(int)strlen(line);
    if(len>6&&line[6]=='*') return NULL; /* comment line */
    if(len>7) {
        /* content starts at col 7 (0-indexed) = position 7 */
        const char *t=ws(line+7);
        if(*t) return t;
    }
    return ws(line);
}

/* ================================================================ COPY parsing */

static void parse_copy(const char *line, FileEntry *fe) {
    const char *p=line;
    /* COPY LIBNAME.  or  COPY "FILE.CPY"  or  COPY LIB OF LIBRARY */
    if(!swi(p,"COPY ")) return;
    p=ws(p+5);

    char name[MAX_SYMBOL]={0};
    if(*p=='"'||*p=='\'') {
        char q=*p++;int i=0;
        while(*p&&*p!=q&&*p!='.'&&i<MAX_SYMBOL-1)name[i++]=*p++;
        name[i]=0;
    } else {
        read_cob_ident(p,name,sizeof(name));
    }
    if(!name[0]) return;

    /* Check for OF library -> external */
    const char *of=strstr(line," OF ");
    if(!of) of=strstr(line," IN ");
    if(of) {
        /* name is a copybook from an external library */
        char lib[MAX_SYMBOL]={0};
        const char *lp=ws(of+4);
        read_cob_ident(lp,lib,sizeof(lib));
        /* store library as external dep */
        if(lib[0]) as(fe->imports,&fe->import_count,MAX_IMPORTS,lib);
        /* store copybook name as internal */
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",name);
        as(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    } else {
        /* local copybook */
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",name);
        as(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ CALL parsing */

static void parse_call(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!swi(p,"CALL ")) return;
    p=ws(p+5);

    char name[MAX_SYMBOL]={0};
    if(*p=='\''||*p=='"') {
        char q=*p++;int i=0;
        while(*p&&*p!=q&&i<MAX_SYMBOL-1)name[i++]=*p++;
        name[i]=0;
    } else {
        read_cob_ident(p,name,sizeof(name));
    }
    if(name[0]) as(fe->calls,&fe->call_count,MAX_CALLS,name);
}

/* ================================================================ EXEC parsing */

static void parse_exec(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!swi(p,"EXEC ")) return;
    p=ws(p+5);

    if(swi(p,"SQL")) {
        as(fe->imports,&fe->import_count,MAX_IMPORTS,"sql");
        /* EXEC SQL INCLUDE SQLCA */
        const char *inc=strstr(p,"INCLUDE");
        if(!inc) inc=strstr(p,"include");
        if(inc) {
            char nm[MAX_SYMBOL]={0};
            read_cob_ident(ws(inc+7),nm,sizeof(nm));
            if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
        }
        return;
    }
    if(swi(p,"CICS")) {
        as(fe->imports,&fe->import_count,MAX_IMPORTS,"cics");
        /* EXEC CICS LINK PROGRAM(name) */
        const char *lp=strstr(p,"PROGRAM(");
        if(!lp) lp=strstr(p,"program(");
        if(lp) {
            char nm[MAX_SYMBOL]={0};int i=0;lp+=8;
            while(*lp&&*lp!=')'&&i<MAX_SYMBOL-1)nm[i++]=*lp++;nm[i]=0;
            if(nm[0])as(fe->calls,&fe->call_count,MAX_CALLS,nm);
        }
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=line;

    /* PROGRAM-ID. name. */
    if(swi(p,"PROGRAM-ID")) {
        const char *dot=strchr(p,'.');
        if(dot){p=ws(dot+1);char nm[MAX_SYMBOL]={0};read_cob_ident(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,filename);}
        return;
    }
    /* FUNCTION-ID. name. */
    if(swi(p,"FUNCTION-ID")) {
        const char *dot=strchr(p,'.');
        if(dot){p=ws(dot+1);char nm[MAX_SYMBOL]={0};read_cob_ident(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,filename);}
        return;
    }
}

/* Paragraph: PARAGRAPH-NAME. at area A (cols 8-11 in fixed, 0+ in free)
   Heuristic: line ends with '.' and is an identifier */
static void parse_paragraph(const char *orig_line, const char *stripped,
                              FunctionDef *defs, int *def_count,
                              int max_defs, const char *filename) {
    /* Must start at column A (col 7 = position 7, so stripped starts after spaces) */
    /* Simplified: if it looks like PARA-NAME. and nothing else */
    const char *p=stripped;
    char nm[MAX_SYMBOL]={0}; int l=read_cob_ident(p,nm,sizeof(nm));
    if(!l||!nm[0]) return;
    p=ws(p+l);
    /* should end with . and nothing else (or SECTION) */
    if(*p=='.') {
        /* skip keywords that end with . */
        static const char *skip_kw[]={"PROGRAM-ID","FUNCTION-ID","DATA","PROCEDURE",
            "ENVIRONMENT","IDENTIFICATION","CONFIGURATION","INPUT-OUTPUT",
            "FILE","WORKING-STORAGE","LOCAL-STORAGE","LINKAGE","SCREEN",
            "REPORT","COMMUNICATION","DIVISION","SECTION",NULL};
        for(int i=0;skip_kw[i];i++) {
            if(!strcasecmp(nm,skip_kw[i])) return;
        }
        ad(defs,def_count,max_defs,nm,filename);
    } else if(swi(p,"SECTION.")) {
        /* SECTION name */
        ad(defs,def_count,max_defs,nm,filename);
    }
    (void)orig_line;
}

/* PERFORM paragraph-name */
static void parse_perform(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!swi(p,"PERFORM ")) return;
    p=ws(p+8);
    /* skip VARYING/UNTIL/TIMES etc. */
    static const char *kw[]={"VARYING","UNTIL","TIMES","THRU","THROUGH","FOREVER","WITH",NULL};
    for(int i=0;kw[i];i++)if(swi(p,kw[i]))return;
    char nm[MAX_SYMBOL]={0};read_cob_ident(p,nm,sizeof(nm));
    if(nm[0])as(fe->calls,&fe->call_count,MAX_CALLS,nm);
}

/* ================================================================ Public API */

void parser_cob_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r"); if(!f)return;
    char line[MAX_LINE];

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* Fixed format: col 6 (0-indexed) = * means comment */
        if(strlen(line)>6&&line[6]=='*') continue;
        /* Sequence number area (cols 0-5) -- just work with ws-stripped */
        const char *t=ws(line); if(!*t) continue;
        /* Free-format comment: * or / in col 0, or *> anywhere */
        if(*t=='*'||strncmp(t,"*>",2)==0) continue;

        /* Convert to uppercase copy for keyword matching */
        char upper[MAX_LINE]={0};
        for(int i=0;t[i]&&i<MAX_LINE-1;i++)
            upper[i]=(char)toupper((unsigned char)t[i]);

        if(swi(upper,"COPY "))           parse_copy(upper,fe);
        else if(swi(upper,"CALL "))      parse_call(upper,fe);
        else if(swi(upper,"EXEC "))      parse_exec(upper,fe);
        else if(swi(upper,"PERFORM "))   parse_perform(upper,fe);
        else if(swi(upper,"PROGRAM-ID")||swi(upper,"FUNCTION-ID"))
                                          parse_def(upper,defs,def_count,max_defs,fe->name);
        else parse_paragraph(t,upper,defs,def_count,max_defs,fe->name);
    }
    fclose(f);
}
