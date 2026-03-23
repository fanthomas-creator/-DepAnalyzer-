/* parser_groovy.c  -  Groovy / Gradle / Jenkinsfile parser */
#include "parser_groovy.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static int ri(const char *s,char *o,int n){int i=0;while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='$')&&i<n-1)o[i++]=*s++;o[i]=0;return i;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){if(!v||!v[0])return;for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){if(!f||!f[0])return;for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}}

/* Extract quoted string */
static int eq(const char *s,char *o,int n){
    while(*s&&*s!='"'&&*s!='\'')s++;
    if(!*s)return 0;
    char q=*s++;int i=0;
    while(*s&&*s!=q&&i<n-1)o[i++]=*s++;
    o[i]=0;return i;
}

/* Gradle dependency: 'group:artifact:version' → "artifact" */
static void parse_gradle_dep(const char *line, FileEntry *fe) {
    const char *kws[]={"implementation","api","compileOnly","runtimeOnly",
                       "testImplementation","testCompileOnly","classpath",
                       "compile","testCompile","provided",NULL};
    const char *p=ws(line);
    for(int i=0;kws[i];i++){
        if(!sw(p,kws[i]))continue;
        char dep[MAX_SYMBOL]={0};
        if(eq(p,dep,sizeof(dep))&&dep[0]){
            /* "group:artifact:version" → get artifact (second field) */
            char art[MAX_SYMBOL]={0};
            const char *c=dep,*prev=dep;
            int colons=0;
            while(*c){
                if(*c==':'){colons++;if(colons==1)prev=c+1;}
                c++;
            }
            if(colons>=1){
                int al=0;const char *end=strchr(prev,':');
                if(!end)end=prev+strlen(prev);
                al=(int)(end-prev);
                if(al<MAX_SYMBOL){strncpy(art,prev,al);art[al]=0;}
            } else strncpy(art,dep,MAX_SYMBOL-1);
            if(art[0]) as(fe->imports,&fe->import_count,MAX_IMPORTS,art);
        }
        return;
    }
}

void parser_groovy_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");if(!f)return;
    char line[MAX_LINE];int in_bc=0;
    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        if(!in_bc&&strstr(line,"/*"))in_bc=1;
        if(in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
        char *cm=strstr(line,"//");if(cm)*cm=0;
        const char *t=ws(line);if(!*t)continue;

        /* import com.example.Foo → root "com" / "org" etc. */
        if(sw(t,"import ")){
            const char *p=ws(t+7);
            if(sw(p,"static "))p=ws(p+7);
            /* for well-known generic roots (org, com, net, io), use second component */
            char root[MAX_SYMBOL]={0};int i=0;
            while(*p&&*p!='.'&&*p!=' '&&i<MAX_SYMBOL-1)root[i++]=*p++;root[i]=0;
            const char *dep=root;
            static const char *generic[]={"org","com","net","io","edu","gov",NULL};
            int is_gen=0;for(int gi=0;generic[gi];gi++)if(!strcmp(root,generic[gi])){is_gen=1;break;}
            char second[MAX_SYMBOL]={0};
            if(is_gen&&*p=='.'){p++;i=0;while(*p&&*p!='.'&&*p!=' '&&i<MAX_SYMBOL-1)second[i++]=*p++;second[i]=0;if(second[0])dep=second;}
            if(dep[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,dep);
        }
        /* @Grab('group:artifact:version') */
        if(sw(t,"@Grab")){char dep[MAX_SYMBOL]={0};eq(t,dep,sizeof(dep));if(dep[0])as(fe->imports,&fe->import_count,MAX_IMPORTS,dep);}

        /* Gradle deps */
        parse_gradle_dep(t,fe);

        /* class / def */
        if(sw(t,"class ")){const char *p=ws(t+6);char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}
        if(sw(t,"def ")){const char *p=ws(t+4);char nm[MAX_SYMBOL]={0};int l=ri(p,nm,sizeof(nm));if(l){const char *a=ws(p+l);if(*a=='(')ad(defs,def_count,max_defs,nm,fe->name);}}
        /* interface / enum / trait */
        const char *kws2[]={"interface ","enum ","trait ",NULL};
        for(int i=0;kws2[i];i++){if(sw(t,kws2[i])){const char *p=ws(t+strlen(kws2[i]));char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);}}
    }
    fclose(f);
}
