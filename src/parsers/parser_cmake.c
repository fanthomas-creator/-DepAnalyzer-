/* parser_cmake.c  -  CMake dependency parser */
#include "parser_cmake.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int swi(const char *s,const char *p){while(*p){if(tolower((unsigned char)*s)!=tolower((unsigned char)*p))return 0;s++;p++;}return 1;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){if(!v||!v[0])return;for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){if(!f||!f[0])return;for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}}

/* Extract first unquoted token after keyword */
static const char *first_arg(const char *s) {
    /* skip to ( */
    while(*s&&*s!='(')s++;
    if(!*s)return s;
    return ws(s+1);
}
static int read_arg(const char *s,char *o,int n){
    const char *p=s;
    if(*p=='"'){p++;int i=0;while(*p&&*p!='"'&&i<n-1)o[i++]=*p++;o[i]=0;return i;}
    int i=0;
    while(*p&&*p!=' '&&*p!='\t'&&*p!=')'&&*p!='\n'&&i<n-1)o[i++]=*p++;o[i]=0;return i;
}

static void store_local(FileEntry *fe,const char *path){
    const char *base=path;
    for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
    char no_ext[MAX_SYMBOL]={0};strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.');if(dot)*dot=0;
    if(no_ext[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",no_ext);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
}

void parser_cmake_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");if(!f)return;
    char line[MAX_LINE];
    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        char *cm=strchr(line,'#');if(cm)*cm=0;
        const char *t=ws(line);if(!*t)continue;

        /* find_package(OpenSSL REQUIRED) → "OpenSSL" */
        if(swi(t,"find_package(")||swi(t,"find_package (")){
            const char *p=first_arg(t);
            char nm[MAX_SYMBOL]={0};read_arg(p,nm,sizeof(nm));
            if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
        }
        /* add_subdirectory(path) → internal structural dep (as call) */
        else if(swi(t,"add_subdirectory(")||swi(t,"add_subdirectory (")){
            const char *p=first_arg(t);
            char path[MAX_PATH]={0};read_arg(p,path,sizeof(path));
            if(path[0]){
                /* get basename of subdirectory path */
                const char *base=path;
                for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
                if(base[0])as(fe->calls,&fe->call_count,MAX_CALLS,base);
            }
        }
        /* include(file.cmake) → internal */
        else if(swi(t,"include(")||swi(t,"include (")){
            const char *p=first_arg(t);
            char path[MAX_PATH]={0};read_arg(p,path,sizeof(path));
            /* skip CMake built-in modules like CTest, GNUInstallDirs */
            if(path[0]&&strchr(path,'/')||strchr(path,'.'))store_local(fe,path);
        }
        /* FetchContent_Declare(name ...) → external */
        else if(swi(t,"FetchContent_Declare(")||swi(t,"fetchcontent_declare(")){
            const char *p=first_arg(t);
            char nm[MAX_SYMBOL]={0};read_arg(p,nm,sizeof(nm));
            if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
        }
        /* target_link_libraries(target dep1 dep2) */
        else if(swi(t,"target_link_libraries(")){
            const char *p=first_arg(t);
            /* skip target name */
            char skip[MAX_SYMBOL]={0};int sl=read_arg(p,skip,sizeof(skip));
            p=ws(p+sl);
            /* skip keywords */
            const char *kws[]={"PUBLIC","PRIVATE","INTERFACE",NULL};
            for(int i=0;kws[i];i++){if(!strcmp(skip,kws[i])){read_arg(p,skip,sizeof(skip));p=ws(p+strlen(skip));}}
            /* remaining are deps */
            while(*p&&*p!=')'){
                char dep[MAX_SYMBOL]={0};int dl=read_arg(p,dep,sizeof(dep));
                if(dl&&dep[0]&&strcmp(dep,"PUBLIC")&&strcmp(dep,"PRIVATE")&&strcmp(dep,"INTERFACE"))
                    as(fe->calls,&fe->call_count,MAX_CALLS,dep);
                p=ws(p+dl);
            }
        }
        /* project(Name) → def */
        else if(swi(t,"project(")){
            const char *p=first_arg(t);
            char nm[MAX_SYMBOL]={0};read_arg(p,nm,sizeof(nm));
            if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);
        }
        /* add_executable / add_library → def */
        else if(swi(t,"add_executable(")||swi(t,"add_library(")){
            const char *p=first_arg(t);
            char nm[MAX_SYMBOL]={0};read_arg(p,nm,sizeof(nm));
            if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);
        }
    }
    fclose(f);
}
