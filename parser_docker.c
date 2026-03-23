/* parser_docker.c  -  Dockerfile dependency parser
 *
 * Handles:
 *   FROM nginx:1.21               → external image "nginx"
 *   FROM ubuntu:22.04 AS builder  → external "ubuntu", alias "builder"
 *   FROM builder AS final         → internal multi-stage ref "builder"
 *   COPY --from=builder /src /dst → internal multi-stage "builder"
 *   RUN apt-get install -y curl git nginx
 *   RUN pip install requests flask
 *   RUN npm install express
 *   RUN go get github.com/gin-gonic/gin
 *   ADD ./config.json /app/       → internal local file
 *   COPY ./src/ /app/             → internal local dir
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_docker.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int sw_ci(const char *s, const char *pre) {
    while(*pre) {
        if(tolower((unsigned char)*s)!=tolower((unsigned char)*pre)) return 0;
        s++;pre++;
    }
    return 1;
}
static const char *skip_ws(const char *s) {
    while(*s==' '||*s=='\t') s++; return s;
}
static int read_token(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&*s!=' '&&*s!='\t'&&*s!='\n'&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
}
static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->import_count;i++) if(strcmp(fe->imports[i],val)==0) return;
    if (fe->import_count<MAX_IMPORTS)
        strncpy(fe->imports[fe->import_count++],val,MAX_SYMBOL-1);
}
static void add_call(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->call_count;i++) if(strcmp(fe->calls[i],val)==0) return;
    if (fe->call_count<MAX_CALLS)
        strncpy(fe->calls[fe->call_count++],val,MAX_SYMBOL-1);
}

/* Strip image tag: nginx:1.21 → nginx  / registry.io/app:v1 → app */
static void strip_image_tag(const char *s, char *out, int out_size) {
    /* get last component after / */
    const char *base=s;
    for(const char *c=s;*c&&*c!=':'&&*c!=' ';c++) if(*c=='/') base=c+1;
    int i=0;
    while(*base&&*base!=':'&&*base!='@'&&*base!=' '&&i<out_size-1)
        out[i++]=*base++;
    out[i]='\0';
}

/* Track multi-stage aliases: FROM x AS alias → alias is internal */
#define MAX_ALIASES 32
static char g_aliases[MAX_ALIASES][MAX_SYMBOL];
static int  g_alias_count=0;

static void register_alias(const char *name) {
    if (!name||!name[0]) return;
    for(int i=0;i<g_alias_count;i++) if(strcmp(g_aliases[i],name)==0) return;
    if(g_alias_count<MAX_ALIASES) strncpy(g_aliases[g_alias_count++],name,MAX_SYMBOL-1);
}
static int is_alias(const char *name) {
    for(int i=0;i<g_alias_count;i++) if(strcmp(g_aliases[i],name)==0) return 1;
    return 0;
}

/* ================================================================ FROM */

static void parse_from(const char *line, FileEntry *fe) {
    const char *p=skip_ws(line+4); /* skip "FROM" */
    if (!*p) return;

    /* platform flag: --platform=linux/amd64 */
    if (sw_ci(p,"--platform")) { while(*p&&*p!=' ') p++; p=skip_ws(p); }

    char image[MAX_SYMBOL]={0};
    read_token(p,image,sizeof(image));
    p=skip_ws(p+strlen(image));

    if (is_alias(image)) {
        /* multi-stage reference → internal */
        add_call(fe,image);
    } else {
        /* external image */
        char name[MAX_SYMBOL]={0};
        strip_image_tag(image,name,sizeof(name));
        if (strcmp(name,"scratch")!=0) add_import(fe,name);
    }

    /* AS alias */
    if (sw_ci(p,"as ")) {
        p=skip_ws(p+3);
        char alias[MAX_SYMBOL]={0}; read_token(p,alias,sizeof(alias));
        if (alias[0]) register_alias(alias);
    }
}

/* ================================================================ RUN - package installs */

static void parse_run_packages(const char *line, FileEntry *fe) {
    const char *p=line;

    typedef struct { const char *cmd; } PkgMgr;
    static const PkgMgr mgrs[]={
        {"apt-get install"},{"apt install"},{"yum install"},
        {"dnf install"},{"apk add"},{"pacman -S"},
        {"pip install"},{"pip3 install"},
        {"npm install"},{"npm i"},{"yarn add"},
        {"gem install"},{"brew install"},
        {"cargo install"},{"go get"},
        {"composer require"},
        {NULL}
    };

    for(int i=0;mgrs[i].cmd;i++) {
        const char *mp=strstr(p,mgrs[i].cmd);
        if(!mp) continue;
        mp=skip_ws(mp+strlen(mgrs[i].cmd));
        while(*mp&&*mp!='\n'&&*mp!=';'&&*mp!='&'&&*mp!='|'&&*mp!='\\') {
            if(*mp=='-'){while(*mp&&*mp!=' '&&*mp!='\t')mp++;mp=skip_ws(mp);continue;}
            char tok[MAX_SYMBOL]={0}; int l=read_token(mp,tok,sizeof(tok));
            if(l) {
                /* strip version: pkg>=1.0 → pkg */
                char *gt=strchr(tok,'>');if(gt)*gt='\0';
                char *lt=strchr(tok,'<');if(lt)*lt='\0';
                char *eq=strchr(tok,'=');if(eq)*eq='\0';
                char *at=strchr(tok,'@');if(at)*at='\0';
                if(tok[0]&&tok[0]!='-') add_import(fe,tok);
            }
            mp+=l; mp=skip_ws(mp);
        }
        return;
    }
}

/* ================================================================ COPY / ADD */

static void parse_copy(const char *line, FileEntry *fe) {
    const char *p=skip_ws(line);
    /* skip COPY or ADD keyword */
    while(*p&&*p!=' '&&*p!='\t') p++;
    p=skip_ws(p);

    /* --from=stagename → internal */
    if (sw_ci(p,"--from=")) {
        p+=7;
        char stage[MAX_SYMBOL]={0}; read_token(p,stage,sizeof(stage));
        if(stage[0]) add_call(fe,stage);
        return;
    }

    /* skip other flags */
    while(sw_ci(p,"--")) { while(*p&&*p!=' '&&*p!='\t') p++; p=skip_ws(p); }

    /* source (first token) */
    char src[MAX_PATH]={0}; int l=read_token(p,src,sizeof(src));
    if(!l) return;

    /* if it starts with . or / → local file ref */
    if(src[0]=='.'||src[0]=='/') {
        const char *base=src;
        for(const char *c=src;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char no_ext[MAX_SYMBOL]={0};
        strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot) *dot='\0';
        if(no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_import(fe,tagged);
        }
    }
}

/* ================================================================ Public API */

void parser_docker_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;
    g_alias_count=0; /* reset per-file */

    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_docker] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* strip # comments */
        char *cmt=strchr(line,'#'); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if     (sw_ci(t,"FROM "))  parse_from(t,fe);
        else if(sw_ci(t,"RUN "))   parse_run_packages(t+4,fe);
        else if(sw_ci(t,"COPY "))  parse_copy(t,fe);
        else if(sw_ci(t,"ADD "))   parse_copy(t,fe);
    }
    fclose(f);
}
