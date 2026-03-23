/* parser_yaml.c  -  YAML dependency parser
 *
 * Handles key YAML patterns:
 *
 * docker-compose.yml:
 *   image: nginx:1.21            → "nginx" as external dep
 *   build: ./my-service          → "my-service" as internal dep
 *   depends_on: [db, cache]      → "db","cache" as internal deps
 *
 * GitHub Actions (.github/workflows/*.yml):
 *   uses: actions/checkout@v3    → "actions/checkout"
 *   uses: docker/login-action@v2 → "docker/login-action"
 *
 * Kubernetes manifests:
 *   image: my-registry/app:v1   → "my-registry/app"
 *   configMapRef: name: my-cfg  → "my-cfg"
 *   secretRef: name: my-secret  → "my-secret"
 *
 * Ansible:
 *   hosts: webservers            → "webservers"
 *   roles: [common, nginx]       → "common","nginx"
 *   include_tasks: tasks/db.yml  → "db" (internal)
 *   import_tasks: setup.yml      → "setup" (internal)
 *
 * Generic:
 *   Any line with known dep-key: value pattern
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_yaml.h"
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
static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    /* strip quotes */
    char clean[MAX_SYMBOL]={0}; int i=0;
    const char *p=val;
    if (*p=='"'||*p=='\'') p++;
    while (*p&&*p!='"'&&*p!='\''&&i<MAX_SYMBOL-1) clean[i++]=*p++;
    clean[i]='\0';
    if (!clean[0]) return;
    for (int j=0;j<fe->import_count;j++) if(strcmp(fe->imports[j],clean)==0) return;
    if (fe->import_count<MAX_IMPORTS) strncpy(fe->imports[fe->import_count++],clean,MAX_SYMBOL-1);
}
static void add_call(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    char clean[MAX_SYMBOL]={0}; int i=0;
    const char *p=val;
    if (*p=='"'||*p=='\'') p++;
    while (*p&&*p!='"'&&*p!='\''&&i<MAX_SYMBOL-1) clean[i++]=*p++;
    clean[i]='\0';
    if (!clean[0]) return;
    for (int j=0;j<fe->call_count;j++) if(strcmp(fe->calls[j],clean)==0) return;
    if (fe->call_count<MAX_CALLS) strncpy(fe->calls[fe->call_count++],clean,MAX_SYMBOL-1);
}

/* Strip version tag: "nginx:1.21" → "nginx", "actions/checkout@v3" → "actions/checkout" */
static void strip_version(const char *s, char *out, int out_size) {
    int i=0;
    while (*s&&*s!=':'&&*s!='@'&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
}

/* Parse a YAML list value: "- item" or "[a, b, c]" */
static void parse_list_value(const char *p, FileEntry *fe, int is_internal) {
    if (*p=='[') {
        /* inline list: [a, b, c] */
        p++;
        while (*p&&*p!=']') {
            p=skip_ws(p);
            char item[MAX_SYMBOL]={0}; int i=0;
            while (*p&&*p!=','&&*p!=']'&&*p!='\n'&&i<MAX_SYMBOL-1) item[i++]=*p++;
            item[i]='\0';
            /* rtrim */
            while (i>0&&(item[i-1]==' '||item[i-1]=='\t')) item[--i]='\0';
            if (item[0]) {
                if (is_internal) add_call(fe,item); else add_import(fe,item);
            }
            if (*p==',') p++;
        }
    } else if (*p=='-') {
        /* list item line: "  - value" */
        p=skip_ws(p+1);
        char item[MAX_SYMBOL]={0}; int i=0;
        while (*p&&*p!='\n'&&i<MAX_SYMBOL-1) item[i++]=*p++;
        item[i]='\0';
        while (i>0&&(item[i-1]==' '||item[i-1]=='\t')) item[--i]='\0';
        if (item[0]) {
            if (is_internal) add_call(fe,item); else add_import(fe,item);
        }
    } else {
        /* plain value */
        char item[MAX_SYMBOL]={0}; int i=0;
        while (*p&&*p!='\n'&&i<MAX_SYMBOL-1) item[i++]=*p++;
        item[i]='\0';
        while (i>0&&(item[i-1]==' '||item[i-1]=='\t')) item[--i]='\0';
        if (item[0]) {
            if (is_internal) add_call(fe,item); else add_import(fe,item);
        }
    }
}

/* ================================================================
 * Key handlers
 * ================================================================ */

/* image: nginx:1.21 or image: my.registry/app:tag */
static void handle_image(const char *val, FileEntry *fe) {
    char name[MAX_SYMBOL]={0};
    strip_version(val,name,sizeof(name));
    if (name[0]) add_import(fe,name);
}

/* build: ./path or build: context: ./path
 * We only record build paths that point to actual files (have an extension).
 * Directory builds like "./api" are skipped — they are service dirs, not file deps. */
static void handle_build(const char *val, FileEntry *fe) {
    /* skip if it looks like a pure directory path (no extension in last component) */
    const char *base=val;
    for (const char *c=val;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
    /* strip leading dots/slashes from base */
    while (*base=='.') base++;
    while (*base=='/') base++;
    /* if no dot in basename → it's a directory, skip */
    if (!strchr(base,'.')) return;
    /* it has an extension → treat as file reference */
    char name[MAX_SYMBOL]={0}; int i=0;
    const char *p=base;
    while (*p&&*p!='.'&&i<MAX_SYMBOL-1) name[i++]=*p++;
    name[i]='\0';
    if (name[0]) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",name);
        add_import(fe,tagged);
    }
}

/* uses: actions/checkout@v3 */
static void handle_uses(const char *val, FileEntry *fe) {
    char name[MAX_SYMBOL]={0};
    strip_version(val,name,sizeof(name));
    if (name[0]) add_import(fe,name);
}

/* include_tasks: tasks/setup.yml  or  import_tasks: setup.yml */
static void handle_task_file(const char *val, FileEntry *fe) {
    /* get basename without extension */
    const char *base=val;
    for (const char *c=val;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
    char no_ext[MAX_SYMBOL]={0};
    strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.'); if(dot) *dot='\0';
    if (no_ext[0]) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
        add_import(fe,tagged);
    }
}

/* ================================================================ Public API */

void parser_yaml_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;

    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_yaml] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    /* track list context for multi-line lists */
    typedef enum { CTX_NONE,CTX_DEPENDS,CTX_ROLES,CTX_HOSTS,CTX_VOLUMES } Context;
    Context ctx=CTX_NONE;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        const char *t=skip_ws(line);

        /* skip comments */
        if (*t=='#') continue;

        /* detect list continuation (line starts with -) */
        if (*t=='-' && ctx!=CTX_NONE) {
            const char *val=skip_ws(t+1);
            char item[MAX_SYMBOL]={0}; int i=0;
            while (*val&&i<MAX_SYMBOL-1) item[i++]=*val++;
            item[i]='\0';
            /* strip inline comment */
            char *cm=strchr(item,'#'); if(cm)*cm='\0';
            while (i>0&&(item[i-1]==' '||item[i-1]=='\t')) item[--i]='\0';
            if (item[0]) {
                if (ctx==CTX_DEPENDS||ctx==CTX_ROLES||ctx==CTX_HOSTS) add_call(fe,item);
                else if (ctx==CTX_VOLUMES) {
                    /* skip volume mount paths like "pg_data:/var/lib/..." */
                    /* only store named volumes (no : or / in name) */
                    if (!strchr(item,':')&&!strchr(item,'/')) add_import(fe,item);
                } else add_import(fe,item);
            }
            continue;
        }
        /* non-list line resets context */
        if (*t!='-') ctx=CTX_NONE;

        /* find key: */
        const char *colon=strchr(t,':');
        if (!colon) continue;

        /* extract key */
        char key[MAX_SYMBOL]={0};
        int klen=(int)(colon-t);
        if (klen<=0||klen>=MAX_SYMBOL) continue;
        strncpy(key,t,klen);
        key[klen]='\0';
        /* strip leading spaces from key (indented YAML) */
        const char *kp=skip_ws(key);

        /* get value (after colon) */
        const char *val=skip_ws(colon+1);
        /* strip inline comment */
        char val_buf[MAX_PATH]={0};
        strncpy(val_buf,val,MAX_PATH-1);
        char *cm=strchr(val_buf,'#'); if(cm)*cm='\0';
        int vl=(int)strlen(val_buf);
        while(vl>0&&(val_buf[vl-1]==' '||val_buf[vl-1]=='\t')) val_buf[--vl]='\0';
        val=val_buf;

        /* ---- docker-compose / k8s keys ---- */
        if (strcmp(kp,"image")==0)           { handle_image(val,fe); }
        else if (strcmp(kp,"build")==0)      { handle_build(val,fe); }
        else if (strcmp(kp,"depends_on")==0) { ctx=CTX_DEPENDS; parse_list_value(val,fe,1); }
        else if (strcmp(kp,"extends")==0)    { add_call(fe,val); }
        else if (strcmp(kp,"network_mode")==0) {}  /* skip */
        else if (strcmp(kp,"volumes")==0)    { ctx=CTX_VOLUMES; }

        /* ---- GitHub Actions ---- */
        else if (strcmp(kp,"uses")==0)       { handle_uses(val,fe); }
        else if (strcmp(kp,"with")==0)       {}
        else if (strcmp(kp,"run")==0)        {}

        /* ---- k8s ---- */
        else if (strcmp(kp,"name")==0 && val[0]) {
            /* inside configMapRef / secretRef blocks */
            add_call(fe,val);
        }
        else if (strcmp(kp,"configMapRef")==0||strcmp(kp,"secretRef")==0||
                 strcmp(kp,"serviceAccountName")==0) { add_call(fe,val); }

        /* ---- Ansible ---- */
        else if (strcmp(kp,"hosts")==0)        { parse_list_value(val,fe,1); }
        else if (strcmp(kp,"roles")==0)        { ctx=CTX_ROLES; parse_list_value(val,fe,1); }
        else if (strcmp(kp,"include_tasks")==0||
                 strcmp(kp,"import_tasks")==0||
                 strcmp(kp,"include_playbook")==0||
                 strcmp(kp,"import_playbook")==0) { handle_task_file(val,fe); }
        else if (strcmp(kp,"include_vars")==0) { handle_task_file(val,fe); }
        else if (strcmp(kp,"src")==0||strcmp(kp,"file")==0) {
            /* generic file reference */
            if (val[0]=='.'||strchr(val,'/')) handle_task_file(val,fe);
        }
    }
    fclose(f);
}
