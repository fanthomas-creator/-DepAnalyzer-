/* parser_bazel.c  -  Bazel BUILD / WORKSPACE dependency parser */
#include "parser_bazel.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static int ri(const char *s,char *o,int n){int i=0;while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='-'||*s=='.')&&i<n-1)o[i++]=*s++;o[i]=0;return i;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){if(!v||!v[0])return;for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){if(!f||!f[0])return;for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}}

static int eq(const char *s,char *o,int n){
    while(*s&&*s!='"'&&*s!='\'')s++;
    if(!*s)return 0;
    char q=*s++;int i=0;
    while(*s&&*s!=q&&i<n-1)o[i++]=*s++;
    o[i]=0;return i;
}

/* Classify a label: "//src/lib:foo" → internal, "@maven//:guava" → external */
static void classify_label(const char *label, FileEntry *fe) {
    if(!label||!label[0])return;
    if(label[0]=='@'){
        /* external: @rules_go//... or @maven//:guava */
        char repo[MAX_SYMBOL]={0};int i=0;
        const char *p=label+1;
        while(*p&&*p!='/'&&*p!='['&&i<MAX_SYMBOL-1)repo[i++]=*p++;
        repo[i]=0;
        if(repo[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,repo);
    } else if(label[0]=='/'&&label[1]=='/'){
        /* internal: //src/lib:foo */
        char path[MAX_SYMBOL]={0};strncpy(path,label+2,MAX_SYMBOL-1);
        char *colon=strchr(path,':');if(colon)*colon=0;
        /* get last dir component */
        char *sl=strrchr(path,'/');const char *base=sl?sl+1:path;
        if(base[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",base);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
    }
}

void parser_bazel_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");if(!f)return;
    char line[MAX_LINE];int in_bc=0;
    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        if(!in_bc&&strstr(line,"\"\"\""))in_bc=!in_bc;
        else if(in_bc&&strstr(line,"\"\"\""))in_bc=0;
        if(in_bc)continue;
        char *cm=strchr(line,'#');if(cm)*cm=0;
        const char *t=ws(line);if(!*t)continue;

        /* load("@rules_go//go:def.bzl", "go_binary") */
        if(sw(t,"load(")){
            const char *p=t+5;
            char src[MAX_PATH]={0};
            if(eq(p,src,sizeof(src))&&src[0])classify_label(src,fe);
        }
        /* http_archive / git_repository: name = "..." */
        if(sw(t,"http_archive")||sw(t,"git_repository")||sw(t,"http_file")){
            const char *np=strstr(t,"name");
            if(np){const char *eq2=strchr(np,'=');if(eq2){char nm[MAX_SYMBOL]={0};eq(eq2+1,nm,sizeof(nm));if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);}}
        }
        /* name = "target_name" → def */
        if(sw(t,"name")){
            const char *eq2=strchr(t,'=');
            if(eq2){char nm[MAX_SYMBOL]={0};eq(eq2+1,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}
        }
        /* deps = [...] or srcs = [...] - scan for labels */
        if(sw(t,"deps")||sw(t,"data")){
            const char *p=t;
            while((p=strchr(p,'"'))!=NULL){
                p++;char label[MAX_SYMBOL]={0};int i=0;
                while(*p&&*p!='"'&&i<MAX_SYMBOL-1)label[i++]=*p++;
                label[i]=0;
                if(label[0]&&(label[0]=='@'||label[0]=='/'))classify_label(label,fe);
                if(*p)p++;
            }
        }
    }
    fclose(f);
}
