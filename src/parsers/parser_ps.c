/* parser_ps.c  -  PowerShell dependency parser */
#include "parser_ps.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static int swi(const char *s,const char *p){/* case-insensitive */
    while(*p){if(tolower((unsigned char)*s)!=tolower((unsigned char)*p))return 0;s++;p++;}return 1;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static int ri(const char *s,char *o,int n){int i=0;while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='-')&&i<n-1)o[i++]=*s++;o[i]=0;return i;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){if(!v||!v[0])return;for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){if(!f||!f[0])return;for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}}

static void store_local(FileEntry *fe, const char *path) {
    const char *base=path;
    for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
    char no_ext[MAX_SYMBOL]={0};strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.');if(dot)*dot=0;
    /* strip surrounding quotes */
    if(no_ext[0]=='"'||no_ext[0]=='\''){memmove(no_ext,no_ext+1,strlen(no_ext));int l=strlen(no_ext);if(l>0&&(no_ext[l-1]=='"'||no_ext[l-1]=='\''))no_ext[l-1]=0;}
    if(no_ext[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",no_ext);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
}

void parser_ps_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");if(!f)return;
    char line[MAX_LINE];int in_bc=0;
    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        if(!in_bc&&strstr(line,"<#"))in_bc=1;
        if(in_bc){if(strstr(line,"#>"))in_bc=0;continue;}
        /* strip # comments but not #Requires */
        char *cm=strchr(line,'#');
        if(cm&&!swi(cm,"#requires")&&!swi(cm,"#region")&&!swi(cm,"#endregion"))*cm=0;
        const char *t=ws(line);if(!*t)continue;

        /* dot-source: . ./lib.ps1 or . "$PSScriptRoot/utils.ps1" */
        if(*t=='.'&&(*(t+1)==' '||*(t+1)=='\t')){
            const char *p=ws(t+2);
            if(*p=='.'||*p=='$'){store_local(fe,p);}
        }
        /* & ./script.ps1 */
        if(*t=='&'){const char *p=ws(t+2);if(*p=='.'||*p=='"')store_local(fe,p);}

        /* Import-Module */
        if(swi(t,"Import-Module ")){
            const char *p=ws(t+14);
            if(*p=='.'||*p=='$'||*p=='"')store_local(fe,p);
            else{char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);}
        }
        /* using module / using namespace */
        if(swi(t,"using module ")){const char *p=ws(t+13);store_local(fe,p);}
        if(swi(t,"using namespace ")){
            const char *p=ws(t+16);
            char root[MAX_SYMBOL]={0};int i=0;
            while(*p&&*p!='.'&&i<MAX_SYMBOL-1)root[i++]=*p++;root[i]=0;
            if(root[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,root);
        }
        /* #Requires -Module ModName */
        if(swi(t,"#requires")){
            const char *p=strstr(t,"-Module");if(!p)p=strstr(t,"-module");
            if(p){p=ws(p+7);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);}
        }

        /* function My-Function { */
        if(swi(t,"function ")){const char *p=ws(t+9);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}
        if(swi(t,"filter ")){const char *p=ws(t+7);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}
        if(swi(t,"class ")){const char *p=ws(t+6);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}
    }
    fclose(f);
}
