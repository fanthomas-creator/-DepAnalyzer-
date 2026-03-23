/* parser_sh.c  -  Shell script dependency parser
 *
 * Handles:
 *   source ./lib.sh / . ./utils.sh       → internal (local:lib)
 *   bash ./scripts/run.sh                → internal
 *   ./deploy.sh                          → internal
 *   curl URL / wget URL                  → external (domain)
 *   apt install pkg / apt-get install    → external package
 *   pip install / pip3 install           → external package
 *   npm install / yarn add               → external package
 *   brew install / yum install / dnf     → external package
 *   function foo() { } / function foo {} → FunctionIndex
 *   foo() {                              → FunctionIndex
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_sh.h"
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
    while (*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='-'||*s=='.')&&i<out_size-1)
        out[i++]=*s++;
    out[i]='\0'; return i;
}
static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->import_count;i++) if(strcmp(fe->imports[i],val)==0) return;
    if (fe->import_count<MAX_IMPORTS) strncpy(fe->imports[fe->import_count++],val,MAX_SYMBOL-1);
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

/* Get basename without extension from a path */
static void path_to_module(const char *path, char *out, int out_size) {
    /* strip leading ./ */
    while (*path=='.'||*path=='/') path++;
    const char *base=path;
    for (const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
    strncpy(out,base,out_size-1); out[out_size-1]='\0';
    char *dot=strrchr(out,'.'); if(dot) *dot='\0';
}

/* Extract domain from URL */
static void url_domain(const char *url, char *out, int out_size) {
    const char *p=url;
    if (starts_with(p,"https://")) p+=8;
    else if (starts_with(p,"http://"))  p+=7;
    else if (starts_with(p,"ftp://"))   p+=6;
    else { out[0]='\0'; return; }
    int i=0;
    while (*p&&*p!='/'&&*p!='?'&&*p!='#'&&i<out_size-1) out[i++]=*p++;
    out[i]='\0';
}

/* ================================================================ Source / dot */

static void parse_source(const char *line, FileEntry *fe) {
    const char *p=line;
    if (starts_with(p,"source ")) p=skip_ws(p+7);
    else if (*p=='.'&&(*(p+1)==' '||*(p+1)=='\t')) p=skip_ws(p+2);
    else return;

    /* strip quotes */
    if (*p=='"'||*p=='\'') { char q=*p++; char *end=strchr(p,q); if(end)*end='\0'; }

    char mod[MAX_SYMBOL]={0};
    path_to_module(p,mod,sizeof(mod));
    if (mod[0]) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",mod);
        add_import(fe,tagged);
    }
}

/* ================================================================ Script execution */

static void parse_exec(const char *line, FileEntry *fe) {
    /* bash ./x.sh / sh ./x.sh / ./x.sh */
    const char *p=line;
    const char *execs[]={"bash ","sh ","zsh ","fish ","dash ",NULL};
    for (int i=0;execs[i];i++) {
        if (starts_with(p,execs[i])) { p=skip_ws(p+strlen(execs[i])); break; }
    }
    if (*p=='.'&&(*(p+1)=='/'||*(p+1)=='\\')) {
        /* relative script call */
        char mod[MAX_SYMBOL]={0};
        path_to_module(p,mod,sizeof(mod));
        if (mod[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",mod);
            add_import(fe,tagged);
        }
    }
}

/* ================================================================ curl / wget */

static void parse_url_tool(const char *line, FileEntry *fe) {
    const char *tools[]={"curl ","wget ","curl -","wget -",NULL};
    for (int i=0;tools[i];i++) {
        const char *p=strstr(line,tools[i]); if(!p) continue;
        /* find URL argument */
        const char *q=p+strlen(tools[i]);
        /* skip flags: -s -O -o etc */
        while (*q=='-') { while(*q&&*q!=' '&&*q!='\t') q++; q=skip_ws(q); }
        /* strip quotes */
        if (*q=='"'||*q=='\'') q++;
        char url[MAX_PATH]={0}; int j=0;
        while (*q&&*q!='"'&&*q!='\''&&*q!=' '&&*q!='\t'&&*q!='\n'&&j<MAX_PATH-1)
            url[j++]=*q++;
        url[j]='\0';
        char domain[MAX_SYMBOL]={0};
        url_domain(url,domain,sizeof(domain));
        if (domain[0]) add_import(fe,domain);
        return;
    }
}

/* ================================================================ Package managers */

static void parse_pkg_manager(const char *line, FileEntry *fe) {
    /* apt install PKG / apt-get install PKG / yum install / dnf install
     * pip install PKG / pip3 install PKG
     * npm install PKG / yarn add PKG
     * brew install PKG / cargo install PKG / go install PKG */

    typedef struct { const char *cmd; int skip; } PkgTool;
    static const PkgTool tools[]={
        {"apt install ",0},{"apt-get install ",0},
        {"yum install ",0},{"dnf install ",0},{"pacman -S ",0},
        {"pip install ",0},{"pip3 install ",0},{"pip3.11 install ",0},
        {"npm install ",0},{"npm i ",0},
        {"yarn add ",0},{"yarn install ",0},
        {"brew install ",0},{"cargo install ",0},
        {"go install ",0},{"gem install ",0},
        {"apk add ",0},{"zypper install ",0},
        {NULL,0}
    };

    const char *p=skip_ws(line);
    for (int i=0;tools[i].cmd;i++) {
        if (!starts_with(p,tools[i].cmd)) continue;
        p=skip_ws(p+strlen(tools[i].cmd));
        /* collect package names (may be multiple: apt install git curl) */
        while (*p&&*p!='\n'&&*p!=';'&&*p!='#') {
            if (*p=='-') { /* skip flags */ while(*p&&*p!=' '&&*p!='\t') p++; p=skip_ws(p); continue; }
            if (*p=='\\'||*p=='|'||*p=='&') break;
            char pkg[MAX_SYMBOL]={0}; int j=0;
            while (*p&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!=';'&&*p!='\\' &&j<MAX_SYMBOL-1)
                pkg[j++]=*p++;
            pkg[j]='\0';
            /* strip version: pkg>=1.0 → pkg */
            char *gt=strchr(pkg,'>'); if(gt)*gt='\0';
            char *lt=strchr(pkg,'<'); if(lt)*lt='\0';
            char *eq=strchr(pkg,'='); if(eq)*eq='\0';
            if (pkg[0]&&pkg[0]!='-') add_import(fe,pkg);
            p=skip_ws(p);
        }
        return;
    }
}

/* ================================================================ Function definition */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* function foo { or function foo() { */
    if (starts_with(p,"function ")) {
        p=skip_ws(p+9);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* foo() { */
    char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
    if (l>0) {
        const char *after=skip_ws(p+l);
        if (*after=='(') {
            after=skip_ws(after+1);
            if (*after==')') add_def(defs,def_count,max_defs,nm,filename);
        }
    }
}

/* ================================================================ Public API */

void parser_sh_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_sh] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_heredoc=0;
    char heredoc_end[64]={0};

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* heredoc handling */
        if (!in_heredoc) {
            char *hd=strstr(line,"<<"); if (!hd) hd=strstr(line,"<<-");
            if (hd) {
                char *marker=hd+2; if(*marker=='-') marker++;
                while(*marker==' '||*marker=='\t') marker++;
                /* strip quotes from heredoc marker */
                if (*marker=='\''||*marker=='"') { char q=*marker++; char *e=strchr(marker,q); if(e)*e='\0'; }
                int ml=0;
                while(marker[ml]&&marker[ml]!='\n'&&marker[ml]!='\r'&&ml<63) {
                    heredoc_end[ml]=marker[ml]; ml++;
                }
                heredoc_end[ml]='\0';
                if (heredoc_end[0]) in_heredoc=1;
            }
        } else {
            if (strcmp(skip_ws(line),heredoc_end)==0) { in_heredoc=0; heredoc_end[0]='\0'; }
            continue;
        }

        /* strip # comments (not inside strings — simplified) */
        char *cmt=strchr(line,'#');
        while (cmt) {
            if (cmt==line||*(cmt-1)==' '||*(cmt-1)=='\t'||*(cmt-1)==';') { *cmt='\0'; break; }
            cmt=strchr(cmt+1,'#');
        }

        const char *t=skip_ws(line); if(!*t) continue;

        /* skip shebang */
        if (starts_with(t,"#!")) continue;

        parse_source(t, fe);
        parse_exec(t, fe);
        parse_url_tool(t, fe);
        parse_pkg_manager(t, fe);
        parse_def(t, defs, def_count, max_defs, fe->name);
    }
    fclose(f);
}
