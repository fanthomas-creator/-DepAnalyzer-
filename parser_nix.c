/* parser_nix.c  -  Nix expression dependency parser */
#include "parser_nix.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static int ri(const char *s,char *o,int n){int i=0;while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='-'||*s=='.')&&i<n-1)o[i++]=*s++;o[i]=0;return i;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){if(!v||!v[0])return;for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);}

static void store_local(FileEntry *fe,const char *path){
    const char *base=path;
    for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
    char no_ext[MAX_SYMBOL]={0};strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.');if(dot)*dot=0;
    if(no_ext[0]&&strcmp(no_ext,"default")){
        char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",no_ext);
        as(fe->imports,&fe->import_count,MAX_IMPORTS,t);
    }
}

void parser_nix_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs;(void)def_count;(void)max_defs;
    FILE *f=fopen(fe->path,"r");if(!f)return;
    char line[MAX_LINE];int in_bc=0;
    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        if(!in_bc&&strstr(line,"/*"))in_bc=1;
        if(in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cm=strstr(line,"#");if(cm)*cm=0;
        const char *t=ws(line);if(!*t)continue;

        /* import ./path.nix → internal */
        if(sw(t,"import ")){
            const char *p=ws(t+7);
            if(*p=='.'||*p=='/'){
                char path[MAX_PATH]={0};int i=0;
                while(*p&&*p!=' '&&*p!='\t'&&*p!=';'&&*p!='\n'&&i<MAX_PATH-1)path[i++]=*p++;path[i]=0;
                store_local(fe,path);
            } else if(*p=='<'){
                /* import <nixpkgs> → external channel */
                p++;char nm[MAX_SYMBOL]={0};int i=0;
                while(*p&&*p!='>'&&i<MAX_SYMBOL-1)nm[i++]=*p++;nm[i]=0;
                if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
            }
        }
        /* pkgs.callPackage ./pkg { } */
        if(strstr(t,"callPackage")){
            const char *p=strstr(t,"callPackage");p+=11;p=ws(p);
            if(*p=='.'||*p=='/'){char path[MAX_PATH]={0};int i=0;while(*p&&*p!=' '&&*p!='\t'&&i<MAX_PATH-1)path[i++]=*p++;path[i]=0;store_local(fe,path);}
        }
        /* buildInputs = [ pkg1 pkg2 ] or nativeBuildInputs */
        if(strstr(t,"buildInputs")||strstr(t,"propagatedBuildInputs")||
           strstr(t,"buildDepends")||strstr(t,"dependencies")){
            /* scan for identifiers in [ ] */
            const char *p=strchr(t,'[');
            if(p){p++;
                while(*p&&*p!=']'){
                    p=ws(p);
                    char nm[MAX_SYMBOL]={0};int l=ri(p,nm,sizeof(nm));
                    /* only lowercase names are likely packages */
                    if(l&&nm[0]&&islower((unsigned char)nm[0])&&strlen(nm)>2) {
                        /* strip pkgs. / lib. / stdenv. prefix */
                        const char *pkg=nm;
                        if(strncmp(pkg,"pkgs.",5)==0)   pkg+=5;
                        else if(strncmp(pkg,"lib.",4)==0)pkg+=4;
                        if(pkg[0]) as(fe->imports,&fe->import_count,MAX_IMPORTS,pkg);
                    }
                    p+=l;
                    if(!l)p++;
                }
            }
        }
    }
    fclose(f);
}
