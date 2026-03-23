/* parser_make.c  -  Makefile dependency parser
 *
 * Handles:
 *   include config.mk              → internal
 *   include $(SRCDIR)/rules.mk     → internal (stripped of $(...))
 *   -include optional.mk           → internal (optional)
 *   sinclude utils.mk              → internal
 *
 *   target: dep1 dep2 dep3
 *     → target → FunctionIndex
 *     → dep1/dep2/dep3 → calls (cross-target deps)
 *
 *   CC = gcc / CFLAGS = -Wall      → variable defs (stored as calls)
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_make.h"
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
    while(*s&&*s!=' '&&*s!='\t'&&*s!='\n'&&*s!='\r'&&i<out_size-1)
        out[i++]=*s++;
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

/* Strip $(VAR) / ${VAR} expansions from a token, leaving the basename */
static void strip_make_vars(const char *in, char *out, int out_size) {
    int i=0;
    while(*in&&i<out_size-1) {
        if (*in=='$'&&(*(in+1)=='('||*(in+1)=='{')) {
            char close=*(in+1)=='('?')':'}';
            in+=2;
            while(*in&&*in!=close) in++;
            if(*in) in++;
            continue;
        }
        out[i++]=*in++;
    }
    out[i]='\0';
    /* get basename */
    char *slash=strrchr(out,'/');
    char *bslash=strrchr(out,'\\');
    char *sep=slash?(bslash&&bslash>slash?bslash:slash):(bslash?bslash:NULL);
    if (sep&&*(sep+1)) {
        int blen=(int)strlen(sep+1);
        memmove(out,sep+1,(size_t)(blen+1));
    }
}

/* Skip automatic targets and special vars that aren't real file deps */
static int skip_dep(const char *s) {
    if (!s||!s[0]) return 1;
    if (s[0]=='.'&&s[1]=='\0') return 1;
    if (s[0]=='$') return 1;
    if (strcmp(s,"PHONY")==0||strcmp(s,"FORCE")==0||
        strcmp(s,"all")==0||strcmp(s,"clean")==0||
        strcmp(s,"install")==0||strcmp(s,"distclean")==0) return 1;
    return 0;
}

/* ================================================================ Public API */

void parser_make_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_make] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_recipe=0; /* lines starting with TAB are recipe lines */

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* recipe lines (start with TAB) */
        if (line[0]=='\t') { in_recipe=1; continue; }
        in_recipe=0;

        /* strip # comments */
        char *cmt=strchr(line,'#'); if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        /* include / -include / sinclude */
        if (starts_with(t,"include ")||starts_with(t,"-include ")||
            starts_with(t,"sinclude ")) {
            const char *p=t;
            while(*p&&*p!=' '&&*p!='\t') p++;
            p=skip_ws(p);
            /* may include multiple files */
            while (*p) {
                char tok[MAX_SYMBOL]={0}; int l=read_token(p,tok,sizeof(tok));
                if (l) {
                    char clean[MAX_SYMBOL]={0};
                    strip_make_vars(tok,clean,sizeof(clean));
                    if (clean[0]) {
                        char stem[MAX_SYMBOL]={0};
                        strncpy(stem,clean,MAX_SYMBOL-1);
                        char *mkd=strrchr(stem,'.'); if(mkd)*mkd='\0';
                        if(stem[0]){
                            char tagged[MAX_SYMBOL]={0};
                            snprintf(tagged,sizeof(tagged),"local:%s",stem);
                            add_import(fe,tagged);
                        }
                    }
                }
                p+=l; p=skip_ws(p);
            }
            continue;
        }

        /* target: deps  (colon present, not inside recipe) */
        const char *colon=strchr(t,':');
        if (colon&&colon>t&&*(colon+1)!=':') { /* avoid :: rules */
            /* extract target */
            char target[MAX_SYMBOL]={0};
            int tlen=(int)(colon-t);
            if (tlen>0&&tlen<MAX_SYMBOL) {
                strncpy(target,t,tlen);
                target[tlen]='\0';
                /* trim */
                while(tlen>0&&(target[tlen-1]==' '||target[tlen-1]=='\t'))
                    target[--tlen]='\0';
                /* skip pattern rules like %.o */
                if (!strchr(target,'%')&&!skip_dep(target))
                    add_def(defs,def_count,max_defs,target,fe->name);
            }

            /* extract deps */
            const char *p=skip_ws(colon+1);
            while (*p) {
                char tok[MAX_SYMBOL]={0}; int l=read_token(p,tok,sizeof(tok));
                if (l&&!skip_dep(tok)&&!strchr(tok,'%')) {
                    char clean[MAX_SYMBOL]={0};
                    strip_make_vars(tok,clean,sizeof(clean));
                    if (clean[0]) add_call(fe,clean);
                }
                p+=l; p=skip_ws(p);
                /* handle line continuation */
                if (*p=='\\'&&*(p+1)=='\0') break;
            }
        }
    }
    fclose(f);
}
