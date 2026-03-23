/* parser_sol.c  -  Solidity smart contract dependency parser */
#include "parser_sol.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static int ri(const char *s,char *o,int n){int i=0;while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<n-1)o[i++]=*s++;o[i]=0;return i;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){if(!v||!v[0])return;for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){if(!f||!f[0])return;for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}}

static int eq(const char *s,char *o,int n){
    while(*s&&*s!='"'&&*s!='\'')s++;
    if(!*s)return 0;
    char q=*s++;int i=0;
    while(*s&&*s!=q&&i<n-1)o[i++]=*s++;
    o[i]=0;return i;
}

void parser_sol_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");if(!f)return;
    char line[MAX_LINE];int in_bc=0;
    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        if(!in_bc&&strstr(line,"/*"))in_bc=1;
        if(in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cm=strstr(line,"//");if(cm)*cm=0;
        const char *t=ws(line);if(!*t)continue;

        /* import "./interfaces/IERC20.sol"          → internal
           import "@openzeppelin/contracts/token/..."→ external "openzeppelin" */
        if(sw(t,"import ")){
            char path[MAX_PATH]={0};eq(t,path,sizeof(path));
            if(path[0]){
                if(path[0]=='.'||path[0]=='/'){
                    const char *base=path;
                    for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
                    char no_ext[MAX_SYMBOL]={0};strncpy(no_ext,base,MAX_SYMBOL-1);
                    char *dot=strrchr(no_ext,'.');if(dot)*dot=0;
                    if(no_ext[0]){char tg[MAX_SYMBOL]={0};snprintf(tg,sizeof(tg),"local:%s",no_ext);as(fe->imports,&fe->import_count,MAX_IMPORTS,tg);}
                } else if(path[0]=='@'){
                    /* @openzeppelin/... → "openzeppelin" */
                    char pkg[MAX_SYMBOL]={0};int i=0;
                    const char *p=path+1;
                    while(*p&&*p!='/'&&i<MAX_SYMBOL-1)pkg[i++]=*p++;pkg[i]=0;
                    if(pkg[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,pkg);
                } else {
                    char root[MAX_SYMBOL]={0};int i=0;
                    const char *p=path;
                    while(*p&&*p!='/'&&i<MAX_SYMBOL-1)root[i++]=*p++;root[i]=0;
                    if(root[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,root);
                }
            }
        }

        /* contract Foo is Bar, Baz → def Foo, deps Bar/Baz */
        const char *kws[]={"contract ","abstract contract ","interface ","library ",NULL};
        for(int i=0;kws[i];i++){
            if(!sw(t,kws[i]))continue;
            const char *p=ws(t+strlen(kws[i]));
            char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
            if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);
            /* inheritance: is Bar, Baz */
            const char *is=strstr(p," is ");
            if(is){p=is+4;
                while(*p&&*p!='{'){
                    p=ws(p);
                    char dep[MAX_SYMBOL]={0};int l=ri(p,dep,sizeof(dep));
                    if(l&&dep[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,dep);
                    p+=l;p=ws(p);if(*p==',')p++;
                }
            }
            break;
        }
        /* function myFunc(...) → def */
        if(sw(t,"function ")){const char *p=ws(t+9);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}
        /* event / modifier / error */
        const char *evkws[]={"event ","modifier ","error ",NULL};
        for(int i=0;evkws[i];i++){if(sw(t,evkws[i])){const char *p=ws(t+strlen(evkws[i]));char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}}
        /* using SafeMath for uint256 */
        if(sw(t,"using ")){const char *p=ws(t+6);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,nm);}
    }
    fclose(f);
}
