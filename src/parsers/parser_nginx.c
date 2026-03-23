/* parser_nginx.c  -  Nginx configuration dependency parser
 *
 * Handles:
 *   include /etc/nginx/conf.d/*.conf    → internal includes
 *   include snippets/fastcgi.conf       → internal
 *   upstream mybackend { }             → FunctionIndex
 *   server { }                         → FunctionIndex
 *   proxy_pass http://mybackend        → call (upstream ref)
 *   proxy_pass http://localhost:3000   → external dep
 *   fastcgi_pass 127.0.0.1:9000        → external
 *   fastcgi_pass unix:/run/php-fpm.sock → external
 *   ssl_certificate /etc/ssl/cert.pem  → local file dep
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_nginx.h"
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
static int read_token(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&*s!=' '&&*s!='\t'&&*s!=';'&&*s!='{'&&*s!='\n'&&i<out_size-1)
        out[i++]=*s++;
    out[i]='\0'; return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if(!val||!val[0]) return;
    for(int i=0;i<*count;i++) if(strcmp(arr[i],val)==0) return;
    if(*count<max) strncpy(arr[(*count)++],val,MAX_SYMBOL-1);
}
static void add_def(FunctionDef *defs, int *count, int max,
                    const char *func, const char *file) {
    if(!func||!func[0]) return;
    for(int i=0;i<*count;i++)
        if(strcmp(defs[i].function,func)==0&&strcmp(defs[i].file,file)==0) return;
    if(*count<max) {
        strncpy(defs[*count].function,func,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* Track upstream names defined in this file */
#define MAX_UPSTREAMS 32
static char g_upstreams[MAX_UPSTREAMS][MAX_SYMBOL];
static int  g_upstream_count=0;

static void reg_upstream(const char *name) {
    for(int i=0;i<g_upstream_count;i++) if(strcmp(g_upstreams[i],name)==0) return;
    if(g_upstream_count<MAX_UPSTREAMS) strncpy(g_upstreams[g_upstream_count++],name,MAX_SYMBOL-1);
}
static int is_upstream(const char *name) {
    for(int i=0;i<g_upstream_count;i++) if(strcmp(g_upstreams[i],name)==0) return 1;
    return 0;
}

/* ================================================================ include */

static void parse_include(const char *line, FileEntry *fe) {
    const char *p=skip_ws(line+7); /* skip "include" */
    /* strip trailing ; */
    char path[MAX_PATH]={0}; int l=read_token(p,path,sizeof(path));
    if(!l) return;
    /* strip wildcards: /etc/nginx/conf.d/*.conf → conf.d */
    char clean[MAX_SYMBOL]={0};
    const char *base=path;
    for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
    /* strip wildcard from filename */
    const char *star=strchr(base,'*');
    if(star&&star>base) {
        /* directory include: store parent dir name */
        int blen=(int)(base-path);
        if(blen>1) {
            const char *dir=path+blen-2;
            while(dir>path&&*dir!='/'&&*dir!='\\') dir--;
            if(*dir=='/'||*dir=='\\') dir++;
            strncpy(clean,dir,MAX_SYMBOL-1);
            char *sl=strchr(clean,'/'); if(sl)*sl='\0';
            sl=strchr(clean,'\\'); if(sl)*sl='\0';
        }
    } else {
        /* specific file */
        strncpy(clean,base,MAX_SYMBOL-1);
        char *dot=strrchr(clean,'.'); if(dot)*dot='\0';
    }
    /* skip if just a wildcard or empty */
    if(clean[0]&&strcmp(clean,"*")!=0&&strcmp(clean,"**")!=0) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",clean);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ proxy_pass / fastcgi_pass */

static void parse_pass(const char *val, FileEntry *fe) {
    /* strip http:// or https:// */
    const char *p=val;
    if(starts_with(p,"http://"))  p+=7;
    if(starts_with(p,"https://")) p+=8;

    /* extract host/upstream name */
    char host[MAX_SYMBOL]={0}; int i=0;
    while(*p&&*p!='/'&&*p!=':'&&*p!=';'&&i<MAX_SYMBOL-1) host[i++]=*p++;
    host[i]='\0';

    if(!host[0]) return;

    if(is_upstream(host)) {
        /* reference to local upstream block */
        add_sym(fe->calls,&fe->call_count,MAX_CALLS,host);
    } else {
        /* external backend */
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,host);
    }
}

/* ================================================================ Public API */

void parser_nginx_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    g_upstream_count=0;

    /* Pass 1: collect upstream names */
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_nginx] Cannot open: %s\n",fe->path);return;}
    char line[MAX_LINE];
    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        char *cmt=strchr(line,'#'); if(cmt)*cmt='\0';
        const char *t=skip_ws(line);
        if(starts_with(t,"upstream ")) {
            const char *p=skip_ws(t+9);
            char nm[MAX_SYMBOL]={0}; read_token(p,nm,sizeof(nm));
            if(nm[0]) {
                reg_upstream(nm);
                add_def(defs,def_count,max_defs,nm,fe->name);
            }
        }
    }
    rewind(f);

    /* Pass 2: parse everything */
    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        char *cmt=strchr(line,'#'); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        if(starts_with(t,"include ")) {
            parse_include(t,fe);
        } else if(starts_with(t,"proxy_pass ")||starts_with(t,"fastcgi_pass ")) {
            const char *p=t;
            while(*p&&*p!=' '&&*p!='\t') p++;
            p=skip_ws(p);
            char val[MAX_SYMBOL]={0}; read_token(p,val,sizeof(val));
            if(val[0]) parse_pass(val,fe);
        } else if(starts_with(t,"server ")||starts_with(t,"server{")) {
            add_def(defs,def_count,max_defs,"server",fe->name);
        } else if(starts_with(t,"location ")) {
            const char *p=skip_ws(t+9);
            char loc[MAX_SYMBOL]={0}; read_token(p,loc,sizeof(loc));
            if(loc[0]) add_def(defs,def_count,max_defs,loc,fe->name);
        } else if(starts_with(t,"ssl_certificate ")&&!starts_with(t,"ssl_certificate_key")) {
            /* ssl cert file → local dep */
            const char *p=skip_ws(t+16);
            char path[MAX_SYMBOL]={0}; read_token(p,path,sizeof(path));
            if(path[0]&&path[0]=='/'&&strncmp(path,"/etc/",5)!=0&&strncmp(path,"/usr/",5)!=0&&strncmp(path,"/var/",5)!=0) {
                const char *base=path+strlen(path)-1;
                while(base>path&&*base!='/') base--;
                if(*base=='/') base++;
                char no_ext[MAX_SYMBOL]={0};
                strncpy(no_ext,base,MAX_SYMBOL-1);
                char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
                if(no_ext[0]) {
                    char tagged[MAX_SYMBOL]={0};
                    snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
                    add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
                }
            }
        }
    }
    fclose(f);
}
